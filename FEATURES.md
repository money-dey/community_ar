# Community AR — Features

Complete feature inventory of the library, grouped by status (shipped, planned) and functional area. Use this as the source of truth when updating the README, marketing pages, or pub.dev description.

---

## Shipped (Phases 0 → 3)

### Camera & rendering pipeline
- Zero-copy camera-to-GPU pipeline on both Android and iOS
- Front and back camera support with runtime switching
- OES external texture handling on Android with EGL context sharing
- Metal `MTLTexture` + `CVPixelBuffer` IOSurface sharing on iOS
- Direct Flutter texture handoff — no Dart-side pixel copies
- BoxFit-style fitting (cover, contain, fill) in the widget
- Automatic camera rotation and mirroring for front-facing capture

### Face perception
- 468-point face landmark detection via MediaPipe FaceMesh
- 52 ARKit-style blendshape coefficients (jawOpen, eyeBlinkLeft, mouthSmile, etc.)
- 6-DoF face pose estimation via custom PnP solver (no OpenCV dependency)
- Iris detection for both eyes (5 contour points + center + radius per eye)
- Hair segmentation as a GPU texture
- Selfie/body segmentation as a GPU texture
- **Multi-class segmentation (background, hair, body-skin, face-skin, clothes) via `MulticlassSegmenterBackend`**
- **Swappable segmenter backends with graceful fallback**
- Per-face motion estimation for downstream stabilization

### Multi-face support
- Detection and tracking of up to 4 simultaneous faces (configurable)
- Stable face IDs via Hungarian-IoU tracking across frames
- Per-face state management (filters, motion, skin tone)
- Automatic garbage collection of state for retired faces
- BlazeFace short-range detector with full SSD anchor decoding + weighted NMS

### Temporal smoothing
- One-Euro adaptive filter on every landmark output
- Adaptive cutoff frequency — aggressive smoothing at rest, responsive during motion
- Per-track filter banks keyed by stable face IDs
- Tunable parameters (minCutoff, beta, dCutoff) exposed via API
- Independent filter tuning for face landmarks, iris, and blendshapes

### Skin tone estimation
- Trimmed-mean baseline luminance and chroma per face
- 32-sample GPU compute pass distributed across cheeks, forehead, nose bridge
- Asynchronous GPU readback — render thread never stalls
- Throttled to every 5 frames (~6 updates/second at 30fps)
- Per-face skin tone tracking across frames

### Neural inference
- TensorFlow Lite backend on Android with GPU delegate
- Core ML backend on iOS with automatic ANE/GPU/CPU routing
- Real zero-copy GPU texture binding to model input tensors
- IOSurface-shared `CVPixelBuffer` pipeline on iOS (zero-copy to Core ML)
- Camera input blit cache shared across multiple models per frame
- Model output texture binding for segmentation masks (mask stays on GPU)
- Diagnostic reporting of actual binding mode (GpuTextureZeroCopy / GpuTextureBlit / CpuUpload)
- On-demand model loading — unused models never run
- TFLite GPU delegate with persistent texture binding
- Core ML float16 precision with `mlprogram` format

### Effect system
- Three-engine architecture: MaskedRecolor, LandmarkWarp, AssetOverlay
- Declarative `EffectGraph` with **pass-order-based composition** (`SkinAdjust` → `Recolor` → `Warp` → `Overlay` → `Background` → `PostProcess`)
- Gap-numbered `EffectPass` enum for forward-compatible category additions
- **Shared mask resource pool** with canonical named masks (`masks.faceSkin`, `masks.hair`, etc.)
- Effect `consumes`/`produces` declarations for the mask pool
- Atomic graph replacement — thread-safe runtime updates
- Per-effect perception requirements with automatic OR'd union
- Per-effect mask requirements with automatic union across effects
- Ping-pong framebuffers for multi-effect composition
- Stability-bound numeric type IDs reserved by bucket for forward compatibility
- Hand-rolled versioned C struct serialization across the Dart-to-C++ FFI boundary

### Lip recoloring (`LipsEffect`)
- Live lip color changes on camera feed in real time
- Tunable parameters: color, opacity, edge softness, luminance preservation
- Mouth-open detection via jawOpen blendshape — teeth/tongue stay natural color
- Soft-edged mask rasterization (no visible polygon edges)
- Natural lipstick → matte → flat-painted continuum via single parameter
- Works on all detected faces simultaneously

### Skin beautification (`SkinSmoothEffect`)
- 9-pass multi-band frequency separation pipeline
- Bilateral filter in Oklab L-space for proper cross-skin-tone edge detection
- Per-band scaling: low (preserved), mid (wrinkle attenuation), high (pore detail)
- `detailPreserve` floor prevents "plastic skin" look
- `blemishReduction` dampens dark mid-band features (typical blemish signature)
- Specular control: matte (`-1`) ↔ neutral (`0`) ↔ glow (`+1`) with bloom
- Glow finishing: warmth, highlight lift, clarity (unsharp mask)
- Temporal stabilization with motion gating and disocclusion guard
- **Tone-aware threshold scaling using per-face `baselineLuma` — works across light, medium, and dark skin without per-tone tuning**
- Multi-face support with dominant-face baseline selection
- Per-frame `masks.refinedFaceSkin` published to the pool for downstream effects

### Beauty presets and quality tiers
- Nine pre-tuned `BeautyPresets`: off, natural, subtle, glamour, softGlow, matte, editorial, lowLight, studio
- `BeautyQuality` enum: Auto / High / Medium / Low
- Startup benchmark resolves Auto to the appropriate tier within ~10 frames
- Adaptive throttling: drops one tier after 3 consecutive over-budget frames, restores after 100 consecutive under-budget frames
- Multi-face overload: 3+ faces in frame auto-drops High → Medium
- User's explicit tier choice respected (no throttling, no benchmark)

### Color math
- Oklab perceptual color space for all recolor and beauty operations
- sRGB ↔ linear ↔ Oklab conversion as shared GLSL helper
- Out-of-gamut clamping after Oklab manipulation
- Perceptually uniform blending — consistent visual shifts across base colors

### Mask production
- Landmark-contour-driven mask rasterization
- GPU triangle fan with per-vertex alpha falloff
- Soft edges via smoothstep on interpolated alpha
- Additive contours (outer features) and subtractive contours (inner features)
- Configurable edge softness (0 = sharp, 1 = very soft)
- Multi-face mask combination in a single rasterization pass

### Debug & diagnostics
- Toggleable debug overlay modes: landmarks, iris, hair mask, pose, skin tone, mesh
- Single instanced draw call for all landmark dots (~0.1 ms total cost)
- Anti-aliased dot rendering with per-instance color and radius
- Live perception statistics: face count, inference times per model, filter count
- One-Euro parameter live tuning via API
- Active accelerator name reporting (GPU/ANE/CPU)

### Math & numerics
- Custom PnP face pose solver — Gauss-Newton with Rodrigues rotation parameterization
- Cholesky-decomposition normal equations solver
- Numerical Jacobian computation for reprojection error minimization
- Canonical 3D face point model (9 stable landmarks: nose tip, eye corners, mouth corners, chin, forehead)
- Approximate camera intrinsics derivable from image dimensions + FOV

### Platform integration
- Flutter plugin architecture with method channels
- Kotlin platform adapter on Android with Camera2 integration
- Swift platform adapter on iOS with AVFoundation integration
- JNI bridge with proper threading model
- ObjC++ bridge with ARC interop
- Automatic model extraction from APK assets on first launch (Android)
- iOS `.mlmodelc` bundles linked as framework resources

### Build & tooling
- Single consolidated CMakeLists.txt for both platforms
- Android NDK shared library output
- iOS static library output
- `tools/fetch_models.sh` — automated MediaPipe model download
- `tools/convert_models_to_coreml.py` — `.tflite` → `.mlmodelc` conversion for iOS
- Reproducible model versioning via pinned URLs
- Apache 2.0 license with MediaPipe attribution handling

---

## Planned (Phase 4+)

### Phase 4 — Geometric deformation
- `LandmarkWarpEffect` engine — Bucket B
- Eye enlargement
- Nose reshape
- Lip plumping
- Face slimming
- Animation infrastructure (configs animating over time)

### Phase 5 — 3D accessories
- Filament integration for physically-based rendering (PBR)
- 3D mesh accessories: glasses, hats, earrings, nose rings, necklaces
- 2D sprite accessories: eyelashes, decals
- glTF asset loading with KTX2/Basis texture compression
- Separately downloadable asset packs
- Blendshape-driven asset animation (e.g., glasses that bounce with smile)
- SPIRV-Cross shader unification (one source → GLES + Metal)

### Phase 5.5 — Color grading
- `ImageGradeEffect` — Bucket D
- LUT-based color grading (~256 KB per LUT)
- Film stock emulation (Huji, vintage, B&W, low-exposure)
- Film grain, vignette, light leak overlays

### Phase 6 — Remaining Bucket A effects
- Iris recoloring with both-eye consistency
- Teeth whitening with detection-based masking
- Brow recoloring
- Under-eye brightening
- Hair color enhancement / thickening using segmentation mask
- Beard recoloring / thickening
- Background blur (`BackgroundEffect` — Bucket E)
- Background replacement with custom images
- Per-face individual effect parameters

### Phase 7 — 3D hairstyles
- 3D hair mesh accessories
- Hair texture library with diverse style coverage
- Blendshape-driven hair physics approximation

### Phase 8 — Polish & ecosystem
- Comprehensive cross-device performance profiling
- Effect preset library (matte, editorial, natural, glamour, soft-glow, etc.)
- Snapshot/recording API for sharing
- Localization support
- Comprehensive API documentation
- Production deployment guide

---

## Cross-cutting qualities

These aren't features in the traditional sense but they're what makes the library distinct:

- Production-grade performance budget (~2 ms effect overhead, ~20 ms total pipeline on mid-range Android)
- Memory-conscious design (~25 MB base library + ~12 MB MediaPipe models)
- No dependency on OpenCV or Eigen
- Apache 2.0 licensed (commercial-use friendly)
- Open development model with public roadmap
- Detailed phase-by-phase architecture documentation
- AI-coding-agent-ready (`CLAUDE.md` operating manual)
- Extensible via well-documented extension points (new effects, new perception modules)
