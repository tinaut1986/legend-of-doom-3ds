# GPU world renderer: which path (P2.2)

Draft, 2026-10-13: measured, the decision is the owner's. Frame numbers:
[`frame-budget.md`](frame-budget.md).

## The constraint

The 3D view costs 20-23 ms at 400x240 today, all on the main thread: ~15-16 ms of rasterizing
that a GPU removes, and 4.6-7 ms of scene work (BSP, clipping, setup) that stays in some form.
With the rest of the frame (logic, present, bottom screen, HUD), **the world's CPU side has
~9.5 ms on MAP01 for 60 fps on the New 3DS** (~13 ms in a dungeon), on the main thread. Stereo
(P3) doubles any per-eye cost; the Old 3DS has a third of the clock and no core 2.

So the deciding number is not GPU speed (the PICA200 has room for Doom's triangle counts) but
**how much CPU the path spends per frame to feed it**, and whether the scene walk can be shared
by both eyes.

## What the PICA200 can and cannot do (for Doom)

- Formats: RGBA8/RGB8/5551/565/RGBA4, L8/A8/LA8/LA4/L4/A4, ETC1. **No paletted textures, and
  no dependent texture reads**: a colormap lookup on the GPU is impossible. The look is true
  colour multiplied by light, as `vid_rendermode 1` (what ships) already is.
- No fragment shaders: 6 TEV combiner stages, a fog unit with a 128-entry depth LUT, fragment
  lighting with LUTs. GZDoom's software light (`R_DoomLightingEquation`, `main.fp:273`) depends,
  for a given light level, only on depth: **one fog LUT per light level** (fog colour black, or
  the sector's fade colour) reproduces the light diminishing, with stepped entries for the
  bands. MAP01 has 8 distinct light levels, MAP10 4, so draws grouped by light level need few
  LUT switches. Fullbright = fog off; brightmaps = a second texture and one TEV stage;
  desaturation = DOT3 + interpolate.
- Textures: power of two, 8-1024 per side, Morton-tiled, sampled from linear RAM (the CPU
  cannot write VRAM; upstream's NovaGL work and U2 both hit it). 173 of the 194 wall textures
  are power of two; the rest need padding or splitting for REPEAT.
- No user clip planes beyond one, and no reliable clipping across w=0: near clipping on the
  CPU or a guard band (upstream's NovaGL patches had to clip on the CPU).
- Stereo: two render targets, one projection each; geometry submitted twice.

## Options

**(a) Upstream's NovaGL route** (GZDoom's GLES renderer over a GLES 1.1 emulation on citro3d).
Upstream never got one complete correct frame on hardware (v0.7-v0.11: GX queue stalls, cache
coherency, clipping; v0.12 dropped it). ~2,450 draws for one MAP01 frame, each through a GLSL
emulation layer. Its speed was never measured. **Rejected**: all of (b)'s CPU cost plus an
emulation layer, and none of its problems are solved.

**(b) A citro3d backend for GZDoom's hwrenderer.** The engine already ships a CPU-raster
hwrenderer backend, `PolyFrameBuffer` (~2.1k lines + drawers, no GLSL, its "shader" written in
C++): the template for a PICA200 one (`DFrameBuffer`, ~20 pure virtuals of `FRenderState`,
buffers, `IHardwareTexture`), reusing `gpu_3ds.cpp`'s context and texture copies from U2.
Everything the engine draws comes for free (portals, 3D floors, decals, translucency sorting,
sky). Cost: the hwrenderer's CPU side, which walks the BSP and rebuilds every visible wall's
vertices each frame and issues one draw per wall and per sprite; stereo runs the whole scene
walk once per eye unless patched. **Measured below: ~20 ms a frame on MAP01.**

**(c) A Doom-specific renderer of our own.** Static wall and flat geometry built at map load,
only movers rebuilt, our own visibility, batched by texture and light level; one scene walk for
both eyes. The lowest CPU cost and the only one likely to fit the Old 3DS, but portals, sky,
translucency, sprite clipping, 3D floors (MAP01 uses them), polyobjects and every render style
become our work, and parity with what the engine draws is ours to prove.

**(d) Interim, no GPU:** the swrenderer's own column-slice threading (`r_scene_multithreaded`)
with its workers pinned to CPU2. Today the worker is a `std::thread`, which libctru puts on CPU0
at priority 0x3F, so it measured slower (25.2 ms against 23.0). Pinned to CPU2 it could take up
to half of the ~16 ms raster off the main core. New 3DS only, mono only; it does not replace P2.

## Measured: the hwrenderer's CPU side (2026-10-13)

GZDoom's `bench`, New 3DS, the hwrenderer on the poly backend with every draw dropped (the
probe below), so only its CPU side runs. Raw output: [`measurements/hwrender-probe-2026-10-13.txt`](measurements/hwrender-probe-2026-10-13.txt).
"3D view" is the recorder's `world_ms` (mean of 8 s); the rest is `bench` for one frame.

| Spot | walls / flats / sprites | BSP | clip | walls setup + submit | flats | sprites | `bench` All | 3D view |
|---|---|---|---|---|---|---|---|---|
| MAP01 start, no distance cull | 2580 / 129 / 195 | 11.6 | 36.5 | 20.4 + 29.0 | 3.9 | 4.9 | 92.9 | 66.8 |
| MAP01 start, culled at the swrenderer's fog distance (1536) | 106 / 127 / 195 | 11.3 | 10.7 | 0.9 + 1.6 | 3.7 | 4.6 | 30.6 | 20.3 |
| MAP02 start (dungeon) | 88 / 38 / 1 | 0.5 | 1.3 | 0.7 + 1.5 | 0.7 | 0.1 | 4.4 | 3.1 |

Against the swrenderer's own scene work (its fixed part, `frame-budget.md`): **7.0 ms on MAP01,
4.8 on MAP02**, and 23 / 20 ms for the whole 3D view *including* every pixel at 400x240.

- MAP01 is the case that matters (the overworld is most of the game). Even culled like the
  swrenderer, the hwrenderer spends ~20 ms of CPU a frame **before a single triangle is drawn**:
  three times the swrenderer's walk, twice the ~9.5 ms that 60 fps leaves, and it would be paid
  per eye in stereo. Its BSP walk (11 ms with 195 sprites) and its angle clipper (11 ms) are the
  cost; the walls themselves are cheap once culled.
- Upstream's distance fog is what keeps MAP01 affordable: without it the hwrenderer sees 2580
  walls (67 ms). Any GPU path must keep a distance limit on the overworld.
- In a dungeon both are cheap (3-5 ms).

## Proposal (for the owner's decision)

**(b) is out**: optimising the hwrenderer's walk from 20 ms to under ~8 ms (and to half that for
stereo, and a third for the Old 3DS) is a rewrite of its BSP and clipper in all but name.

**(c), built on what is already fast: the swrenderer's own visibility.** Its opaque pass already
walks the BSP front to back with Doom's 1D screen-space occlusion and the overworld distance cull,
in ~7 ms *including* its wall and plane setup for the CPU drawers. The GPU renderer would:

1. Keep that walk (`RenderOpaquePass`), but instead of setting up column drawing record what it
   found visible: segs (wall pieces), subsectors (floor and ceiling), sprites. The depth buffer
   does the per-pixel occlusion, so the swrenderer's column clipping is only used to cull.
2. Build each map's geometry once at load, in linear RAM: wall quads per side and part
   (upper/middle/lower), flats triangulated per subsector (GZDoom's `hw_vertexbuilder` already
   does flats), Morton-tiled textures (U2's texture copies). Rebuild only what moves (doors,
   lifts) when it moves.
3. Submit per frame only index ranges, **batched by texture and light level** (one fog LUT per
   light level for the software light falloff; MAP01 has 8 light levels), sprites as quads, sky as
   its own pass. A few dozen draw calls instead of thousands.
4. Stereo (P3): one walk, two submissions with per-eye projections (the visible set of the
   centre eye, widened by the eye offset).

Expected CPU: the walk without its column setup (under the swrenderer's 7 ms on MAP01) plus
submission (~1-2 ms batched); the ~16 ms of rasterizing goes to the PICA200. That fits 60 fps
on the New 3DS (~9.5 ms budget) and is the only option with a chance on the Old 3DS.
What it costs us: parity is ours to prove (light bands, fades, translucency order, 3D floors on
MAP01, mid-textures, sprite clipping against floors, the sky), checked against the swrenderer as
in TMC's GPU CHECK (P2.3).

**(d)** stays a possible interim (CPU2 rasterizing half the screen for the swrenderer), not a
substitute: no help for the Old 3DS or stereo.

First step if (c) is chosen: a spike that draws MAP01's walls and flats from load-time buffers
with the swrenderer's visible set, no lighting, timed with the recorder: it validates the CPU
estimate before any parity work.

## Spike result (2026-10-13, branch `feat/gpu-world-spike`)

`gpu_world_3ds.cpp`: right after the swrenderer's opaque pass, its visible subsectors
(`PvsSubsectors`, front to back) become wall quads and flat fans, batched by texture, drawn by
the PICA200 with a depth buffer in place of the canvas (`gpu_3ds.cpp`, `DrawWorld`; second
shader `gpu_world_3ds.v.pica`). The projection is the swrenderer's own (`CenterX`, `CenterY`,
`InvZtoScale`, the focal tangent), so the GPU image lines up with the CPU one. With it on, the
swrenderer still walks and clips but draws no wall columns, planes, sky, translucent pass or
weapon (`I_3DSGpuWorldActive`). Built per frame from the visible set (no load-time buffers yet).
Debug builds: `lod3ds_gpu_world 1`, numbers in `lod3ds_gpu_world_stats`.

New 3DS, 400x240 (render scale 1.0), standing at each map's start, recorder means:

| Spot | world | fps | 3D view | capture (CPU) | draws / vertices | GPU frame |
|---|---|---|---|---|---|---|
| MAP01 | CPU (swrenderer) | 27.8 | 22.9 ms | | | |
| MAP01 | **GPU** | **60.3** | **6.7 ms** | 0.7 ms | 6 / 1257 | 10.2 ms |
| MAP02 | CPU | 38.2 | 19.8 ms | | | |
| MAP02 | **GPU** | **105** | **4.3 ms** | 0.9 ms | 6 / 2016 | 8.4 ms |

"3D view" with the GPU is the swrenderer's walk (with its wall setup and clipping, not yet
trimmed) plus the capture. MAP01 reaches the LCD's 60 Hz with the walk untrimmed. The GPU's own
time (citro3d's drawing time for the whole frame, 2D and bottom screen included) is well inside
a frame. The estimate of the proposal holds.

Missing before parity (P2.3): light (sector level and the software falloff, fog LUTs), sky,
sprites and the weapon, masked mid-textures, 3D floors (MAP01), portals, slopes' texture
alignment, flat offsets/rotation/scale, texture animation and wall scrolling (they come from
the engine's texture lookups each frame, untested), and the swrenderer's walk trimmed to
visibility only. Near-plane clipping by the PICA200 has not shown problems standing; to watch
when walking into walls.

## The probe

Debug builds take a one-shot probe (`i_main.cpp`): **`hwrender-probe.txt`** in
`/3ds/Legend of Doom 3DS/` starts the next boot with `vid_rendermode 4` (the hwrenderer on the
poly backend) with every draw dropped (`I_3DSHwRenderProbeNoDraw`, `poly_renderstate.cpp`), and
on MAP01 culls the hwrenderer's BSP at the swrenderer's fog distance (`hw_bsp.cpp`). The game
deletes the marker before using it, so a boot that hangs is not repeated. In a level,
`lod-link.py console bench` appends the hwrenderer's clocks and counts to `/benchmarks.txt` on
the SD card. The 3D view stays black and the bottom map flickers meanwhile: measurement only.

The poly backend cannot draw the hwrenderer's scene on the 3DS: switching `vid_rendermode` to 4
at run time, and booting in it, both crash entering MAP01 with the same data abort in a poly
drawer thread, `PolyMainVertexShader::main()` (`poly_vertex_shader.h:59`, from
`PolyVertexInputAssembly::Load`), reading a vertex at `far=096c8bbb` (Luma dumps kept in
`build-3ds/luma-dumps-2026-10-13/`, `rendermode4-*` and `probe-boot-*`). Not investigated: no
option above uses the poly drawers.
