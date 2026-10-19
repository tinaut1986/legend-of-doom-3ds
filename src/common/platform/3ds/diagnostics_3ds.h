#pragma once

struct F3DSDiagnosticButtonResult
{
	bool SuppressEvent = false;
	bool ReleaseComboKeys = false;
};

// Writes one immediately-flushed startup checkpoint to startup.log. This is
// intentionally independent from GZDoom's logfile so a crash or hang during
// early engine/video initialization still leaves a precise last-known stage.
void I_3DSStartupLog(const char *stage);
// A message for the player, as a toast on the touch screen in the game's own style (English text:
// it goes through Tr()). The engine's own messages print in its generic font over the top screen.
void I_3DSToast(const char *english);
// The same for the C side (ftp_server_3ds.c): `line` is logged as the stage. Main thread only.
extern "C" void I_3DSStartupLogLine(const char *line);
void I_3DSWriteFatalLog(const char *message);

// The 3DS video service is brought up before the expensive WAD/ZScript load.
// These functions draw a tiny framebuffer-only progress screen without
// creating a GZDoom window or a NovaGL context. The final image remains on the
// LCD until the first real GPU frame replaces it.
// True when the game cannot run here and a message was shown: the game must exit (main). A New
// 3DS started in its 64 MB mode (#27).
bool I_3DSStartupBlockingScreen();
// Debug builds: a no-audio.txt in the data folder starts the game without audio (Old 3DS bring-up).
bool I_3DSNoAudioRequested();
// After the audio starts, on the Old 3DS / 2DS: logs core 1's time limit for the application (the
// NDSP mixer and the music stream run there at 30%; #16 made the music cheaper instead).
void I_3DSAudioCpuBudget();
void I_3DSLoadingScreenStart();
void I_3DSLoadingScreenFinish();
// The map-change loading overlay (a level change or a saved game): the dimmed last picture with
// the area's name, what is loading and a bar. Nested calls count; the phases follow
// I_3DSStartupLog stages (map-load-begin, level-freed, map-nodes, ...). mapName is the lump
// (MAP02), levelName what the box shows.
void I_3DSMapLoadBegin(const char *mapName, const char *levelName, bool restoring);
void I_3DSMapLoadEnd();
// After every presented frame: logs map-first-frame for the first one after a map load.
void I_3DSMapLoadFrameShown();
struct F3DSMapLoadOverlay
{
	F3DSMapLoadOverlay(const char *mapName, const char *levelName, bool restoring)
	{
		I_3DSMapLoadBegin(mapName, levelName, restoring);
	}
	~F3DSMapLoadOverlay() { I_3DSMapLoadEnd(); }
};

// Duplicates the most recent upper LCD image into both scanout buffers before
// launching the system software keyboard, so the applet preserves the game
// scene instead of inheriting a cleared back buffer.
void I_3DSPrepareNativeKeyboardTop();

// Hardware-diagnostic builds record one bounded CSV segment with render,
// Citro3D CPU/GPU, and memory timings. Calls compile out at their call sites
// for release builds.
void I_3DSFrameTelemetryBegin();
void I_3DSFrameTelemetryEnd();
void I_3DSFrameTelemetryDraw(unsigned int topology, unsigned int vertices, bool indexed);

// Records the primary-sky background pass without doing any filesystem I/O.
// The counters are written into the next diagnostic dump so a hardware test
// can prove that the fallback actually ran and how many pixels it repaired.
void I_3DSRecordSkyFallback(unsigned int filledPixels);
void I_3DSRecordSkyColumnRepair(unsigned int repairedColumns,
	unsigned int repairedPixels);

// Legacy flat-sky counters retained for compatibility with earlier dumps.
void I_3DSRecordFlatSkyBackground(unsigned int filledPixels);
void I_3DSRecordFlatSkyPortalSkip();

// MAP01 keeps its classic SKYWW cloud texture, but not the remote
// SkyViewpoint scene. These counters make the distance policy observable in
// the next diagnostic dump without logging from renderer hot paths.
void I_3DSRecordSkyViewpointPortalSkip();
void I_3DSRecordDrawDistanceBspCull();
void I_3DSRecordDrawDistanceLineCull();
void I_3DSRecordDrawDistanceSpriteCull();
void I_3DSRecordDrawDistanceFog(unsigned int filledPixels);

// Called from the SDL event pump on the main thread. It only updates the
// combo state and latches a request; it never performs filesystem I/O.
F3DSDiagnosticButtonResult I_3DSDiagnosticButtonEvent(unsigned int button, bool pressed);

// Switches the level-only MAP/ITEMS tabs when the player touches their bottom
// strip. All other lower-screen touches remain ordinary game input.
bool I_3DSDiagnosticTouch(float x, float y);

// The engine still draws its real ListMenu/OptionMenu. The 3DS software
// presenter snapshots the pre-menu canvas, extracts the resulting native menu
// pixels for the lower LCD, then restores the upper canvas with only the
// translucent live/title background.
bool I_3DSNativeMenuVisible();

// Native menus (PLAN U1, native_menu_3ds.inc). OwnsFrame: the open menu is one drawn
// straight on the touch screen, so the engine does not draw it (no M_Drawer).
// Frozen: and the top screen of its first frame is kept, so D_Display renders nothing.
// Present: from PolyFrameBuffer::Update instead of the compositor and the menu route.
bool I_3DSNativeMenuOwnsFrame();
bool I_3DSNativeMenuFrozen();
// gpuOverlays: the frame is presented by the GPU path (gpu_3ds.h), so the FPS counter and the
// crosshair go there, at the LCD's resolution, instead of into the canvas.
void I_3DSPresentNativeMenu(unsigned char *pixels, int pitchBytes, int width, int height,
	bool gpuOverlays);
void I_3DSRouteNativeMenuFrame(unsigned char *menuPixels,
	const unsigned char *basePixels, int pitchBytes, int width, int height,
	bool gpuOverlays);

// The engine 2D cannot go to the GPU this frame: the port needs it inside the canvas pixels
// (a native menu's first frame, which is kept; a menu routed to the touch screen; the top
// automap, which the compositor draws over everything).
bool I_3DSGpu2DBlocked();

// Final CPU-canvas compositor. It makes FPS and the supplied crosshair
// independent of gameplay render scale and replaces the classic brown
// automap with an enlarged copy of the textured lower-screen map.
void I_3DSComposeGameplayFrame(unsigned char *pixels, int pitchBytes,
	int width, int height, bool gpuOverlays);

// Called from the main thread after input polling, before the next frame is
// rendered. A pending request is consumed at most once per chord press.
void I_3DSServiceDiagnosticDump();

// Refreshes the persistent lower-screen map/items interface. This is
// deliberately framebuffer-only: no SDL renderer or GPU context is allocated.
void I_3DSOverlayFrame();
void I_3DSSetAudioReady(bool ready);

// Keeps the independent bottom LCD synchronized with the engine's title-page
// sequence. Credit/lore pages use a black lower screen; the ordinary title
// restores the supplied menu art.
void I_3DSSetMenuStoryPage(bool story);

// Latches the same request as the hardware chord. This exists so automated
// emulator runs can exercise the complete dump writer even when their input
// driver cannot hold three ordinary keyboard keys simultaneously.
void I_3DSRequestDiagnosticDump();
void I_3DSRequestFullDiagnosticDump();
void I_3DSRequestCleanDiagnosticDumps();

// Writes engine-level state that is meaningful without symbolizing the raw
// process-memory capture (map, player/camera, input command and render state).
// Implemented in d_main.cpp, where the engine globals are already available.
bool I_3DSWriteEngineDiagnosticSnapshot(const char *path);

void I_3DSQuitWithoutSaving();
bool I_3DSQuitWithoutSavingRequested();

int I_3DSUpdateAction(int action);
void I_3DSServiceUpdater();

// The in-game FTP server and remote control (ftp_server_3ds.h, debug_link_3ds.h). Service runs
// once per input poll on the game thread: starts or stops the server with OPTIONS -> DEVELOPER ->
// FTP SERVER, logs its lines and relaunches after a SITE INSTALL. Stop before exiting.
void I_3DSServiceConsoleLink();
void I_3DSStopConsoleLink();

// Both LCDs as 24-bit BMPs (the remote control's SITE SHOT).
bool I_3DSSaveScreenShots(const char *topPath, const char *bottomPath);
// Once per input poll: saves the config when the player leaves the option menus.
void I_3DSServiceSettingsSave();
// Text in the player's language: the English text is also the key (LOD_ and its words), looked
// up in language.csv; missing keys return the English (Tr() in native_menu_3ds.inc). The game's
// ACS messages go through it too.
const char *I_3DSTranslate(const char *english);
