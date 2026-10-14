#!/usr/bin/env python3
"""Offline contracts for the bounded New 3DS hybrid renderer."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class HybridPerformanceContractTests(unittest.TestCase):
    def test_build_profile_enables_safe_world_and_hybrid_presenter(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        profile = build[build.index("  hardware-hybrid)") :]
        profile = profile[: profile.index("    ;;")]
        self.assertIn("SAFE_SOFTWARE=ON", profile)
        self.assertIn("HYBRID_PERFORMANCE=ON", profile)
        self.assertIn("SDL_AUDIO=OFF", profile)
        self.assertIn("GAME_NO_OPENAL=OFF", profile)
        self.assertIn('-DLOD3DS_HYBRID_PERFORMANCE="${HYBRID_PERFORMANCE}"', build)
        self.assertIn('BUILD_PROFILE="$1"', build)

    def test_launch_uses_reduced_canvas_and_core2_drawer(self):
        main = (ROOT / "src/common/platform/posix/sdl/i_main.cpp").read_text(
            encoding="utf-8"
        )
        drawer = (ROOT / "src/common/rendering/r_thread.cpp").read_text(
            encoding="utf-8"
        )
        scale = (ROOT / "src/common/rendering/r_videoscale.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("LOD3DS_HYBRID_PERFORMANCE", main)
        self.assertIn('Args->AppendArg("320")', main)
        self.assertIn('Args->AppendArg("192")', main)
        self.assertIn('Args->AppendArg("2")', main)
        self.assertIn('Args->AppendArg("+r_scene_multithreaded")', main)
        self.assertIn("threadCreate(WorkerMain3DS", drawer)
        self.assertIn("(i == 0) ? 0 : 2", drawer)
        self.assertIn("PlatformMinimumWidth = 40", scale)
        self.assertIn("PlatformMinimumHeight = 24", scale)
        self.assertIn("CUSTOM_CVAR(Float, lod3ds_render_scale, 7", scale)
        self.assertIn("std::round(std::clamp(static_cast<float>(self), 5.0f, 10.0f) * 2.0f) / 2.0f", scale)

    def test_presenter_is_one_bounded_texture_not_novagl_world(self):
        video = (
            ROOT / "src/common/platform/posix/sdl/sdlglvideo.cpp"
        ).read_text(encoding="utf-8")
        gpu = (ROOT / "src/common/platform/3ds/gpu_3ds.cpp").read_text(
            encoding="utf-8"
        )
        framebuffer = (
            ROOT
            / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("C3D_TexInitVRAM(&CanvasTexture", gpu)
        self.assertIn("C3D_SyncDisplayTransfer(CanvasUpload", gpu)
        self.assertIn("constexpr int TopWidth = 400", gpu)
        self.assertIn("GPU_LINEAR, GPU_LINEAR", gpu)
        canvas = gpu[gpu.index("case EMode::Canvas:") :]
        canvas = canvas[: canvas.index("case EMode::Image:")]
        self.assertLess(
            canvas.index("GPU_TEVOP_RGB_SRC_G"), canvas.index("GPU_TEVOP_RGB_SRC_B")
        )
        self.assertLess(
            canvas.index("GPU_TEVOP_RGB_SRC_B"),
            canvas.index("GPU_TEVOP_RGB_SRC_ALPHA"),
        )
        self.assertIn("C3D_FrameEnd(GX_CMDLIST_FLUSH)", gpu)
        self.assertIn("GSPGPU_FlushDataCache(arena.Vertices", gpu)
        self.assertIn("I_3DSGpuPresentTop(pixels, pitch, width, height)", video)
        self.assertIn("hybrid-presenter-sdl-fallback", video)
        self.assertIn("defined(LOD3DS_HYBRID_PERFORMANCE)", framebuffer)

    def test_engine_2d_goes_to_the_gpu_with_a_software_fallback(self):
        framebuffer = (
            ROOT
            / "src/common/rendering/polyrenderer/backend/poly_framebuffer.cpp"
        ).read_text(encoding="utf-8")
        draw = framebuffer[framebuffer.index("void PolyFrameBuffer::Draw2D()") :]
        draw = draw[: draw.index("::Draw2D(twod, *mRenderState);")]
        self.assertIn("I_3DSGpuRecord2D(twod)", draw)
        # The wipe's new screen and the menu base are read from the canvas pixels.
        wipe = framebuffer[framebuffer.index("FTexture *PolyFrameBuffer::WipeEndScreen()") :]
        self.assertLess(wipe.index("mForceSoftware2D = true;"), wipe.index("Draw2D();"))
        start = framebuffer[framebuffer.index("FTexture *PolyFrameBuffer::WipeStartScreen()") :]
        self.assertIn("I_3DSGpuReadTop(", start[: start.index("return tex;")])
        self.assertIn("I_3DSGpuDiscard2D();", framebuffer)

    def test_bottom_screen_transfers_never_overlap_the_frame(self):
        gpu = (ROOT / "src/common/platform/3ds/gpu_3ds.cpp").read_text(
            encoding="utf-8"
        )
        present = gpu[gpu.index("bool I_3DSGpuPresentTop(") :]
        present = present[: present.index("\n}\n")]
        # The bottom layer is copied alone in the GX queue, before the frame starts: inside it,
        # libctru runs it alongside the frame's drawing and the queue hung on the console.
        self.assertLess(
            present.index("PrepareBottom()"), present.index("C3D_FrameBegin(0)")
        )
        prepare = gpu[gpu.index("bool PrepareBottom()") :]
        prepare = prepare[: prepare.index("\n}\n")]
        self.assertIn("C3D_SyncDisplayTransfer(", prepare)
        # Draws keep their own copy of the texture descriptor (the engine may free it first).
        draw = gpu[gpu.index("struct FDraw") :]
        self.assertIn("C3D_Tex Texture;", draw[: draw.index("};")])
        # Without a presented frame the bottom screen goes out through the CPU, never through
        # an empty citro3d frame (one with no draw is never submitted).
        changed = gpu[gpu.index("void I_3DSGpuBottomChanged()") :]
        changed = changed[: changed.index("\n}\n")]
        self.assertNotIn("C3D_FrameBegin", changed)
        self.assertIn("gfxScreenSwapBuffers(GFX_BOTTOM", changed)

    def test_manifest_and_overlay_identify_the_real_hybrid_path(self):
        build = (ROOT / "platform/3ds/build.sh").read_text(encoding="utf-8")
        diagnostics = (
            ROOT / "src/common/platform/3ds/diagnostics_3ds.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("softpoly-core0-core2-pica200-presenter", build)
        self.assertIn("2-explicit-libctru-core0-core2", build)
        self.assertIn(
            "200x120-to-400x240-in-5-percent-steps-plus-400x240-native-menus",
            build,
        )
        self.assertIn("pica200-bilinear", build)
        self.assertIn("bgra-to-rgb-tev-g-b-alpha-before-draw", build)
        self.assertIn(
            "gsp-active-upload-rows-plus-frame-vertex-arena", build
        )
        self.assertIn("I_3DSGameplayResolutionTenths() * 10", diagnostics)
        self.assertIn('"QUICK DUMP"', diagnostics)
        self.assertIn('"FULL DUMP"', diagnostics)
        self.assertIn('"CLEAN DUMPS"', diagnostics)
        self.assertIn("I_3DSRequestDiagnosticDump();", diagnostics)
        self.assertIn("I_3DSRequestFullDiagnosticDump();", diagnostics)
        self.assertIn("I_3DSRequestCleanDiagnosticDumps();", diagnostics)
        self.assertNotIn("FPS LOCK 30", diagnostics)


if __name__ == "__main__":
    unittest.main()
