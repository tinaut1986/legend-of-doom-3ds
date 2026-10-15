#pragma once

#include <cstddef>
#include <cstdint>

// Hold one contiguous block while GZDoom parses ZScript, then return it just
// before SoftPoly creates its large vertex buffer. This prevents abundant but
// fragmented free heap from making that allocation fail on physical hardware.
bool I_3DSReserveRendererMemory();
void I_3DSReleaseRendererMemory();

// One contiguous block taken at start, before ZScript's compile leaves the heap in pieces, and
// handed back right before each map load, whose large blocks (MAP01's 2 MB TEXTMAP, 2.5 MB of
// sides) then find room; taken again once the map is loaded. On the Old 3DS (80 MB) the 2 MB
// TEXTMAP found no room with 17 MB free (2DS, 2026-10-14). The take returns false when no block
// of that size is free.
bool I_3DSReserveMapLoadMemory();
void I_3DSReleaseMapLoadMemory();

size_t I_3DSLastFailedNewRequest();
uintptr_t I_3DSLastFailedNewCaller();
