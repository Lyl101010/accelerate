# Board-side source layout

This directory is being reorganized from a single demo-style source tree into
clear board-side modules. Existing files remain in place for now so the current
CMake build is not disturbed.

Recommended migration order:

1. Move pure helpers from `main.cpp` into `common/` and `vlm/`.
2. Move camera/video/image input code from `yolov8_utils.cpp` into `input/`.
3. Wrap YOLO/RKNN detection behind `detection/`.
4. Keep rule and risk judgement in `risk/`.
5. Keep upload and host communication in `transport/`.
6. Add timing, FPS, latency, and resource logs in `telemetry/`.
7. Keep CTTA-like threshold/statistics/sample-cache logic in `adaptation/`.

When refactoring is complete, `main.cpp` should only initialize modules and
orchestrate the workflow.
