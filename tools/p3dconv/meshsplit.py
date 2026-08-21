"""Split oversized prim groups into spatially compact ones.

The PVR backend rejects and fast-paths whole draw calls using each prim group's
AABB. A road or terrain group spanning a whole street has an AABB that covers
the camera, so it can never be culled and never takes the strip fast path. This
pass cuts such groups along a grid so each piece is small enough to be decided
individually, which also tightens the int16 position quantisation (qScale is
per group).

Everything here is a data-format operation: a mesh already holds N prim groups
and the loader iterates them, so splitting one into several needs no engine or
chunk-format change.
"""

import struct

MESH = 0x00010000
PRIMGROUP = 0x00010002
POSITIONLIST = 0x00010005
NORMALLIST = 0x00010006
UVLIST = 0x00010007
COLOURLIST = 0x00010008
INDEXLIST = 0x0001000A
PACKEDNORMALLIST = 0x00010010
VERTEXSHADER = 0x00010011
TANGENTLIST = 0x00010015
BINORMALLIST = 0x00010016

# Custom: start/length pairs naming the real strips inside the stitched index
# list. Loaders that do not know it skip it; the PVR backend uses it instead of
# rediscovering the strips by hunting for degenerate joins every frame.
DC_RUNLIST = 0x44435253

PRIM_TRIANGLES = 0
PRIM_TRISTRIP = 1

# Per-vertex payloads this pass knows how to remap. Anything else (skin
# weights, matrix palettes) means the group is left alone.
VEC3_LISTS = (POSITIONLIST, NORMALLIST, TANGENTLIST, BINORMALLIST)
DWORD_LISTS = (COLOURLIST, PACKEDNORMALLIST)

# Per-group metadata with no per-vertex content: copied verbatim into each
# piece. VERTEXSHADER is a 1-byte empty pstring the prim group loader ignores.
PASSTHROUGH = (VERTEXSHADER,)


class Unsupported(Exception):
    """The group uses a layout this pass will not touch."""


def _read_pstr(buf, pos):
    n = buf[pos]
    return buf[pos + 1: pos + 1 + n], pos + 1 + n


class PrimGroupHeader:
    """version, shader pstring, primType, vertexFormat, vertexCount,
    indexCount, matrixCount -- see tPrimGroupLoader::ParseHeader."""

    def __init__(self, data):
        self.version, = struct.unpack_from("<I", data, 0)
        self.shader, pos = _read_pstr(data, 4)
        (self.prim_type, self.vertex_format, self.vertex_count,
         self.index_count, self.matrix_count) = struct.unpack_from("<IIIII", data, pos)
        self.tail = data[pos + 20:]

    def pack(self, vertex_count, index_count, prim_type):
        return (struct.pack("<I", self.version)
                + bytes([len(self.shader)]) + self.shader
                + struct.pack("<IIIII", prim_type, self.vertex_format,
                              vertex_count, index_count, self.matrix_count)
                + self.tail)


def _decode_lists(group, header):
    """Pull the per-vertex arrays and the index list out of a prim group."""
    verts = {}
    indices = None
    extra = []

    for c in group.children:
        if c.id in VEC3_LISTS:
            count, = struct.unpack_from("<I", c.data, 0)
            verts[c.id] = ("vec3", None,
                           [struct.unpack_from("<fff", c.data, 4 + i * 12) for i in range(count)])
        elif c.id in DWORD_LISTS:
            count, = struct.unpack_from("<I", c.data, 0)
            verts[c.id] = ("dword", None,
                           [struct.unpack_from("<I", c.data, 4 + i * 4)[0] for i in range(count)])
        elif c.id == UVLIST:
            count, channel = struct.unpack_from("<II", c.data, 0)
            verts.setdefault(UVLIST, [])
            verts[UVLIST] = verts[UVLIST] if isinstance(verts[UVLIST], list) else []
            verts[UVLIST].append(("uv", channel,
                                  [struct.unpack_from("<ff", c.data, 8 + i * 8) for i in range(count)]))
        elif c.id == INDEXLIST:
            count, = struct.unpack_from("<I", c.data, 0)
            indices = list(struct.unpack_from("<%dI" % count, c.data, 4))
        elif c.id in PASSTHROUGH:
            extra.append(c)
        else:
            raise Unsupported("chunk %08x" % c.id)

    if indices is None:
        raise Unsupported("no index list")
    if POSITIONLIST not in verts:
        raise Unsupported("no position list")
    return verts, indices, extra


def _encode_lists(Chunk, verts, indices, keep):
    """Rebuild the per-vertex chunks for the subset of vertices in `keep`."""
    out = []
    for cid in (POSITIONLIST, NORMALLIST, TANGENTLIST, BINORMALLIST):
        if cid in verts:
            _, _, src = verts[cid]
            body = struct.pack("<I", len(keep))
            body += b"".join(struct.pack("<fff", *src[v]) for v in keep)
            out.append(Chunk(cid, body))
    for cid in (COLOURLIST, PACKEDNORMALLIST):
        if cid in verts:
            _, _, src = verts[cid]
            body = struct.pack("<I", len(keep))
            body += b"".join(struct.pack("<I", src[v]) for v in keep)
            out.append(Chunk(cid, body))
    if UVLIST in verts:
        for _, channel, src in verts[UVLIST]:
            body = struct.pack("<II", len(keep), channel)
            body += b"".join(struct.pack("<ff", *src[v]) for v in keep)
            out.append(Chunk(UVLIST, body))
    out.append(Chunk(INDEXLIST,
                     struct.pack("<I", len(indices))
                     + struct.pack("<%dI" % len(indices), *indices)))
    return out


def _strip_to_triangles(indices):
    """Expand a tristrip into triangles, dropping degenerates and keeping the
    alternating winding the strip encodes."""
    tris = []
    for i in range(len(indices) - 2):
        a, b, c = indices[i], indices[i + 1], indices[i + 2]
        if a == b or b == c or a == c:
            continue
        tris.append((a, c, b) if (i & 1) else (a, b, c))
    return tris


def _triangles_to_strips(tris):
    """Greedy strip builder: extend a strip while a neighbouring triangle
    shares the trailing edge, otherwise start a new one."""
    remaining = list(tris)
    by_edge = {}
    for idx, (a, b, c) in enumerate(remaining):
        for e in ((a, b), (b, c), (c, a)):
            by_edge.setdefault(frozenset(e), []).append(idx)

    used = [False] * len(remaining)
    strips = []

    for seed in range(len(remaining)):
        if used[seed]:
            continue
        used[seed] = True
        a, b, c = remaining[seed]
        strip = [a, b, c]

        while True:
            tail = frozenset((strip[-2], strip[-1]))
            nxt = None
            for cand in by_edge.get(tail, ()):
                if not used[cand]:
                    nxt = cand
                    break
            if nxt is None:
                break
            used[nxt] = True
            tri = remaining[nxt]
            apex = [v for v in tri if v not in (strip[-2], strip[-1])]
            if len(apex) != 1:
                break
            strip.append(apex[0])
        strips.append(strip)

    return strips


def _stitch(strips):
    """Join strips with degenerate triangles so a cell stays one draw call."""
    return _stitch_runs(strips)[0]


def _stitch_runs(strips):
    """As _stitch, but also report where each strip landed, as (first, count).

    The joins are what the backend would otherwise have to detect at runtime,
    once per frame, forever.
    """
    out = []
    runs = []
    for s in strips:
        if out:
            out.append(out[-1])
            out.append(s[0])
            if len(out) % 2:
                out.append(s[0])
        runs.append((len(out), len(s)))
        out.extend(s)
    return out, runs


def _strip_runs(indices):
    """Split a stitched index list back into its component strips, dropping the
    degenerate joins. Returns a list of index runs, each a real strip."""
    runs = []
    n = len(indices)
    i = 0
    while i + 2 < n:
        a, b, c = indices[i], indices[i + 1], indices[i + 2]
        if a == b or b == c or a == c:
            i += 1
            continue
        j = i
        while j + 2 < n:
            x, y, z = indices[j], indices[j + 1], indices[j + 2]
            if x == y or y == z or x == z:
                break
            j += 1
        run = list(indices[i:j + 2])
        if i & 1:
            run.insert(0, run[0])       # keep the winding the source had
        runs.append(run)
        i = j
    return runs


def _pack_meshlets(runs, max_verts):
    """dca3's grouping: repeatedly take the strip that adds the fewest new
    vertices, so a meshlet stays spatially coherent and its vertex set fits an
    8-bit local index space."""
    remaining = list(runs)
    out = []

    while remaining:
        verts = set()
        chosen = []

        while True:
            best = None
            best_new = None
            best_shared = -1

            for r in remaining:
                new = {v for v in r if v not in verts}
                if len(verts) + len(new) > max_verts:
                    continue
                shared = len(r) - len(new)
                if not new:
                    best, best_new, best_shared = r, new, shared
                    break
                if shared > best_shared:
                    best, best_new, best_shared = r, new, shared

            if best is None:
                break

            chosen.append(best)
            verts |= best_new
            remaining.remove(best)

        if not chosen:
            # a single strip wider than max_verts: give it its own meshlet
            chosen = [remaining.pop(0)]
            verts = set(chosen[0])

        out.append((chosen, verts))

    return out



def _scan_runs(indices):
    """Maximal degenerate-free spans of an existing index list, as
    (first, count). Mirrors the walk the backend would otherwise do at runtime.
    """
    runs = []
    n = len(indices)
    i = 0
    while i + 2 < n:
        a, b, c = indices[i], indices[i + 1], indices[i + 2]
        if a == b or b == c or a == c:
            i += 1
            continue
        j = i
        while j + 2 < n:
            x, y, z = indices[j], indices[j + 1], indices[j + 2]
            if x == y or y == z or x == z:
                break
            j += 1
        runs.append((i, (j + 2) - i))
        i = j
    return runs


def _runs_chunk(Chunk, runs):
    runs = [(a, n) for a, n in runs if n >= 3]
    if not runs:
        return None
    body = struct.pack("<I", len(runs))
    for first, count in runs:
        body += struct.pack("<II", first, count)
    return Chunk(DC_RUNLIST, body)


def annotate_group(Chunk, group):
    """Attach a run list to a group that is being kept as-is. Returns a
    replacement group, or None to leave it alone."""
    header = PrimGroupHeader(group.data)
    if header.prim_type != PRIM_TRISTRIP:
        return None
    for c in group.children:
        if c.id == DC_RUNLIST:
            return None

    indices = None
    for c in group.children:
        if c.id == INDEXLIST:
            count, = struct.unpack_from("<I", c.data, 0)
            indices = list(struct.unpack_from("<%dI" % count, c.data, 4))
    if not indices:
        return None

    chunk = _runs_chunk(Chunk, _scan_runs(indices))
    if chunk is None:
        return None

    kids = [Chunk(c.id, c.data, list(c.children)) for c in group.children]
    kids.append(chunk)
    return Chunk(PRIMGROUP, group.data, kids)


def split_group(Chunk, group, max_verts):
    """Return a list of replacement prim groups, or None to keep as-is."""
    header = PrimGroupHeader(group.data)
    if header.prim_type not in (PRIM_TRIANGLES, PRIM_TRISTRIP):
        return None
    if header.matrix_count:
        return None

    verts, indices, extra = _decode_lists(group, header)

    if header.prim_type == PRIM_TRISTRIP:
        tris = _strip_to_triangles(indices)
    else:
        tris = [tuple(indices[i:i + 3]) for i in range(0, len(indices) - 2, 3)]

    if header.vertex_count <= max_verts:
        return None

    runs = _strip_runs(indices) if header.prim_type == PRIM_TRISTRIP else None

    if runs is None:
        # triangle lists: each triangle is its own run
        runs = [list(t) for t in tris]

    packs = _pack_meshlets(runs, max_verts)
    if len(packs) < 2:
        return None

    out = []
    for chosen, _ in packs:
        keep = []
        remap = {}
        for r in chosen:
            for v in r:
                if v not in remap:
                    remap[v] = len(keep)
                    keep.append(v)

        local = [[remap[v] for v in r] for r in chosen]

        runs = None
        if header.prim_type == PRIM_TRISTRIP:
            new_indices, runs = _stitch_runs(local)
            prim = PRIM_TRISTRIP
        else:
            new_indices = [v for r in local for v in r]
            prim = PRIM_TRIANGLES

        if len(new_indices) < 3:
            continue

        children = _encode_lists(Chunk, verts, new_indices, keep)

        if runs:
            chunk = _runs_chunk(Chunk, runs)
            if chunk is not None:
                children.append(chunk)

        out.append(Chunk(PRIMGROUP,
                         header.pack(len(keep), len(new_indices), prim),
                         children
                         + [Chunk(e.id, e.data, list(e.children)) for e in extra]))

    return out if len(out) > 1 else None


def split_mesh(Chunk, mesh, max_verts, stats):
    """Rewrite one MESH chunk in place. Returns True if anything changed."""
    name, pos = _read_pstr(mesh.data, 0)
    version, n_prim = struct.unpack_from("<II", mesh.data, pos)

    rebuilt = []
    changed = False

    for c in mesh.children:
        if c.id != PRIMGROUP:
            rebuilt.append(c)
            continue
        try:
            pieces = split_group(Chunk, c, max_verts)
        except Unsupported:
            pieces = None
        except Exception:
            pieces = None
        if pieces:
            rebuilt.extend(pieces)
            stats["split"] += 1
            stats["pieces"] += len(pieces)
            changed = True
        else:
            annotated = annotate_group(Chunk, c)
            if annotated is not None:
                rebuilt.append(annotated)
                stats["annotated"] += 1
                changed = True
            else:
                rebuilt.append(c)
            stats["kept"] += 1

    if not changed:
        return False

    mesh.children = rebuilt
    count = sum(1 for c in rebuilt if c.id == PRIMGROUP)
    mesh.data = (mesh.data[:pos] + struct.pack("<II", version, count)
                 + mesh.data[pos + 8:])
    return True


def split_file(Chunk, root, max_verts, stats):
    touched = False
    for chunk in root.walk():
        if chunk.id == MESH:
            if split_mesh(Chunk, chunk, max_verts, stats):
                touched = True
    return touched
