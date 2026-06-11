# vlm

Qwen2-VL / RKLLM multimodal analysis module.

Good candidates:

- image square padding and resize for Qwen2-VL vision encoder
- RKNN image encoder initialization and release
- image embedding generation
- RKLLM initialization and release
- prompt formatting
- multimodal inference
- first-token and total-generation timing hooks

Existing related files:

- `../image_enc.cc`
- `../image_enc.h`
- RKLLM calls currently inside `../main.cpp`
