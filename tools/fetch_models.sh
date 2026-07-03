#!/usr/bin/env bash
# =============================================================================
# Community AR — Fetch MediaPipe model files for Android (.tflite)
#
# Run from the project root:  bash tools/fetch_models.sh
#
# Windows/Git-Bash note: if curl fails with "schannel ... CRYPT_E_NO_
# REVOCATION_CHECK", the machine can't reach the CA revocation endpoints.
# Workaround that keeps certificate-chain validation on (skips only the
# revocation lookup):
#   echo "ssl-no-revoke" > /tmp/curlhome/.curlrc
#   CURL_HOME=/tmp/curlhome bash tools/fetch_models.sh
#
# Downloads:
#   1. face_landmarker.task (bundle: BlazeFace + FaceMesh + Blendshapes)
#   2. iris_landmark.tflite (legacy MediaPipe Solutions — Iris isn't in Tasks)
#   3. hair_segmenter.tflite
#   4. selfie_segmenter.tflite
#
# Extracts the .task bundle to get the individual .tflite files.
# =============================================================================

set -euo pipefail

MODELS_DIR="$(cd "$(dirname "$0")/.." && pwd)/native/models"
mkdir -p "$MODELS_DIR"
cd "$MODELS_DIR"

echo "==> Fetching to $MODELS_DIR"

# -----------------------------------------------------------------------------
# 1. FaceLandmarker bundle — contains face_detector + face_landmarks + blendshapes
# -----------------------------------------------------------------------------
echo "==> Downloading face_landmarker.task (3 sub-models bundled)"
curl -fL --retry 3 -o face_landmarker.task \
  "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task"

echo "==> Extracting bundled .tflite files"
# A .task is a zip — unzip and rename to our expected filenames
mkdir -p _task_extracted
unzip -o -q face_landmarker.task -d _task_extracted

# The bundle structure is:
#   face_detector.tflite
#   face_landmarks_detector.tflite
#   face_blendshapes.tflite
# Plus a manifest. Map to our naming convention:
mv _task_extracted/face_detector.tflite              face_detector.tflite
mv _task_extracted/face_landmarks_detector.tflite    face_landmarker.tflite
mv _task_extracted/face_blendshapes.tflite           face_blendshapes.tflite
rm -rf _task_extracted face_landmarker.task

# -----------------------------------------------------------------------------
# 2. Iris landmark — legacy MediaPipe Solutions (not yet in Tasks)
#
# NOTE: the old GitHub raw URL (mediapipe/modules/iris_landmark/... on master)
# now 404s — the legacy modules were removed from the repo. Google hosts the
# same file in the mediapipe-assets bucket.
# -----------------------------------------------------------------------------
echo "==> Downloading iris_landmark.tflite (mediapipe-assets bucket)"
curl -fL --retry 3 -o iris_landmark.tflite \
  "https://storage.googleapis.com/mediapipe-assets/iris_landmark.tflite"

# -----------------------------------------------------------------------------
# 3. Hair segmenter
# -----------------------------------------------------------------------------
echo "==> Downloading hair_segmenter.tflite"
curl -fL --retry 3 -o hair_segmenter.tflite \
  "https://storage.googleapis.com/mediapipe-models/image_segmenter/hair_segmenter/float32/1/hair_segmenter.tflite"

# -----------------------------------------------------------------------------
# 4. Selfie segmenter
# -----------------------------------------------------------------------------
echo "==> Downloading selfie_segmenter.tflite"
curl -fL --retry 3 -o selfie_segmenter.tflite \
  "https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_segmenter/float16/latest/selfie_segmenter.tflite"

# -----------------------------------------------------------------------------
# Summary + size check
# -----------------------------------------------------------------------------
echo
echo "==> Models in $MODELS_DIR:"
ls -lh *.tflite
echo
TOTAL=$(du -sh "$MODELS_DIR" | cut -f1)
echo "==> Total models size: $TOTAL"
echo "==> Target budget: <15 MB total"
