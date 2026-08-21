#!/usr/bin/env python3
"""
p3dconv - repack Pure3D (.p3d) assets for the Sega Dreamcast.

Two passes, both aimed at the 16MB/8MB memory budget:

  textures  Every embedded PNG/DXT image is re-encoded to a twiddled,
            VQ-compressed .DT texture with pvrtex. The Dreamcast cannot
            sample S3TC, so leaving the source data alone means paying full
            16bpp in VRAM for every surface.

  anims     Rotation channels stored as 4x float32 quaternions are rewritten
            to the packed 16-bit form the engine already supports, halving
            their cost in RAM and on disc.

Only the standard library is required. pvrtex must be on PATH or given with
--pvrtex; it ships in KallistiOS under utils/pvrtex.

  p3dconv.py art/ art-dc/          convert a tree
  p3dconv.py -n art/               report what would change, write nothing
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha1

import meshsplit

# ---------------------------------------------------------------------------
# Pure3D chunk ids
# ---------------------------------------------------------------------------
CHUNK_IMAGE = 0x00019001
CHUNK_IMAGE_DATA = 0x00019002
CHUNK_QUATERNION = 0x00121105
CHUNK_COMPRESSED_QUATERNION = 0x00121111

# tImageHandler::Format value claimed for Dreamcast .DT textures. Must match
# IMG_DC_DT in libs/pure3d/p3d/imagefactory.hpp.
IMG_DC_DT = 20

# Source formats we know how to re-encode, from the same enum.
FMT_PNG, FMT_TGA, FMT_BMP = 1, 2, 3
FMT_DXT, FMT_DXT1, FMT_DXT3, FMT_DXT5 = 5, 6, 8, 10
FMT_EXT = {FMT_PNG: ".png", FMT_TGA: ".tga", FMT_BMP: ".bmp"}
FMT_DDS = {FMT_DXT, FMT_DXT1, FMT_DXT3, FMT_DXT5}


class ConversionError(Exception):
    pass


# ---------------------------------------------------------------------------
# Chunk tree
# ---------------------------------------------------------------------------
class Chunk:
    """A Pure3D chunk: 12-byte header, a payload, then child chunks.

    On disc the header is (id, data_end, total_end) where data_end is the
    offset from the chunk start to the first child, so the payload length is
    data_end - 12. Rebuilding from this tree recomputes both lengths, which is
    what lets a pass resize a payload without touching its ancestors.
    """

    __slots__ = ("id", "data", "children")

    def __init__(self, cid, data=b"", children=None):
        self.id = cid
        self.data = data
        self.children = children if children is not None else []

    @property
    def total_size(self):
        return 12 + len(self.data) + sum(c.total_size for c in self.children)

    def serialise(self, out):
        out.append(struct.pack("<III", self.id, 12 + len(self.data), self.total_size))
        out.append(self.data)
        for c in self.children:
            c.serialise(out)

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()

    def child(self, cid):
        for c in self.children:
            if c.id == cid:
                return c
        return None


def parse_chunk(buf, off, end):
    cid, dend, tend = struct.unpack_from("<III", buf, off)
    if tend < 12 or dend < 12 or dend > tend or off + tend > end:
        raise ConversionError(f"malformed chunk at offset {off}")
    node = Chunk(cid, buf[off + 12: off + dend])
    pos = off + dend
    stop = off + tend
    while pos + 12 <= stop:
        kid = parse_chunk(buf, pos, stop)
        node.children.append(kid)
        pos += kid.total_size
    return node


def parse_p3d(buf):
    if len(buf) < 12:
        raise ConversionError("file is too small to be a .p3d")
    return parse_chunk(buf, 0, len(buf))


def serialise_p3d(root):
    out = []
    root.serialise(out)
    return b"".join(out)


# ---------------------------------------------------------------------------
# Pure3D string/field helpers
# ---------------------------------------------------------------------------
def read_pstr(buf, pos):
    """Length-prefixed, no terminator, no padding (see tChunkFile::GetString)."""
    n = buf[pos]
    return buf[pos + 1: pos + 1 + n], pos + 1 + n


class ImageHeader:
    """Layout of an IMAGE chunk payload."""

    def __init__(self, data):
        self.name, pos = read_pstr(data, 0)
        (self.version, self.width, self.height, self.bpp,
         self.palettized, self.alpha, self.format) = struct.unpack_from("<7I", data, pos)
        self._fields_at = pos
        self._raw = data

    def rebuild(self, width=None, height=None, fmt=None):
        head = self._raw[:self._fields_at]
        return head + struct.pack(
            "<7I", self.version,
            self.width if width is None else width,
            self.height if height is None else height,
            self.bpp, self.palettized, self.alpha,
            self.format if fmt is None else fmt)


# ---------------------------------------------------------------------------
# S3TC decoding - pvrtex reads PNG/TGA/BMP but not DDS, so DXT sources have to
# be expanded here before they can be re-encoded.
# ---------------------------------------------------------------------------
def _rgb565(c):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def _colour_table(c0, c1, four_colour):
    a, b = _rgb565(c0), _rgb565(c1)
    if four_colour or c0 > c1:
        return [a, b,
                tuple((2 * a[i] + b[i]) // 3 for i in range(3)),
                tuple((a[i] + 2 * b[i]) // 3 for i in range(3))], False
    return [a, b, tuple((a[i] + b[i]) // 2 for i in range(3)), (0, 0, 0)], True


def _alpha_table(a0, a1):
    if a0 > a1:
        return [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    return [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]


def decode_dxt(data, width, height, fourcc):
    """Decode DXT1/3/5 block data to a flat RGBA byte array."""
    block_bytes = 8 if fourcc == b"DXT1" else 16
    out = bytearray(width * height * 4)
    bw, bh = (width + 3) // 4, (height + 3) // 4
    pos = 0
    for by in range(bh):
        for bx in range(bw):
            if pos + block_bytes > len(data):
                raise ConversionError("truncated DXT data")
            blk = data[pos: pos + block_bytes]
            pos += block_bytes

            alphas = None
            if fourcc == b"DXT3":
                bits = int.from_bytes(blk[:8], "little")
                alphas = [((bits >> (4 * i)) & 0xF) * 17 for i in range(16)]
                blk = blk[8:]
            elif fourcc == b"DXT5":
                tbl = _alpha_table(blk[0], blk[1])
                bits = int.from_bytes(blk[2:8], "little")
                alphas = [tbl[(bits >> (3 * i)) & 7] for i in range(16)]
                blk = blk[8:]

            c0, c1 = struct.unpack_from("<HH", blk, 0)
            colours, punch = _colour_table(c0, c1, fourcc != b"DXT1")
            idx = struct.unpack_from("<I", blk, 4)[0]

            for t in range(16):
                px, py = bx * 4 + (t & 3), by * 4 + (t >> 2)
                if px >= width or py >= height:
                    continue
                sel = (idx >> (2 * t)) & 3
                r, g, b = colours[sel]
                if alphas is not None:
                    a = alphas[t]
                elif punch and sel == 3:
                    a = 0
                else:
                    a = 255
                o = (py * width + px) * 4
                out[o] = r
                out[o + 1] = g
                out[o + 2] = b
                out[o + 3] = a
    return bytes(out)


def dds_to_rgba(payload):
    if payload[:4] != b"DDS ":
        raise ConversionError("IMAGE_DATA is not a DDS file")
    height, width = struct.unpack_from("<II", payload, 12)
    fourcc = payload[84:88]
    if fourcc not in (b"DXT1", b"DXT3", b"DXT5"):
        raise ConversionError(f"unsupported DDS fourcc {fourcc!r}")
    rgba = decode_dxt(payload[128:], width, height, fourcc)
    stride = width * 4
    flipped = bytearray(len(rgba))
    for y in range(height):
        src = (height - 1 - y) * stride
        flipped[y * stride:(y + 1) * stride] = rgba[src:src + stride]
    return width, height, bytes(flipped)


def write_png(path, width, height, rgba):
    rows = bytearray()
    stride = width * 4
    for y in range(height):
        rows.append(0)
        rows += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 6)))
        f.write(chunk(b"IEND", b""))


# ---------------------------------------------------------------------------
# Texture pass
# ---------------------------------------------------------------------------
class TextureEncoder:
    """Runs pvrtex, memoised on the source bytes plus the encoding options.

    The same artwork appears in many .p3d files, so the cache turns a full
    re-run from tens of minutes into seconds.
    """

    def __init__(self, opts):
        self.pvrtex = opts.pvrtex
        self.cache_dir = opts.cache
        self.args = ["-f", opts.format]
        if not opts.no_vq:
            self.args += ["-c", opts.codebook]
        if opts.mipmap:
            self.args.append("-m")
        if opts.dither is not None:
            self.args += ["-d", str(opts.dither)]
        if opts.resize != "none":
            self.args += ["-r", opts.resize]
        if self.cache_dir:
            os.makedirs(self.cache_dir, exist_ok=True)
        self._key_salt = ("v2" + "\x00".join(self.args)).encode()

    def _cache_path(self, payload):
        digest = sha1(self._key_salt + payload).hexdigest()
        return os.path.join(self.cache_dir, digest + ".dt") if self.cache_dir else None

    def encode(self, payload, fmt):
        cached = self._cache_path(payload)
        if cached and os.path.exists(cached):
            with open(cached, "rb") as f:
                return f.read()

        with tempfile.TemporaryDirectory(prefix="p3dconv") as tmp:
            if fmt in FMT_DDS:
                w, h, rgba = dds_to_rgba(payload)
                src = os.path.join(tmp, "src.png")
                write_png(src, w, h, rgba)
            else:
                src = os.path.join(tmp, "src" + FMT_EXT.get(fmt, ".png"))
                with open(src, "wb") as f:
                    f.write(payload)

            dst = os.path.join(tmp, "out.dt")
            proc = subprocess.run([self.pvrtex, "-i", src, "-o", dst] + self.args,
                                  capture_output=True)
            if proc.returncode != 0 or not os.path.exists(dst):
                msg = (proc.stderr or proc.stdout).decode("utf-8", "replace").strip()
                raise ConversionError(msg.splitlines()[-1] if msg else "pvrtex failed")
            with open(dst, "rb") as f:
                data = f.read()

        if cached:
            # Workers are threads in one process, so the temp name has to be
            # unique per call rather than per pid.
            fd, tmp_out = tempfile.mkstemp(dir=self.cache_dir, suffix=".tmp")
            try:
                with os.fdopen(fd, "wb") as f:
                    f.write(data)
                os.replace(tmp_out, cached)
            except OSError:
                if os.path.exists(tmp_out):
                    os.unlink(tmp_out)
        return data


def dt_dimensions(dt):
    """(width, height) from a .DT header (see pvrtex file_dctex.h)."""
    if len(dt) < 32 or dt[:4] != b"DcTx":
        raise ConversionError("pvrtex did not produce a .DT file")
    return struct.unpack_from("<HH", dt, 12)


def convert_textures(root, encoder, stats, verbose):
    for image in root.walk():
        if image.id != CHUNK_IMAGE:
            continue
        blob = image.child(CHUNK_IMAGE_DATA)
        if blob is None or len(blob.data) < 4:
            continue

        hdr = ImageHeader(image.data)
        if hdr.format == IMG_DC_DT:
            stats.tex_already += 1
            continue
        if hdr.format not in FMT_EXT and hdr.format not in FMT_DDS:
            stats.tex_skipped += 1
            stats.note(f"unsupported image format {hdr.format}")
            continue

        size = struct.unpack_from("<I", blob.data, 0)[0]
        payload = blob.data[4:4 + size]
        try:
            dt = encoder.encode(payload, hdr.format)
            width, height = dt_dimensions(dt)
        except ConversionError as exc:
            stats.tex_failed += 1
            stats.note(f"{hdr.name.decode('latin-1')}: {exc}")
            continue

        stats.tex_done += 1
        stats.bytes_raw16 += hdr.width * hdr.height * 2
        stats.bytes_dt += len(dt)
        if (width, height) != (hdr.width, hdr.height):
            stats.tex_resized += 1
            if verbose:
                stats.note(f"{hdr.name.decode('latin-1')}: "
                           f"{hdr.width}x{hdr.height} -> {width}x{height}")

        image.data = hdr.rebuild(width=width, height=height, fmt=IMG_DC_DT)
        blob.data = struct.pack("<I", len(dt)) + dt


# ---------------------------------------------------------------------------
# Animation pass
# ---------------------------------------------------------------------------
def convert_anims(root, stats):
    """QUATERNION -> COMPRESSED_QUATERNION.

    Both payloads are (version, param, nKeys) then nKeys int16 frames then the
    values; only the value encoding differs, float32 w,x,y,z against int16
    w,x,y,z scaled by 32767. The reader does plain sequential memcpy, so there
    is no padding between the arrays.
    """
    for chunk in root.walk():
        if chunk.id != CHUNK_QUATERNION:
            continue
        data = chunk.data
        if len(data) < 12:
            continue
        version, param, nkeys = struct.unpack_from("<III", data, 0)
        if version != 0:
            stats.anim_skipped += 1
            continue

        frames_at = 12
        values_at = frames_at + nkeys * 2
        need = values_at + nkeys * 16
        if len(data) < need:
            stats.anim_skipped += 1
            stats.note("quaternion channel is shorter than its key count")
            continue

        floats = struct.unpack_from(f"<{nkeys * 4}f", data, values_at)
        packed = bytearray()
        for v in floats:
            # A few shipped channels carry NaN, which int() refuses.
            packed += struct.pack("<h", 0 if v != v
                                  else max(-32767, min(32767, int(v * 32767.0))))

        chunk.id = CHUNK_COMPRESSED_QUATERNION
        chunk.data = (struct.pack("<III", 0, param, nkeys)
                      + data[frames_at:values_at] + bytes(packed)
                      + data[need:])
        stats.anim_done += 1
        stats.anim_before += nkeys * 16
        stats.anim_after += nkeys * 8


# ---------------------------------------------------------------------------
# Driving
# ---------------------------------------------------------------------------
class Stats:
    def __init__(self):
        self.files = self.failed = self.copied = 0
        self.tex_done = self.tex_skipped = self.tex_failed = 0
        self.tex_already = self.tex_resized = 0
        self.bytes_raw16 = self.bytes_dt = 0
        self.anim_done = self.anim_skipped = 0
        self.anim_before = self.anim_after = 0
        self.split = self.pieces = self.kept = 0
        self.size_in = self.size_out = 0
        self.notes = []

    def note(self, text):
        if len(self.notes) < 40:
            self.notes.append(text)

    def merge(self, other):
        for k, v in vars(other).items():
            if k == "notes":
                for n in v:
                    self.note(n)
            else:
                setattr(self, k, getattr(self, k) + v)


def copy_through(src, dst, buf, opts):
    if not opts.dry_run and os.path.abspath(src) != os.path.abspath(dst):
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        with open(dst, "wb") as f:
            f.write(buf)


def convert_file(src, dst, encoder, opts):
    st = Stats()
    with open(src, "rb") as f:
        buf = f.read()
    st.size_in = len(buf)

    if not src.lower().endswith(".p3d"):
        st.copied = 1
        st.size_out = len(buf)
        copy_through(src, dst, buf, opts)
        return st

    try:
        root = parse_p3d(buf)
    except ConversionError as exc:
        # Pass anything we cannot read through untouched, so the destination is
        # always a complete, loadable tree. LZR-compressed ("P3DZ") files land
        # here; the engine reads those directly anyway.
        st.failed = 1
        st.size_out = len(buf)
        st.note(f"{src}: {exc} (copied unchanged)")
        copy_through(src, dst, buf, opts)
        return st

    if not opts.no_textures:
        convert_textures(root, encoder, st, opts.verbose)
    if not opts.no_anim:
        convert_anims(root, st)

    if opts.split_verts:
        mstats = {"split": 0, "pieces": 0, "kept": 0}
        meshsplit.split_file(Chunk, root, opts.split_verts, mstats)
        st.split += mstats["split"]
        st.pieces += mstats["pieces"]
        st.kept += mstats["kept"]

    out = serialise_p3d(root)
    st.size_out = len(out)
    st.files = 1

    if not opts.dry_run:
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        tmp = dst + ".tmp"
        with open(tmp, "wb") as f:
            f.write(out)
        os.replace(tmp, dst)
    return st


def gather(src, dst):
    """Every file in the tree, not just the .p3d ones.

    The game loads .cho choreography, loose .png, .pag pages and more from the
    same directories, so anything left behind here is a missing-file error at
    runtime. Non-.p3d files are copied through untouched.
    """
    if os.path.isfile(src):
        return [(src, dst)]
    jobs = []
    for base, _, names in os.walk(src):
        for name in names:
            full = os.path.join(base, name)
            jobs.append((full, os.path.join(dst, os.path.relpath(full, src))))
    return sorted(jobs)


def mib(n):
    return f"{n / 1048576:.2f} MB"


def find_pvrtex():
    from shutil import which
    found = which("pvrtex")
    if found:
        return found
    kos = os.environ.get("KOS_BASE")
    if kos:
        guess = os.path.join(kos, "utils", "pvrtex", "pvrtex")
        if os.path.exists(guess):
            return guess
    return "pvrtex"


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="p3dconv",
        description="Repack Pure3D .p3d assets for the Sega Dreamcast.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="examples:\n"
               "  p3dconv.py art/ art-dc/         convert a whole tree\n"
               "  p3dconv.py -n art/              report savings, write nothing\n"
               "  p3dconv.py l1z1.p3d out.p3d     convert a single file\n")
    ap.add_argument("input", help=".p3d file, or a directory to walk")
    ap.add_argument("output", nargs="?", help="destination file or directory")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4,
                    help="parallel conversions (default: %(default)s)")
    ap.add_argument("--no-textures", action="store_true", help="leave IMAGE chunks alone")
    ap.add_argument("--no-anim", action="store_true", help="leave rotation channels alone")
    ap.add_argument("--split-verts", type=int, default=0, metavar="N",
                    help="repack prim groups into meshlets of at most N "
                         "unique vertices (0 disables); 256 is the largest "
                         "that fits the operand cache window")
    ap.add_argument("--no-vq", action="store_true",
                    help="twiddle only, skip VQ compression")
    ap.add_argument("--codebook", default="small", metavar="SIZE",
                    help="VQ codebook: small, full, or 1-256 (default: %(default)s). "
                         "'small' is much smaller for textures under 128x128")
    ap.add_argument("--format", default="AUTO", metavar="FMT",
                    help="pixel format: AUTO, RGB565, ARGB1555, ARGB4444 "
                         "(default: %(default)s)")
    ap.add_argument("--mipmap", action="store_true",
                    help="generate mipmaps (costs about a third more VRAM)")
    ap.add_argument("--dither", type=float, metavar="AMOUNT",
                    help="dither amount, 0 to 1")
    ap.add_argument("--resize", default="none", choices=["none", "near", "up", "down"],
                    help="round invalid sizes to a legal PVR size (default: %(default)s). "
                         "changes UV mapping, so off by default")
    ap.add_argument("--pvrtex", default=None, help="path to the pvrtex binary")
    ap.add_argument("--cache", default=".p3dconv-cache", metavar="DIR",
                    help="conversion cache (default: %(default)s), '' to disable")
    ap.add_argument("-n", "--dry-run", action="store_true",
                    help="report only, write nothing")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("-q", "--quiet", action="store_true")
    opts = ap.parse_args(argv)

    if opts.output is None and not opts.dry_run:
        ap.error("an output path is required unless --dry-run is given")
    if opts.codebook == "full":
        opts.codebook = "256"
    if opts.pvrtex is None:
        opts.pvrtex = find_pvrtex()

    jobs = gather(opts.input, opts.output or opts.input)
    if not jobs:
        print(f"p3dconv: no .p3d files found under {opts.input}", file=sys.stderr)
        return 1

    if not opts.no_textures:
        try:
            subprocess.run([opts.pvrtex, "--version"], capture_output=True, check=False)
        except OSError:
            print(f"p3dconv: cannot run pvrtex at '{opts.pvrtex}'.\n"
                  f"         pass --pvrtex PATH, or use --no-textures.", file=sys.stderr)
            return 1

    encoder = TextureEncoder(opts)
    total = Stats()
    done = 0

    def run(job):
        # One unreadable file must not take the rest of the tree down with it.
        try:
            return convert_file(job[0], job[1], encoder, opts)
        except Exception as exc:
            st = Stats()
            st.failed = 1
            st.note(f"{os.path.relpath(job[0], opts.input)}: {exc}")
            return st

    with ThreadPoolExecutor(max_workers=max(1, opts.jobs)) as pool:
        for st in pool.map(run, jobs):
            total.merge(st)
            done += 1
            if not opts.quiet and sys.stderr.isatty():
                print(f"\r  {done}/{len(jobs)} files", end="", file=sys.stderr)
    if not opts.quiet and sys.stderr.isatty():
        print("\r" + " " * 30 + "\r", end="", file=sys.stderr)

    if opts.quiet:
        return 1 if total.failed or total.tex_failed else 0

    saved = total.bytes_raw16 - total.bytes_dt
    print(f"files      {total.files} converted"
          + (f", {total.copied} copied" if total.copied else "")
          + (f", {total.failed} passed through" if total.failed else ""))
    if not opts.no_textures:
        print(f"textures   {total.tex_done} encoded"
              + (f", {total.tex_resized} resized" if total.tex_resized else "")
              + (f", {total.tex_skipped} skipped" if total.tex_skipped else "")
              + (f", {total.tex_failed} failed" if total.tex_failed else "")
              + (f", {total.tex_already} already .DT" if total.tex_already else ""))
        if total.bytes_raw16:
            pct = 100.0 * total.bytes_dt / total.bytes_raw16
            print(f"  VRAM     {mib(total.bytes_raw16)} at 16bpp"
                  f"  ->  {mib(total.bytes_dt)}  ({pct:.0f}%, saves {mib(saved)})")
    if opts.split_verts:
        print(f"meshlets   {total.split} groups -> {total.pieces} pieces"
              f", {total.kept} left alone")
    if not opts.no_anim:
        print(f"anims      {total.anim_done} rotation channels"
              + (f", {total.anim_skipped} skipped" if total.anim_skipped else ""))
        if total.anim_before:
            print(f"  keys     {mib(total.anim_before)}  ->  {mib(total.anim_after)}")
    print(f"on disc    {mib(total.size_in)}  ->  {mib(total.size_out)}")
    if opts.dry_run:
        print("(dry run, nothing written)")

    if total.notes:
        print("\nnotes:")
        for n in total.notes:
            print(f"  {n}")

    return 1 if total.failed or total.tex_failed else 0


if __name__ == "__main__":
    sys.exit(main())
