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
    if not strips:
        return []
    out = list(strips[0])
    for s in strips[1:]:
        out.append(out[-1])
        out.append(s[0])
        if len(out) % 2:
            out.append(s[0])
        out.extend(s)
    return out


def _cell_of(centroid, lo, inv_cell, dims):
    idx = 0
    mul = 1
    for a in range(3):
        c = int((centroid[a] - lo[a]) * inv_cell[a])
        if c < 0:
            c = 0
        elif c >= dims[a]:
            c = dims[a] - 1
        idx += c * mul
        mul *= dims[a]
    return idx


def split_group(Chunk, group, target_tris):
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

    if len(tris) <= target_tris:
        return None

    _, _, pos = verts[POSITIONLIST]
    lo = [min(p[a] for p in pos) for a in range(3)]
    hi = [max(p[a] for p in pos) for a in range(3)]

    # Choose a grid that lands near target_tris per cell, biased to the longest
    # axes so cells stay roughly cubical rather than slab-shaped.
    want = max(2, (len(tris) + target_tris - 1) // target_tris)
    extent = [max(hi[a] - lo[a], 1e-6) for a in range(3)]
    order = sorted(range(3), key=lambda a: -extent[a])
    dims = [1, 1, 1]
    while dims[0] * dims[1] * dims[2] < want:
        best = max(order, key=lambda a: extent[a] / dims[a])
        dims[best] += 1

    inv_cell = [dims[a] / extent[a] for a in range(3)]

    buckets = {}
    for tri in tris:
        cx = tuple(sum(pos[v][a] for v in tri) / 3.0 for a in range(3))
        buckets.setdefault(_cell_of(cx, lo, inv_cell, dims), []).append(tri)

    if len(buckets) < 2:
        return None

    out = []
    for cell in sorted(buckets):
        cell_tris = buckets[cell]

        keep = []
        remap = {}
        for tri in cell_tris:
            for v in tri:
                if v not in remap:
                    remap[v] = len(keep)
                    keep.append(v)

        local = [tuple(remap[v] for v in tri) for tri in cell_tris]

        if header.prim_type == PRIM_TRISTRIP:
            new_indices = _stitch(_triangles_to_strips(local))
            prim = PRIM_TRISTRIP
        else:
            new_indices = [v for tri in local for v in tri]
            prim = PRIM_TRIANGLES

        if not new_indices:
            continue

        out.append(Chunk(PRIMGROUP,
                         header.pack(len(keep), len(new_indices), prim),
                         _encode_lists(Chunk, verts, new_indices, keep)
                         + [Chunk(e.id, e.data, list(e.children)) for e in extra]))

    return out if len(out) > 1 else None


def split_mesh(Chunk, mesh, target_tris, stats):
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
            pieces = split_group(Chunk, c, target_tris)
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
            rebuilt.append(c)
            stats["kept"] += 1

    if not changed:
        return False

    mesh.children = rebuilt
    count = sum(1 for c in rebuilt if c.id == PRIMGROUP)
    mesh.data = (mesh.data[:pos] + struct.pack("<II", version, count)
                 + mesh.data[pos + 8:])
    return True


def split_file(Chunk, root, target_tris, stats):
    touched = False
    for chunk in root.walk():
        if chunk.id == MESH:
            if split_mesh(Chunk, chunk, target_tris, stats):
                touched = True
    return touched
