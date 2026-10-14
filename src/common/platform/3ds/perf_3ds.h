#pragma once
// Frame-time recorder (PLAN P1.3), from ../zelda-tmc-3ds's debug_perf_3ds. One row per presented
// frame into a RAM buffer while it runs; nothing touches the SD card until it stops, when the
// rows are written as debug/perf-NNNN.csv (platform/3ds/tools/perf-analyze.py summarises it).
//
// Console command (debug builds): lod3ds_perf start | stop | status. From the PC:
// platform/3ds/tools/lod-link.py perf SECONDS records, fetches and summarises in one go.
//
// Each part of the frame is timed where the engine does it, with an F3DSPerfScope; whatever is
// not covered ends up in the row's other_ms. While the recorder is off a scope costs one load.
#include <cstdint>

enum class E3DSPerfPart
{
	Logic,      // the game tics run this frame (C_Ticker, M_Ticker, G_Ticker)
	World,      // the 3D view: BSP, visibility, SoftPoly command generation (D_Render)
	RasterWait, // waiting for the SoftPoly raster threads to finish the frame
	TwoD,       // HUD, menus and other 2D drawn by the engine (PolyFrameBuffer::Draw2D)
	Compose,    // the 3DS compositor: FPS, crosshair, native menu routing to the bottom LCD
	Present,    // upload of the canvas and the PICA200 scale to 400x240
	Limit,      // the frame limiter (vid_maxfps)
	Bottom,     // the bottom-screen interface (map, items, overlay)
	Input,      // I_StartTic: input, the console link and FTP server, updater, settings save
	Sound,      // positional sounds and music (S_UpdateSounds, S_UpdateMusic)
	FrameStart, // texture animations, the sky, PolyFrameBuffer::BeginFrame (the canvas clear)
	Hud,        // the view border, the status bar / HUD, messages (engine 2D recording)
	Count
};

extern bool I3DSPerfRunning;

void I_3DSPerfAdd(E3DSPerfPart part, uint64_t ticks);
// For a part that does not fit a scope: Now() at its start (0 while off), Since() at its end.
uint64_t I_3DSPerfNow();
void I_3DSPerfSince(E3DSPerfPart part, uint64_t start);
// Once per game tic, for the tics-per-frame column.
void I_3DSPerfTic();
// Once per presented frame, at the end of PolyFrameBuffer::Update: closes the row.
void I_3DSPerfFrameDone(int canvasWidth, int canvasHeight);

bool I_3DSPerfStart();
// Writes the CSV and frees the buffer. Returns the file number, or -1.
int I_3DSPerfStop();

struct F3DSPerfScope
{
	explicit F3DSPerfScope(E3DSPerfPart part);
	~F3DSPerfScope();
	F3DSPerfScope(const F3DSPerfScope &) = delete;
	F3DSPerfScope &operator=(const F3DSPerfScope &) = delete;

	E3DSPerfPart Part;
	uint64_t Start;
};
