#include "i_input_3ds.h"

#include "d_eventbase.h"
#include "c_bind.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "diagnostics_3ds.h"
#include "keydef.h"

#include <3ds.h>

#include <algorithm>
#include <cstring>

CUSTOM_CVAR(Float, lod3ds_cstick_sensitivity, 1.0f,
	CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0.25f) self = 0.25f;
	else if (self > 2.0f) self = 2.0f;
}

CUSTOM_CVAR(Int, lod3ds_camera_mode, 2, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0) self = 0;
	else if (self > 2) self = 2;
}

// OPTIONS -> CONTROL -> SPRINT AT FULL LIFE: 0 OFF, 1 BUTTON (the button bound to it, or the Circle
// Pad at its edge in BUTTON CAMERA), 2 AUTO (always, while Link is at full life).
CUSTOM_CVAR(Int, lod3ds_sprint_mode, 2, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0 || self > 2) self = 2;
}

// ---- Control schemes (#22) ------------------------------------------------------------------
//
// Two layouts, chosen in OPTIONS -> CONTROL -> SCHEME: CLASSIC (the camera on the C-Stick and the
// touch screen, today's layout) and BUTTON CAMERA (turn and look with the face buttons, for the
// Old 3DS / 2DS, which have no C-Stick). Each scheme keeps its own buttons: one action per
// remappable button, stored per scheme as a list of action tokens (lod3ds_buttons_classic,
// lod3ds_buttons_camera; empty = the scheme's defaults). The BUTTONS screen edits the active
// scheme through one cvar per button (lod3ds_button_*), and every change is written to the
// engine's key bindings at once. Start, Select, the Circle Pad and the C-Stick are not remappable.

namespace
{
struct FButtonAction
{
	const char *Token;   // stored in the scheme's list
	const char *Command; // the key binding; empty = unbound
	const char *Label;   // what the player reads (LegendButtonAction in the game patch's MENUDEF)
};

// The order is the value of the lod3ds_button_* cvars and of LegendButtonAction: append only.
constexpr FButtonAction ButtonActions[] =
{
	{ "none", "", "NONE" },
	{ "use", "+use", "INTERACT" },
	{ "attack", "+attack", "ATTACK" },
	{ "altattack", "+altattack", "ALT ATTACK" },
	{ "speed", "+speed", "SPRINT" },
	{ "jump", "+jump", "JUMP" },
	{ "left", "+left", "TURN LEFT" },
	{ "right", "+right", "TURN RIGHT" },
	{ "lookup", "+lookup", "LOOK UP" },
	{ "lookdown", "+lookdown", "LOOK DOWN" },
	{ "weapprev", "weapprev", "PREVIOUS WEAPON" },
	{ "weapnext", "weapnext", "NEXT WEAPON" },
	{ "togglemap", "togglemap", "MAP" },
	{ "invuse", "invuse", "USE ITEM" },
	{ "invprev", "invprev", "PREVIOUS ITEM" },
	{ "invnext", "invnext", "NEXT ITEM" },
};
constexpr int ButtonActionCount = static_cast<int>(sizeof(ButtonActions) / sizeof(ButtonActions[0]));

enum EButtonActionIndex
{
	ActNone, ActUse, ActAttack, ActAltAttack, ActSpeed, ActJump, ActLeft, ActRight, ActLookUp,
	ActLookDown, ActWeapPrev, ActWeapNext, ActMap, ActInvUse, ActInvPrev, ActInvNext,
};

struct FButtonSlot
{
	int Key;
	const char *Label;
	int Classic; // the defaults of each scheme
	int Camera;
};

// The order is that of the stored lists and of the BUTTONS screen (the game patch's MENUDEF).
constexpr FButtonSlot ButtonSlots[] =
{
	{ KEY_PAD_A, "A", ActUse, ActRight },
	{ KEY_PAD_B, "B", ActNone, ActLookDown },
	{ KEY_PAD_X, "X", ActSpeed, ActLookUp },
	{ KEY_PAD_Y, "Y", ActJump, ActLeft },
	{ KEY_PAD_LSHOULDER, "L", ActAltAttack, ActAltAttack },
	{ KEY_PAD_RSHOULDER, "R", ActAttack, ActAttack },
	{ KEY_PAD_LTRIGGER, "ZL", ActInvPrev, ActNone },
	{ KEY_PAD_RTRIGGER, "ZR", ActInvNext, ActNone },
	{ KEY_PAD_DPAD_UP, "DPAD UP", ActMap, ActUse },
	{ KEY_PAD_DPAD_DOWN, "DPAD DOWN", ActInvUse, ActMap },
	{ KEY_PAD_DPAD_LEFT, "DPAD LEFT", ActWeapPrev, ActWeapPrev },
	{ KEY_PAD_DPAD_RIGHT, "DPAD RIGHT", ActWeapNext, ActWeapNext },
};
constexpr int ButtonSlotCount = static_cast<int>(sizeof(ButtonSlots) / sizeof(ButtonSlots[0]));

// The active scheme's actions, one per slot.
int SlotActions[ButtonSlotCount];
// False until the configuration and the bindings are loaded (I_3DSApplyControlScheme): the cvars'
// callbacks before that, and while the scheme is being applied, change nothing.
bool SchemeReady;
bool SchemeApplying;

void StoreActiveScheme();
void LoadSlotCVars();
void SetSlotBinding(int slot);
}

// -1 until the first start-up: then BUTTON CAMERA on the Old 3DS / 2DS, CLASSIC on the New 3DS.
CUSTOM_CVAR(Int, lod3ds_control_scheme, -1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < -1 || self > 1) { self = 0; return; }
	if (SchemeReady && !SchemeApplying) I_3DSApplyControlScheme();
}

CVAR(String, lod3ds_buttons_classic, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(String, lod3ds_buttons_camera, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

#define LOD3DS_BUTTON_CVAR(name, slot) \
	CUSTOM_CVAR(Int, name, 0, CVAR_NOINITCALL) \
	{ \
		if (self < 0 || self >= ButtonActionCount) { self = 0; return; } \
		if (!SchemeReady || SchemeApplying) return; \
		SlotActions[slot] = self; \
		SetSlotBinding(slot); \
		StoreActiveScheme(); \
	}

LOD3DS_BUTTON_CVAR(lod3ds_button_a, 0)
LOD3DS_BUTTON_CVAR(lod3ds_button_b, 1)
LOD3DS_BUTTON_CVAR(lod3ds_button_x, 2)
LOD3DS_BUTTON_CVAR(lod3ds_button_y, 3)
LOD3DS_BUTTON_CVAR(lod3ds_button_l, 4)
LOD3DS_BUTTON_CVAR(lod3ds_button_r, 5)
LOD3DS_BUTTON_CVAR(lod3ds_button_zl, 6)
LOD3DS_BUTTON_CVAR(lod3ds_button_zr, 7)
LOD3DS_BUTTON_CVAR(lod3ds_button_up, 8)
LOD3DS_BUTTON_CVAR(lod3ds_button_down, 9)
LOD3DS_BUTTON_CVAR(lod3ds_button_left, 10)
LOD3DS_BUTTON_CVAR(lod3ds_button_right, 11)

namespace
{
FIntCVar *const SlotCVars[ButtonSlotCount] =
{
	&lod3ds_button_a, &lod3ds_button_b, &lod3ds_button_x, &lod3ds_button_y,
	&lod3ds_button_l, &lod3ds_button_r, &lod3ds_button_zl, &lod3ds_button_zr,
	&lod3ds_button_up, &lod3ds_button_down, &lod3ds_button_left, &lod3ds_button_right,
};

bool CameraScheme()
{
	return lod3ds_control_scheme == 1;
}

FStringCVar &ActiveSchemeStore()
{
	return CameraScheme() ? lod3ds_buttons_camera : lod3ds_buttons_classic;
}

int DefaultAction(int slot)
{
	return CameraScheme() ? ButtonSlots[slot].Camera : ButtonSlots[slot].Classic;
}

int ActionFromToken(const char *token, size_t length)
{
	for (int action = 0; action < ButtonActionCount; ++action)
		if (std::strlen(ButtonActions[action].Token) == length &&
			std::strncmp(ButtonActions[action].Token, token, length) == 0) return action;
	return -1;
}

// The active scheme's stored list, or its defaults where the list is empty, short or unknown.
void ReadActiveScheme()
{
	const char *text = ActiveSchemeStore();
	for (int slot = 0; slot < ButtonSlotCount; ++slot)
	{
		int action = -1;
		if (text != nullptr && *text != '\0')
		{
			const char *end = std::strchr(text, ',');
			const size_t length = end != nullptr ? static_cast<size_t>(end - text) : std::strlen(text);
			action = ActionFromToken(text, length);
			text = end != nullptr ? end + 1 : text + length;
		}
		SlotActions[slot] = action >= 0 ? action : DefaultAction(slot);
	}
}

void StoreActiveScheme()
{
	FString text;
	bool defaults = true;
	for (int slot = 0; slot < ButtonSlotCount; ++slot)
	{
		if (slot > 0) text += ',';
		text += ButtonActions[SlotActions[slot]].Token;
		if (SlotActions[slot] != DefaultAction(slot)) defaults = false;
	}
	// The defaults are stored as empty, so a later change of a default reaches the player.
	ActiveSchemeStore() = defaults ? "" : text.GetChars();
}

void LoadSlotCVars()
{
	for (int slot = 0; slot < ButtonSlotCount; ++slot) *SlotCVars[slot] = SlotActions[slot];
}

void SetSlotBinding(int slot)
{
	Bindings.SetBind(ButtonSlots[slot].Key, ButtonActions[SlotActions[slot]].Command);
}
}

void I_3DSApplyControlScheme()
{
	SchemeApplying = true;
	if (lod3ds_control_scheme < 0)
	{
		bool isNew3DS = false;
		APT_CheckNew3DS(&isNew3DS);
		lod3ds_control_scheme = isNew3DS ? 0 : 1;
	}
	ReadActiveScheme();
	for (int slot = 0; slot < ButtonSlotCount; ++slot) SetSlotBinding(slot);
	LoadSlotCVars();
	SchemeApplying = false;
	SchemeReady = true;
}

// OPTIONS -> CONTROL -> BUTTONS -> DEFAULTS: the active scheme's buttons as they come.
CCMD(lod3ds_buttons_reset)
{
	if (!SchemeReady) return;
	ActiveSchemeStore() = "";
	I_3DSApplyControlScheme();
}

bool I_3DSButtonCameraScheme()
{
	return CameraScheme();
}

const char *I_3DSButtonForCommand(const char *command)
{
	// The first remappable button bound to it, in the BUTTONS screen's order.
	for (int slot = 0; slot < ButtonSlotCount; ++slot)
	{
		const char *bind = Bindings.GetBind(ButtonSlots[slot].Key);
		if (bind != nullptr && stricmp(bind, command) == 0) return ButtonSlots[slot].Label;
	}
	return nullptr;
}

int I_3DSRemappableButtonCount()
{
	return ButtonSlotCount;
}

const char *I_3DSRemappableButtonLabel(int slot)
{
	return slot >= 0 && slot < ButtonSlotCount ? ButtonSlots[slot].Label : "";
}

const char *I_3DSRemappableButtonAction(int slot)
{
	if (slot < 0 || slot >= ButtonSlotCount) return "";
	// From the bindings, so the screen shows what the buttons really do.
	const char *bind = Bindings.GetBind(ButtonSlots[slot].Key);
	if (bind == nullptr) return ButtonActions[ActNone].Label;
	for (int action = 1; action < ButtonActionCount; ++action)
		if (stricmp(bind, ButtonActions[action].Command) == 0) return ButtonActions[action].Label;
	return bind;
}

int I_3DSButtonLookStep(int fullStep, int heldTics)
{
	// A slow start for fine aim, full speed after a quarter of a second; CAM SENSITIVITY scales it.
	const float ramp = std::min(1.0f, 0.35f + 0.08f * static_cast<float>(heldTics));
	return static_cast<int>(static_cast<float>(fullStep) * ramp * I_3DSCStickSensitivity());
}

namespace
{
constexpr float TouchWidth = 320.0f;
constexpr float TouchHeight = 240.0f;
constexpr float TouchGain = 2.0f;

float TouchRemainderX;
float TouchRemainderY;

int ConsumeWholeDelta(float &value)
{
	const int whole = static_cast<int>(value);
	value -= whole;
	return whole;
}
}

int I_3DSMapJoystickButton(unsigned int button)
{
	// SDL's N3DS joystick indices are the corresponding libctru HID bits.
	// Keep these as standard gamepad keys so menus and default bindings share
	// the same behavior as the mature XInput and raw-PS2 backends.
	static constexpr int ButtonMap[] =
	{
		KEY_PAD_A,            // 0: A
		KEY_PAD_B,            // 1: B
		KEY_PAD_BACK,         // 2: Select
		KEY_PAD_START,        // 3: Start
		KEY_PAD_DPAD_RIGHT,   // 4: D-pad right
		KEY_PAD_DPAD_LEFT,    // 5: D-pad left
		KEY_PAD_DPAD_UP,      // 6: D-pad up
		KEY_PAD_DPAD_DOWN,    // 7: D-pad down
		KEY_PAD_RSHOULDER,    // 8: R
		KEY_PAD_LSHOULDER,    // 9: L
		KEY_PAD_X,            // 10: X
		KEY_PAD_Y,            // 11: Y
		0,                    // 12: unused HID bit
		0,                    // 13: unused HID bit
		KEY_PAD_LTRIGGER,     // 14: ZL (New 3DS)
		KEY_PAD_RTRIGGER,     // 15: ZR (New 3DS)
	};

	return button < sizeof(ButtonMap) / sizeof(ButtonMap[0]) ? ButtonMap[button] : 0;
}

void I_3DSHandleTouchEvent(const SDL_Event &event, bool guiCapture)
{
	switch (event.type)
	{
	case SDL_FINGERDOWN:
	case SDL_FINGERUP:
		TouchRemainderX = 0.0f;
		TouchRemainderY = 0.0f;
		break;

	case SDL_FINGERMOTION:
		if (!guiCapture && I_3DSTouchLookEnabled())
		{
			TouchRemainderX += event.tfinger.dx * TouchWidth * TouchGain;
			TouchRemainderY += event.tfinger.dy * TouchHeight * TouchGain;
			PostMouseMove(ConsumeWholeDelta(TouchRemainderX), ConsumeWholeDelta(TouchRemainderY));
		}
		break;

	default:
		break;
	}
}

bool I_3DSCStickLookEnabled()
{
	return lod3ds_camera_mode != 1;
}

bool I_3DSTouchLookEnabled()
{
	return lod3ds_camera_mode != 0;
}

float I_3DSCStickSensitivity()
{
	return std::clamp(static_cast<float>(lod3ds_cstick_sensitivity), 0.25f, 2.0f);
}

int I_3DSSprintMode()
{
	return lod3ds_sprint_mode;
}
