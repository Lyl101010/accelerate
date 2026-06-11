# common

Shared utilities and types used by several modules.

Good candidates:

- environment variable parsing
- path selection and file checks
- directory creation
- common result structs
- small string/time helpers

Avoid putting business logic here. If a function belongs to YOLO, RKLLM, risk
judgement, upload, or metrics, place it in that module instead.
