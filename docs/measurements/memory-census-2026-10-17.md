# Memory census on the 2DS, 2026-10-17 (P4.5)

These are the first two censuses on the owner's 2DS (Old 3DS mode, 80 MB, heap arena about 52 MB).
They were taken with `lod3ds_memcensus` / `census-on-load.txt` (`perf/memory-census`, build
`3faa2353b0c0`, on top of `fix/2ds-memory`, debug, `no-audio.txt`). The raw reports are in
[`memory-census-2026-10-17/`](memory-census-2026-10-17/).

| | Title (menus up) | MAP01 just loaded |
|---|---:|---:|
| Heap in use | 40.9 MB | 43.6 MB |
| Heap free | 10.5 MB | 8.7 MB |
| Largest free block | 375 KB | 780 KB |
| `GC::AllocBytes` (`gc=` in `startup.log`) | 9.8 MB | 16.6 MB |
| Engine objects (DObjects, by class size) | 0.08 MB (1075) | 0.80 MB (1731) |
| TArray pool in use / size | 3.4 / 10 MB | 9.5 / 10 MB |
| Map-load reserve (5 MB) | held | released |
| Cached lumps | 1.3 MB (1 lump) | 0.7 MB (1 lump) |
| Chunks of 256 B or less in use | 131,549 (8.9 MB) | 147,229 (10.0 MB) |
| Chunks of 256 B or less free | 51,460 (1.9 MB) | 50,934 (2.0 MB) |
| Free in 16 KB-1 MB pieces | 7.9 MB (198) | 7.1 MB (164) |

## What it says

- **`gc=` is not game objects.** `GC::AllocBytes` counts everything that goes through
  `M_Malloc` / `M_Realloc`: TArrays, strings and the level's geometry arrays. The game's actors
  are small. In MAP01 the 144 green bushes take 164 KB, the spawners 144 KB, and all engine
  objects together 0.8 MB. The P4.8 idea of lighter actors for decorations is worth little. The
  7 MB that `gc=` gains in MAP01 are the level's arrays, which live in the TArray pool, now
  95% full.
- **About 150,000 small blocks are live** (10 MB, most of them at most 256 B; chunk overhead
  alone is about 1.2 MB). They are already there at the title (131,000), so they come from
  startup: the compiled ZScript (symbols, functions, types), names, the string table, the menus.
  This is P4.7's biggest target. Interleaved with them are about 51,000 free crumbs (2 MB) that
  nothing large can use.
- **The free memory is in mid-sized holes.** About 7 MB lies in 160-200 pieces between 16 KB and
  1 MB, spread over the whole heap (the map in the reports has no empty stretch). These holes
  are what large transient blocks (decode buffers, renderer blocks, array growth) leave behind
  when they are freed between long-lived blocks. Any request over ~800 KB in MAP01 fails, and
  over ~375 KB at the title.
- **Two blocks over 4 MB at the title** are the 10 MB TArray pool and the 5 MB map-load reserve.
  Together with the 2 MB renderer reserve, 17 MB is set aside at boot.
- **One cached lump** (1.3 MB at the title, the size of the intro music module) stays cached even
  with audio off.

## Targets this sets

- **P4.6:** a separate arena for large transient blocks. The holes come from those blocks, not
  from the small ones, so it is the direct fix. It should take over the reserves. A size-class
  arena for the small blocks comes second: it saves the chunk overhead and the crumbs.
- **P4.7:** the 131,000 small startup blocks (what they are, by allocation site), the 7.5 MB
  added between `after filesystem init` and `cpu-detection-enter`, the cached music lump.
- **P4.8:** the level's arrays in the pool (9.5 MB for MAP01): line, side and sector structures,
  and what else the pool holds. Not the actors.

## With the large-block arena (P4.6, `perf/large-block-arena`)

Build `9514e8e43726`, same 2DS, `no-audio.txt`. A new game now reaches MAP01 and plays at about
33 fps (developer overlay on). The reports are `arena-title.txt` and `arena-MAP01-walked.txt`
(after a short walk). The owner's own play the same evening ended at a 240 KB PNG decode, with
2.6 MB free and 1.2 MB of linear memory left. That is real exhaustion now, not fragmentation.

| MAP01 after a short walk | |
|---|---:|
| Small heap in use / free | 17.9 / 4.8 MB |
| Large arena in use / free inside (largest) | 26.7 / 1.7 MB (0.27 MB) |
| Gap between the heap's break and the arena | 1.3 MB |
| Linear free | 3.5 MB |

What the arena holds (live blocks by caller, the larger ones):

| Bytes | Blocks | What | Candidate cut |
|---:|---:|---|---|
| 2.75 MB | 24 | `FSoftwareTexture::CreatePixelsBgraWithMipmaps`: software textures in true colour | 8-bit, or no mipmap chain |
| 2.55 MB | 1 | MAP01's sides (`side_t`) | smaller structure (P4.8) |
| 2.15 MB | 17 | `TMap<FName, TableElement>`: the string table, every language | keep the current language and English only |
| 2.08 MB | 52 | `FMemArena` blocks (class data, images, ...) | to split by arena |
| 2.02 MB | 1 | `HexDataSource::ParseDefinition`: the Unicode hex font | load lazily or only the ranges used |
| 1.83 MB | 9 | `DCanvas::Resize`: 400x240x4 canvases | fewer canvases |
| 1.71 MB | 3 | vectors of 32-bit words (SoftPoly buffers?) | to identify |
| 1.06 MB | 1 | `Net3DS_Acquire`: the network buffer (debug FTP, updater) | only while needed |
| 0.89 MB | 2 | `PolyTriangleThreadData`: SoftPoly per-thread data | one thread on the Old 3DS |
| 0.78 + 0.76 + 0.54 MB | | segs, lines, sectors | P4.8 |
| 0.70 MB | 1 | `FZipLump::FillCache`: a lump kept cached | to identify |

Small allocations since boot (counted, not live) are dominated by transient work: zlib inflate
(17.8 MB in 2,500 calls), file reads, FString formatting. The live ones are the compiled ZScript
and the strings. The census cannot tell live small blocks by caller yet.

## First cuts (P4.7 / P4.8, `perf/memory-cuts`)

The five cheapest cuts from the table above, build `60f0c829b6c3`. Reports: `cuts-title.txt` and
`cuts-MAP01-walked.txt`.

| | Title before / after | MAP01 (short walk) before / after |
|---|---:|---:|
| Small heap in use | 13.8 / 9.9 MB | 17.9 / 12.3 MB |
| Large arena in use | 15.9 / 11.5 MB | 26.7 / 19.6 MB |
| Free (small heap + arena + gap) | | 7.9 / 20.6 MB |

- String table: English, Spanish and the default tables only.
- Hex font: Latin and punctuation only. Together with the string table this is most of the
  small-heap saving: 5,000 fewer glyph textures and their names.
- No software precache of the level's textures while the GPU draws the world (-2.75 MB).
- Socket buffer 256 KB on the Old 3DS (-0.75 MB).
- One raster worker on the Old 3DS (-0.45 MB).

What is left in the arena: MAP01's sides 2.5 MB, `FMemArena` blocks 2.1 MB, canvases 1.8 MB,
32-bit vectors 1.7 MB, segs, lines, a cached zip lump 0.7 MB.
