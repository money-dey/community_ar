# Vendored TensorFlow Lite (Android native build)

The Android C++ perception backend (`native/core/ml/tflite_backend.cpp`) links
the TensorFlow Lite **C API** plus the **GPU delegate**, and drives the delegate
through its **GL-interop** API for zero-copy texture binding. The root
`CMakeLists.txt` consumes those artifacts from this directory.

## Expected layout

```
third_party/tensorflow-lite/
├── include/                          # headers (checked out from TF source)
│   └── tensorflow/lite/
│       ├── c/c_api.h, c_api_types.h, common.h, ...
│       └── delegates/gpu/
│           ├── delegate.h
│           └── gl/{gl_texture,gl_buffer,egl_environment}.h   # GL-interop
└── lib/<ABI>/                        # prebuilt runtime libraries
    ├── libtensorflowlite_c.so
    └── libtensorflowlite_gpu_delegate.so
```

`<ABI>` matches `android/build.gradle.kts` (`arm64-v8a`, `armeabi-v7a`).

The contents of `include/` and `lib/` are **git-ignored** (large / machine-built);
only this README is tracked.

## How to populate it

```sh
bash tools/fetch_tflite.sh          # vendors headers; prints the lib build steps
```

Override the version or ABIs:

```sh
TF_VERSION=v2.17.0 ABIS="arm64-v8a" bash tools/fetch_tflite.sh
```

### Headers
`fetch_tflite.sh` sparse-checks-out `tensorflow/lite` at the pinned tag and
copies the header tree into `include/`. A source checkout (not the Maven AAR) is
required because the GL-interop headers (`delegates/gpu/gl/*`) only exist in
source.

### Libraries (build from source)
There is no single official prebuilt that bundles `libtensorflowlite_c.so` **and**
a GL-interop-enabled GPU delegate, so build them from the same TF checkout with
Bazel + the Android NDK (`./configure` first), per ABI:

```sh
# arm64-v8a
bazel build -c opt --config=android_arm64 \
    //tensorflow/lite/c:tensorflowlite_c \
    //tensorflow/lite/delegates/gpu:libtensorflowlite_gpu_delegate.so
cp bazel-bin/tensorflow/lite/c/libtensorflowlite_c.so                       lib/arm64-v8a/
cp bazel-bin/tensorflow/lite/delegates/gpu/libtensorflowlite_gpu_delegate.so lib/arm64-v8a/
# armeabi-v7a: --config=android_arm, copy into lib/armeabi-v7a/
```

## Version pin

Pinned to **`TF_VERSION` = v2.17.0** in `tools/fetch_tflite.sh`. Keep the
headers and the built `.so`'s on the **same** tag — the GPU delegate ABI and the
C API evolve across releases.

## Known caveat — GL texture binding

`tflite_backend.cpp` calls `TfLiteGpuDelegateBindGlTextureToTensor`. The
**buffer**-binding variant (`...BindGlBufferToTensor`) is long-standing, but the
**texture**-binding variant is experimental and not present in every release.
Before a device run, confirm the symbol exists in your pinned version; if not,
either bump the version or adapt the backend to the buffer-binding path. (This
mirrors the note in `CLAUDE.md` about the GPU delegate's experimental,
version-sensitive flags.)

## Alternative: Gradle prefab / Maven

If you don't need the GL-interop zero-copy path, you can instead consume the
plain interpreter from Maven (`org.tensorflow:tensorflow-lite`,
`tensorflow-lite-gpu`) via AGP `prefab`, and adapt `tflite_backend.cpp` to the
CPU-upload / buffer-binding path. That avoids the source build but gives up the
zero-copy texture binding this backend was written for.
