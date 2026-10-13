# Where the frame goes (P2.1)

Measured 2026-10-13 on the owner's New 3DS with the P1.3 recorder (`lod-link.py perf 8`),
build `release/v0.1.1` + `fix/install-relaunch` (`hardware-hybrid`, debug), standing still
where `map MAPxx` drops the player. Milliseconds per frame, mean of 200-670 frames.

## How a gameplay frame is made today

Not SoftPoly, and not on two cores (PLAN's older wording said so):

- The 3D view is GZDoom's **classic true-colour software renderer** (`src/rendering/swrenderer`,
  `vid_rendermode 1`), drawing straight into the BGRA canvas of `PolyFrameBuffer`
  (`r_swscene.cpp:134`). `r_scene_multithreaded 0` (`i_main.cpp:381`): BSP traversal, clipping,
  visplanes, sprites and every column and span are drawn **on the main thread**, inside
  `world_ms` (`d_main.cpp:1199`).
- The poly drawer threads (`r_multithreaded 2`, CPU0 + CPU2, `r_thread.cpp:186`) only run
  `PolyCommandBuffer` work: the software 2D fallback, the colormap quad, models. In gameplay
  they idle (`raster_wait_ms` ~0.01). **CPU2 does almost nothing.**
- Game logic (35 Hz tics) runs on the same main thread (`logic_ms`, `d_net.cpp:1990`).
- `present_ms` (`gpu_3ds.cpp:942`): CPU swizzle of the canvas into a linear buffer, cache flush,
  `C3D_FrameBegin`, display transfer into a 512x256 texture, one quad to 400x240, the GPU 2D
  of U2, the bottom screen.

## Numbers

Render scale = `lod3ds_render_scale` (5 = 50 %, 200x120; 7 = 70 %, 280x168; 10 = 100 %, 400x240).

| Spot | scale | fps | logic | 3D view | present | bottom | other |
|---|---|---|---|---|---|---|---|
| MAP01 (overworld) start | 50 % | 49.7 | 3.87 | 11.01 | 1.77 | 1.21 | 1.98 |
| MAP01 start | 70 % | 38.7 | 5.34 | 15.14 | 2.11 | 1.64 | 1.29 |
| MAP01 start | 100 % | 27.6 | 7.03 | 23.07 | 2.88 | 1.95 | 1.06 |
| MAP02 (dungeon) start | 50 % | 81.3 | 0.50 | 8.52 | 1.63 | 0.35 | 1.09 |
| MAP02 start | 100 % | 38.2 | 0.95 | 19.80 | 2.82 | 0.90 | 1.46 |
| MAP06 (dungeon) start | 50 % | 81.3 | 0.51 | 8.49 | 1.63 | 0.33 | 1.11 |
| MAP06 start | 100 % | 37.7 | 1.02 | 20.14 | 2.85 | 0.82 | 1.50 |

2D, compose, raster wait and limit are all under 0.3 ms and left out.

### The 3D view: fixed part and per-pixel part

The 3D view time fits a straight line in the number of pixels (three points on MAP01):

| Spot | fixed (BSP, clipping, setup) | raster at 400x240 | check at 70 % |
|---|---|---|---|
| MAP01 | **7.0 ms** | 16.1 ms | 7.0 + 0.49 x 16.1 = 14.9 (measured 15.1) |
| MAP02 | **4.8 ms** | 15.0 ms | |
| MAP06 | **4.6 ms** | 15.5 ms | |

So a GPU that draws every pixel for free removes ~15-16 ms at 400x240, and the swrenderer's
own CPU side (5-7 ms) stays unless the new renderer's scene walk is cheaper.

### Game logic

Per tic, not per frame: logic_ms x fps / 35.

| Spot | logic per tic | at 60 fps (0.58 tics a frame) | at 30 fps (1.17) |
|---|---|---|---|
| MAP01 | **5.5 ms** (5.49 / 5.90 / 5.53 at the three scales) | 3.2 ms | 6.4 ms |
| MAP02, MAP06 | 1.0-1.2 ms | 0.6 ms | 1.2 ms |

The overworld's logic is five times a dungeon's (its ~5k lines and many actors, ZScript).
Logic cannot leave the main thread (GZDoom's play code is single-threaded).

## What stays on the CPU whatever the renderer

At 400x240 on MAP01, with the world drawn by the GPU:

| Part | ms a frame at 60 fps |
|---|---|
| Logic | 3.2 |
| Present without the canvas swizzle (present at 50 % minus its per-pixel part: ~1.4) | ~1.4 |
| Bottom screen | 1.2-1.9 |
| Other (status bar, HUD, input) | ~1.0 |
| **Total** | **~7 ms** |

**Budget left for the world's CPU side** (scene walk, geometry, submission to the PICA200),
main thread only:

| Target | frame | left on MAP01 | left in a dungeon |
|---|---|---|---|
| 60 fps | 16.7 ms | **~9.5 ms** | ~13 ms |
| 30 fps | 33.3 ms | ~23 ms | ~27 ms |

The swrenderer's fixed part (7.0 / 4.8 ms) fits in that, so a GPU renderer whose CPU side
costs no more than it does reaches 60 fps on the New 3DS with ~2.5 ms to spare on MAP01.
GZDoom's hwrenderer does not: its CPU side alone is ~20 ms on MAP01
([`gpu-renderer-design.md`](gpu-renderer-design.md)).

Stereo (P3) doubles whatever the world's CPU side costs per eye unless the scene walk is
shared between the eyes.

## Tried

- **`r_scene_multithreaded 2`** (the swrenderer's own split of the screen into column slices
  over threads), MAP01, 100 %: 3D view 25.2 ms against 23.0 with one thread. The second
  thread is a `std::thread`, which libctru creates on CPU0 at the lowest priority, 0x3F
  (`threadCreate(func, arg, size, 0x3F, 0, false)` in libctru's `syscalls.c`,
  `thread_create`): both slices ran on CPU0, the second one behind the main thread. Pinning the slice workers to CPU2 is untried and could take half
  of the 16 ms raster off the main core without any GPU work (an interim step, not a
  substitute for P2: it does not help the Old 3DS, which has no core 2 for applications).
- **`vid_rendermode 4`** (the hwrenderer through the poly backend): its poly drawers crash on
  the 3DS; its CPU side was measured with the draws dropped, see P2.2's note.

## Not yet measured

- A busy overworld screen (enemies around) and a boss; moving rather than standing.
- `hardware-safe`, the Old 3DS.
