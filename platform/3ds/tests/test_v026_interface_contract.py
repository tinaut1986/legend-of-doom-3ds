#!/usr/bin/env python3
"""Contracts for the v0.26 base through the v0.30 interface polish."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]


class V026InterfaceContractTests(unittest.TestCase):
    def test_v014_overlay_layout_and_controls_are_restored(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        for label in (
            '"PERFORMANCE"',
            '"MEMORY"',
            '"STATUS"',
            '"QUICK DUMP EXCLUDES 136 MB MEMORY IMAGE"',
            '"L+R+A"',
            '"L+R+X"',
            '"L+R+Y"',
        ):
            self.assertIn(label, diagnostics)
        self.assertNotIn('"DEVELOPER OVERLAY"', diagnostics)
        self.assertNotIn('{ 217, 94, "CLOSE" }', diagnostics)

    def test_fps_and_aim_marker_stay_on_the_upper_screen(self):
        main = (ROOT / "src/d_main.cpp").read_text()
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        framebuffer = (ROOT / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp").read_text()
        crosshair = ROOT / "platform/3ds/assets/aim-crosshair.png"
        self.assertIn('"FPS %llu  %.1fMS"', main)
        self.assertIn("I_3DSComposeGameplayFrame", framebuffer)
        self.assertIn("ComposeNativeMenuFps", diagnostics)
        self.assertIn("AimCrosshairAlpha", diagnostics)
        self.assertTrue(crosshair.is_file())

    def test_render_selector_drives_the_real_canvas(self):
        scale = (ROOT / "src/common/rendering/r_videoscale.cpp").read_text()
        self.assertIn("return static_cast<uint32_t>(400 * lod3ds_render_scale / 10)", scale)
        self.assertIn("return static_cast<uint32_t>(240 * lod3ds_render_scale / 10)", scale)
        self.assertIn("vid_scale_customwidth = 400 * self / 10", scale)
        self.assertIn("vid_scale_customheight = 240 * self / 10", scale)

    def test_messages_and_dungeon_map_survive_hud_and_level_changes(self):
        main = (ROOT / "src/d_main.cpp").read_text()
        status = (ROOT / "src/g_statusbar/shared_sbar.cpp").read_text()
        notify = (ROOT / "src/console/c_notifybuffer.cpp").read_text()
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        self.assertIn("StatusBar->DrawMessagesOnly(HUD_None)", main)
        self.assertIn("void DBaseStatusBar::DrawMessagesOnly", status)
        # Legible over any picture: each line on a black band (it replaced a one-pixel shadow).
        self.assertIn("Dim(twod, 0, float(alpha * 0.85)", notify)
        self.assertIn("lod3ds_messages_bottom", notify)
        self.assertIn("trackedMapName.Compare(primaryLevel->MapName)", diagnostics)

    def test_unified_engine_menus_and_bottom_hud_polish(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        patch = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text()
        self.assertNotIn("if (DrawNativePixelMenu(framebuffer))", diagnostics)
        self.assertIn('Font "NewSmallFont", "White"', patch)
        self.assertIn("DrawBottomRoundedRectOutline", diagnostics)
        self.assertIn('NesText(framebuffer, RBoxX, TitleY, Tr("INVENTORY"), 1, Red)', diagnostics)
        self.assertIn('Tr(BottomMapZoomedOut ? "ZOOM IN" : "ZOOM OUT")', diagnostics)
        self.assertNotIn("DrawBottomTabs", diagnostics)
        self.assertIn("A_OverlayOffset(OverlayID(), 51, 170)", patch)

    def test_controller_menu_fixed_layout_and_expanded_map(self):
        controls = (ROOT / "wadsrc/static/engine/3dsbinds.txt").read_text()
        input_source = (ROOT / "src/common/platform/3ds/i_input_3ds.cpp").read_text()
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        patch = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text()
        for binding in ("rshoulder +attack", "lshoulder +altattack",
                        "dpadleft weapprev", "dpadright weapnext",
                        "ltrigger invprev", "rtrigger invnext", "pad_x +speed"):
            self.assertIn(binding, controls)
        self.assertIn("lod3ds_cstick_sensitivity", input_source)
        self.assertIn("lod3ds_camera_mode", input_source)
        self.assertIn('OPTIONMENU "LegendControllerOptions"', patch)
        self.assertIn('OPTIONMENU "LegendControlsMenu"', patch)
        self.assertIn("DrawBottomAutomap(mapPixels.data(), 0, 0, SourceWidth, SourceHeight, 64.0)", diagnostics)

    def test_control_schemes(self):
        # #22: CLASSIC and BUTTON CAMERA, each remappable, the default by model.
        input_source = (ROOT / "src/common/platform/3ds/i_input_3ds.cpp").read_text()
        game = (ROOT / "src/g_game.cpp").read_text()
        engine_main = (ROOT / "src/d_main.cpp").read_text()
        playsim = (ROOT / "src/playsim/p_map.cpp").read_text()
        player = (ROOT / "src/playsim/p_user.cpp").read_text()
        native = (ROOT / "src/common/platform/3ds/native_menu_3ds.inc").read_text()
        patch = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text()

        # The menu's action list is the engine's, in the same order.
        engine_actions = re.findall(r'\{ "\w+", "[^"]*", "([^"]+)" \}', input_source)
        menu_block = patch[patch.index('+OPTIONVALUE "LegendButtonAction"'):]
        menu_block = menu_block[:menu_block.index("+}")]
        menu_actions = re.findall(r'\+\s+\d+, "([^"]+)"', menu_block)
        self.assertEqual(engine_actions, menu_actions)
        self.assertEqual(len(engine_actions), 16)

        # Every remappable button has its cvar in the engine and its row in BUTTONS.
        for cvar in ("a", "b", "x", "y", "l", "r", "zl", "zr", "up", "down", "left", "right"):
            self.assertIn(f"LOD3DS_BUTTON_CVAR(lod3ds_button_{cvar}, ", input_source)
            self.assertIn(f'"lod3ds_button_{cvar}", "LegendButtonAction"', patch)
        self.assertIn('Command "DEFAULTS", "lod3ds_buttons_reset"', patch)
        self.assertIn('Option "SCHEME", "lod3ds_control_scheme", "LegendControlScheme"', patch)
        self.assertIn('Submenu "BUTTONS", "LegendButtonsMenu"', patch)
        self.assertIn('menu == FName("LegendButtonsMenu")', native)
        # `action` is a ZScript keyword: a variable called so stops the game at start-up.
        self.assertNotRegex(patch, r"\n\+\s+(int|let|String|bool) action\b")

        # Defaults: (button, CLASSIC, BUTTON CAMERA), as the issue's tables.
        for key, classic, camera in (
                ("KEY_PAD_A", "ActUse", "ActRight"), ("KEY_PAD_B", "ActNone", "ActLookDown"),
                ("KEY_PAD_X", "ActSpeed", "ActLookUp"), ("KEY_PAD_Y", "ActJump", "ActLeft"),
                ("KEY_PAD_DPAD_UP", "ActMap", "ActUse"), ("KEY_PAD_DPAD_DOWN", "ActInvUse", "ActMap")):
            self.assertRegex(input_source, rf"\{{ {key}, \"[^\"]+\", {classic}, {camera} \}}")
        self.assertIn("lod3ds_control_scheme = isNew3DS ? 0 : 1;", input_source)
        self.assertIn('CVAR(String, lod3ds_buttons_classic, ""', input_source)
        self.assertIn('CVAR(String, lod3ds_buttons_camera, ""', input_source)
        self.assertIn("I_3DSApplyControlScheme(); // the control scheme's buttons", engine_main)
        self.assertLess(engine_main.index("GameConfig->DoKeySetup(gameinfo.ConfigName);"),
                        engine_main.index("I_3DSApplyControlScheme();"))

        # Button turning with a ramp and CAM SENSITIVITY; sprint at the pad's edge.
        self.assertIn("I_3DSButtonLookStep(static_cast<int>(JoyYawUnitsPerTic)", game)
        self.assertIn("* I_3DSCStickSensitivity()", input_source)
        self.assertIn("if (push >= 0.85f * 0.85f) speedButton = true;", game)

        # Walk into a door or a push block to use it, once per contact.
        for script in ("UnlockAndOpenDoor", "RevealStairs", "RevealSecret", "RevealDoor",
                       "RevealPowerBracelet", "ToggleShoreMusic"):
            self.assertIn(f'"{script}"', playsim)
        self.assertIn("P_3DSWalkIntoUse(line, side, mobj);", playsim)
        # ...and the hint leaves those lines out: they need no button, some need an item first.
        self.assertIn("if (P_3DSWalkIntoLine(line)) return false;", playsim)

        # The first interaction hint names the button bound to +use.
        self.assertIn('I_3DSButtonForCommand("+use")', player)
        self.assertIn('I_3DSTranslate("TO INTERACT PRESS")', player)
        self.assertIn("+TO INTERACT PRESS,LOD_TO_INTERACT_PRESS,,,", patch)
        for label in engine_actions[1:] + ["SCHEME", "BUTTONS", "CLASSIC", "BUTTON CAMERA",
                                           "DEFAULTS", "DPAD LEFT", "DPAD RIGHT"]:
            self.assertRegex(patch, rf"\n\+{re.escape(label)},LOD_\w+,,,", label)

    def test_v030_controls_alignment_and_touch_inventory(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        patch = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text()
        option_menu = (ROOT / "wadsrc/static/zscript/engine/ui/menu/optionmenu.zs").read_text()
        option_items = (ROOT / "wadsrc/static/zscript/engine/ui/menu/optionmenuitems.zs").read_text()
        self.assertIn("class LegendSplitOptionMenu : OptionMenu", patch)
        self.assertIn("override bool UseSplitOptionLayout() { return true; }", patch)
        self.assertIn("override bool UseCompactSplitSliders() { return true; }", patch)
        self.assertIn("GetSplitLabelLeft", option_menu)
        self.assertIn("GetSplitValueRight", option_menu)
        self.assertIn("current.GetSplitLabelLeft()", option_items)
        self.assertIn("current.GetSplitValueRight() - width", option_items)
        self.assertIn("class LegendControlsReferenceMenu", patch)
        self.assertIn("int rowHeight = 11 * CleanYfac_1", patch)
        self.assertNotIn('Menu.OptionWidth("INPUT")', patch)
        self.assertNotIn('mFontColorHeader, "ACTION"', patch)
        self.assertIn('Font "NewSmallFont", "White"', patch)
        self.assertIn("if (BottomSelectItem(column)) DrawBottomOverlay(true);", diagnostics)

    def test_v030_save_load_uses_independent_screens_and_native_keyboard(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        manager = (ROOT / "src/common/menu/savegamemanager.cpp").read_text()
        menu = (ROOT / "wadsrc/static/zscript/engine/ui/menu/loadsavemenu.zs").read_text()
        self.assertIn("DrawNativeSaveLoadBottomFrame", diagnostics)
        self.assertIn("ComposeNativeSaveLoadTop", diagnostics)
        bottom = diagnostics[diagnostics.index("bool DrawNativeSaveLoadBottomFrame") :]
        bottom = bottom[: bottom.index("void DrawNativeMenuBottomFrame")]
        self.assertIn("OverlayFrame(framebuffer, SectionLeft, SectionTop", bottom)
        self.assertIn("SectionHeight, 2, OverlayIvory", bottom)
        self.assertIn("NativeMenuCustomSave = true", bottom)
        self.assertNotIn("menuPixels +", bottom)
        self.assertNotIn('"SAVE GAME" : "LOAD GAME"', bottom)
        self.assertIn("OverlayTextSized(framebuffer, SectionLeft + 9", bottom)
        top = diagnostics[diagnostics.index("void ComposeNativeSaveLoadTop") :]
        top = top[: top.index("void ComposeNativeMenuFps")]
        self.assertIn("constexpr int TargetTop = 21", top)
        self.assertIn("const int InfoLeft = targetLeft", top)
        self.assertIn("const int infoWidth = targetWidth", top)
        self.assertNotIn("InfoLeft, InfoTop, infoWidth, infoHeight, OverlayInk", top)
        self.assertIn("InfoLeft, InfoTop, infoWidth, 1, OverlayIvory", top)
        self.assertIn("NativeTopCenteredText", top)
        self.assertIn("NativeTopWrappedText", top)
        self.assertNotIn("NativeTopCenteredFontText", top)
        self.assertIn("swkbdInputText", manager)
        self.assertIn("I_3DSPrepareNativeKeyboardTop", manager)
        self.assertIn("void I_3DSPrepareNativeKeyboardTop()", diagnostics)
        self.assertIn("manager.OpenNativeKeyboard", menu)
        self.assertIn("manager.DoSave(Selected, mSaveName)", menu)

    def test_v030_stable_uppercase_menus_and_crosshair(self):
        diagnostics = (ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp").read_text()
        patch = (ROOT / "platform/3ds/patches/legend-of-doom-3ds.patch").read_text()
        compose = diagnostics[diagnostics.index("void ComposeNativeMenuTop") :]
        compose = compose[: compose.index("EBottomPresentation DesiredBottomPresentation")]
        self.assertNotIn('"LODPAUSE"', compose)
        self.assertIn("const int verticalOffset = (6 * height + 120) / 240", diagnostics)
        self.assertIn("stableListMenu", diagnostics)
        self.assertIn("minimumX + maximumX - layoutMinimumX - layoutMaximumX", diagnostics)
        self.assertIn('Selector "M_SKULL1", -25, -5', patch)
        for label in ('"NEW GAME"', '"OPTIONS"', '"LOAD GAME"',
                      '"SAVE GAME"', '"QUIT GAME"'):
            self.assertIn(label, patch)
        self.assertIn('Slider "CAM SENSITIVITY"', patch)
        self.assertIn('0.25, 2.0, 0.05, -1', patch)


if __name__ == "__main__":
    unittest.main()
