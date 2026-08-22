# The Simpsons: Hit & Run — Dreamcast

A work-in-progress port of *The Simpsons: Hit & Run* to the Sega Dreamcast,
based on the Nintendo Switch and PS Vita ports, on [KallistiOS](https://kos-docs.dreamcast.wiki/).

Rendering goes through a PowerVR backend written for this port, with vertex
transform and matrix maths on [sh4zam](https://github.com/gyrovorbis/sh4zam).
There is no SDL or GLdc layer.

You need your own copy of the PC release. No game data is included here, and
none is distributed with the build.

Please report bugs and feature requests in the issues tab of this repository.

## What you need

| | |
|---|---|
| [KallistiOS](https://kos-docs.dreamcast.wiki/) | 2.3.0, with the SH4 toolchain |
| [sh4zam](https://github.com/gyrovorbis/sh4zam) | installed under `kos-ports` |
| `pvrtex` | ships with KallistiOS in `utils/pvrtex` |
| `mkdcdisc` | for baking the CDI |
| Python 3.8+ | for the asset converter |
| A PC install of SHAR | the folder holding `art/`, `scripts/`, `sound/` |

On Windows, everything below runs under WSL.

## Building

Load the KallistiOS environment first — every command assumes it:

```bash
source /opt/toolchains/dc/kos/environ.sh
```

### 1. Convert the game data (once)

```bash
./build.sh assets /path/to/SRR2
```

This reads `art/` from your install, converts textures to PVR formats and
reworks the meshes, then copies `scripts/` and `sound/` across. It takes
roughly 20 minutes and writes about 250 MB into `game-dc/`.

Textures are cached by content in `.p3dconv-cache`, so a second run only
redoes the geometry.

### 2. Build

```bash
./build.sh
```

### 3. Bake a disc image

```bash
./build.sh cdi
```

Writes `SRR2.cdi`, around 700 MB, from the ELF plus `game-dc/`.

### Overrides

| variable | default |
|---|---|
| `SRR2_DATA` | `./game-dc` |
| `SRR2_CDI` | `./SRR2.cdi` |
| `PVRTEX` | `/opt/toolchains/dc/kos/utils/pvrtex/pvrtex` |
| `SPLIT_VERTS` | `256` |

```bash
SRR2_CDI=./test.cdi ./build.sh cdi
```

## The asset converter

`build.sh assets` wraps `tools/p3dconv/p3dconv.py`, which can also be driven
directly:

```bash
python3 tools/p3dconv/p3dconv.py <in-dir> <out-dir> \
    --split-verts 256 --pvrtex /opt/toolchains/dc/kos/utils/pvrtex/pvrtex
```

It rewrites `.p3d` files in place-for-place fashion:

- **Textures** become PVR formats, VQ compressed by default (`--no-vq` to
  disable, `--codebook` to size the codebook).
- **Meshes** are split into pieces of at most `--split-verts` vertices. The
  backend transforms a whole draw into a 256-vertex window in the SH4 operand
  cache, so pieces at or under that size take the fast path; larger ones fall
  back to a slower route. Splitting also tightens each piece's bounding box,
  which culls better.
- **Triangle lists become strips**, and every group gets a run list naming
  the strips inside its index buffer so the backend does not rediscover them
  each frame.
- **Animations** are requantised.

Useful flags: `--jobs N`, `--dry-run`, `--verbose`, `--no-textures`,
`--no-anim`.

## Build options

Pass with `-D` when configuring, then rebuild:

```bash
kos-cmake -DSRR2_DC_DRAW_DIST=90 -S . -B dcbuild
make -C dcbuild -j$(nproc)
```

Changing any of these rewrites the compile definitions and forces a full
rebuild of ~1200 files. Editing source alone does not.

| option | default | what it does |
|---|---|---|
| `SRR2_DC_DRAW_DIST` | 70 | Entity cull distance in `WorldScene`. The main knob for how much geometry reaches the renderer. |
| `SRR2_DC_DEPTH_CULL` | 0 | Per-draw reject by view depth in the backend, on top of the above. 0 is off. |
| `SRR2_DC_MIN_DRAW_AREA` | 6 | Reject draws smaller than this many screen pixels. |
| `SRR2_DC_LIGHT_SCALE` | 100 | Percent scale on diffuse lighting. |
| `SRR2_DC_FOG_RGB` | `0x9AA8B8` | Fog colour, used when `SRR2_DC_DEPTH_CULL` is set. |
| `SRR2_DC_MAX_ZONES` | 3 | Level zones resident at once. |
| `SRR2_DC_ZONE_HIGHWATER_KB` | 11800 | Start evicting zones past this heap use. |
| `SRR2_DC_ZONE_REFUSE_KB` | 12100 | Refuse new zone loads past this heap use. |
| `SRR2_DC_PROFILER` | OFF | Per-frame timing dump over serial. |
| `SRR2_DC_OPB_OP` | 8 | Opaque list object pointer block size, in words. |

## Running

The disc boots on real hardware from GD-EMU, a burned CD-R, or over
`dc-tool`. Flycast works, with two caveats worth knowing when comparing
numbers: it does not implement the operand cache index bit the renderer uses,
so vertex submission takes a different path, and its render timer is stubbed.
Frame times there are not comparable to hardware.

`game-dc/command.txt` holds the game's command line. `skipfe l1 m2` skips the
front end and boots straight into level 1, mission 2. Delete the file to boot
normally.

## License

For educational, documentation and modding purposes. No piracy or commercial
use. Please keep derivative work open and credit accordingly.
