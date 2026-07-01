# Community AR Phase 0 — Foundations: Camera-to-Flutter GPU pipeline.

The data-highway smoke test. Camera frame enters at one end, gets handed
through the platform adapter to C++, runs through a trivial GPU shader,
and comes out the other end as a Flutter `Texture` widget.

**No perception, no effects, no Filament.** The goal is to verify the
zero-copy pipeline works on both platforms at real frame rates.

## Files

```
native/
├── CMakeLists.txt
├── core/
│   ├── phase0_session.{h,cpp}                 Trivial Session: camera → shader → output
│   ├── ffi/
│   │   ├── community_ar_phase0_api.h          Minimal C ABI subset
│   │   └── community_ar_phase0_api.cpp        ABI implementation
│   └── render/
│       ├── render_context.h                   Platform-agnostic GPU interface
│       ├── gles_render_context.cpp            OpenGL ES 3.0 backend (Android)
│       └── metal_render_context.mm            Metal backend (iOS)
android/src/main/
├── kotlin/dev/communityar/
│   ├── CommunityARPlugin.kt                   Flutter plugin entry
│   └── CameraStream.kt                        Camera2 → OES texture
└── cpp/jni_bridge.cpp                         Kotlin ↔ C ABI bridge
ios/Classes/
├── CommunityARPlugin.swift                    Flutter plugin entry
└── CameraStream.swift                         AVFoundation → MTLTexture
lib/src/
├── ffi/community_ar_phase0_ffi.dart           Dart-side method channel
└── widgets/community_ar_phase0_view.dart      The widget
example/lib/main.dart                          Smoke-test app
```

## What's working in Phase 0

- Zero-copy camera → GPU texture on both platforms
- Native C++ Session with a RenderContext that abstracts GLES vs Metal
- Four test shaders (passthrough, grayscale, invert, vignette) to verify
  the GPU pipeline is actually executing on real hardware
- Output texture published to Flutter's `TextureRegistry`
- Live stats (frame time, frames processed)
- UI for switching cameras and test modes

## Verification checklist

Before declaring Phase 0 done, all of these must pass on real devices:

### Android
- [ ] App launches without crash on a real device
- [ ] Camera permission prompt appears on first launch
- [ ] After granting permission, camera frames appear in the texture widget
- [ ] Passthrough mode shows live camera at native colors
- [ ] Grayscale mode shows live B&W (proves the shader runs)
- [ ] Invert and vignette modes work
- [ ] Switching front/back camera works without crash
- [ ] Stats show frame time < 16ms on a mid-range device (60fps achievable)
- [ ] No memory growth over a 5-minute session
- [ ] App backgrounding/foregrounding works without crash
- [ ] Tested on Adreno (Snapdragon), Mali (MediaTek/Exynos), and ideally
      one PowerVR device

### iOS
- [ ] Same checklist as Android, but on an iPhone with both A-series chip
      generations old (e.g. iPhone XR) and recent (iPhone 14+)
- [ ] Camera permission prompt appears on first launch
- [ ] No flicker, tearing, or visible frame drops during interactive use
- [ ] Tested on at least one iPad

### Cross-platform
- [ ] Identical visual output between Android and iOS for the same test mode
- [ ] Same approximate frame time on devices of similar tier
- [ ] No goroutine/thread leaks visible in profiler over a 10-minute session

## Known shortcuts (to be cleaned up in later phases)

- The Phase 0 ABI is a minimal subset; full ABI gets merged later
- The Metal context's shader-naming-by-source-content heuristic is hacky;
  proper SPIRV-Cross transpilation comes when we have multiple effects
- The iOS `copyPixelBuffer` doesn't yet share the IOSurface with the
  native MTLTexture; a copy happens. Acceptable for Phase 0; needs the
  CVPixelBufferPool-based path before any effects are added
- Method channel is used for everything; hot-path operations should
  move to `dart:ffi` in Phase 2 once Phase 0 is verified

## When Phase 0 is done, what's next

Phase 1: Perception
- Integrate MediaPipe Tasks (FaceMesh + Iris + Hair seg + Selfie seg)
- One-Euro filter for landmark stability
- Face pose (PnP) computation
- Skin tone estimation
- Plumb 52 blendshape coefficients through PerceptionFrame
- Visualizer overlay for debug (draw landmarks/masks on top of camera feed)
