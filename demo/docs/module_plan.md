# Module plan

Current status: only the directory skeleton has been created. Existing source
files have not been moved, so the current build flow should remain unchanged.

Suggested final call flow:

```text
app
  -> input
  -> detection
  -> risk
  -> vlm
  -> adaptation
  -> transport
  -> telemetry
```

Suggested mapping from current files:

| Current file | Future module |
| --- | --- |
| `deploy/src/main.cpp` | `app`, `common`, `vlm` |
| `deploy/src/yolov8_utils.cpp` | `input`, `detection` |
| `deploy/src/yolov8_utils.h` | `input`, `detection` |
| `deploy/src/image_enc.cc` | `vlm` |
| `deploy/src/image_enc.h` | `vlm` |
| `deploy/src/border_event_analyzer.cpp` | `risk` |
| `deploy/src/border_event_analyzer.h` | `risk` |
| `deploy/src/send_info.cpp` | `transport` |
| `deploy/src/send_info.h` | `transport` |

Keep the low-level RKNN YOLO implementation under `deploy/src/rknpu2/` unless a
later cleanup requires moving it.
