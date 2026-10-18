#pragma once

#include <SDL_events.h>

// Translate SDL's raw Nintendo 3DS HID bit/button index to GZDoom's standard
// gamepad key space. Returns 0 for HID bits that are not controller buttons.
int I_3DSMapJoystickButton(unsigned int button);

// Use the bottom-screen touch panel as a relative look surface. GUI capture
// owns touch while menus or text widgets explicitly request it.
void I_3DSHandleTouchEvent(const SDL_Event &event, bool guiCapture);

// Camera preferences exposed by the native Controller options menu.
// 0 = C-Stick, 1 = Touch, 2 = Both.
bool I_3DSCStickLookEnabled();
bool I_3DSTouchLookEnabled();
float I_3DSCStickSensitivity();
bool I_3DSSprintWithXEnabled();

// Control schemes (#22): 0 CLASSIC, 1 BUTTON CAMERA (lod3ds_control_scheme). Applies the active
// scheme's buttons to the key bindings; called once the configuration's bindings are loaded, and
// again whenever the scheme changes.
void I_3DSApplyControlScheme();
bool I_3DSButtonCameraScheme();
// The name of the remappable button bound to `command` ("A", "DPAD UP"), or null.
const char *I_3DSButtonForCommand(const char *command);
// The remappable buttons and what each one does now, for the controls screen (English labels).
int I_3DSRemappableButtonCount();
const char *I_3DSRemappableButtonLabel(int slot);
const char *I_3DSRemappableButtonAction(int slot);
// A turn or look step for a button held `heldTics` tics, from `fullStep` at full speed.
int I_3DSButtonLookStep(int fullStep, int heldTics);
