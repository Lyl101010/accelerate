from pathlib import Path
import re

p = Path("src/main.cpp")
s = p.read_text()

# 0. 固定图像 token / embedding size 兼容常量
# 当前 rknn_app_context_t 里没有 model_image_token / model_embed_size，
# 所以统一用固定常量兜底，避免每次源码恢复后又报错。
marker = '#include "rkllm.h"'
compat_block = r'''
#ifndef IMAGE_TOKEN_NUM
#define IMAGE_TOKEN_NUM 196
#endif

#ifndef IMAGE_EMBED_SIZE
#define IMAGE_EMBED_SIZE 2048
#endif
'''

if marker in s and "IMAGE_EMBED_SIZE" not in s:
    s = s.replace(marker, marker + "\n" + compat_block)

# 1. rknn_app_context_t 不存在 model_image_token / model_embed_size
s = s.replace("rknn_app_ctx->model_image_token", "IMAGE_TOKEN_NUM")
s = s.replace("rknn_app_ctx.model_image_token", "IMAGE_TOKEN_NUM")
s = s.replace("rknn_app_ctx->model_embed_size", "IMAGE_EMBED_SIZE")
s = s.replace("rknn_app_ctx.model_embed_size", "IMAGE_EMBED_SIZE")

# 2. 当前 rkllm.h 的 LLMResultCallback 是 void，不是 int
s = re.sub(
    r'\bint\s+callback\s*\(\s*RKLLMResult\s*\*\s*result\s*,\s*void\s*\*\s*userdata\s*,\s*LLMCallState\s+state\s*\)',
    'void callback(RKLLMResult *result, void *userdata, LLMCallState state)',
    s
)

# 3. 只在 callback 函数体里把 return 0; 改成 return;
sig = "void callback(RKLLMResult *result, void *userdata, LLMCallState state)"
start = s.find(sig)
if start != -1:
    brace = s.find("{", start)
    if brace != -1:
        depth = 0
        end = None
        for i in range(brace, len(s)):
            if s[i] == "{":
                depth += 1
            elif s[i] == "}":
                depth -= 1
                if depth == 0:
                    end = i + 1
                    break
        if end:
            block = s[start:end]
            block = block.replace("return 0;", "return;")
            s = s[:start] + block + s[end:]

# 4. 注释当前 rkllm.h 不支持的字段/函数
# 注意：只注释未注释的行，避免重复叠加。
patterns = [
    r'^(\s*)rkllm_infer_params\.keep_history\s*=.*;\s*$',
    r'^(\s*)rkllm_set_chat_template\s*\(.*?\);\s*$',
    r'^(\s*)ret\s*=\s*rkllm_clear_kv_cache\s*\(.*?\);\s*$',
    r'^(\s*)rkllm_clear_kv_cache\s*\(.*?\);\s*$',
    r'^(\s*)rkllm_input\.role\s*=.*;\s*$',
    r'^(\s*)rkllm_input\.multimodal_input\.n_image\s*=.*;\s*$',
    r'^(\s*)rkllm_input\.multimodal_input\.image_height\s*=.*;\s*$',
    r'^(\s*)rkllm_input\.multimodal_input\.image_width\s*=.*;\s*$',
]

for pat in patterns:
    def repl(m):
        line = m.group(0)
        indent = m.group(1)
        if "RKLLM API compatibility" in line or line.lstrip().startswith("//"):
            return line
        return f"{indent}// RKLLM API compatibility: {line.strip()}"
    s = re.sub(pat, repl, s, flags=re.M)

p.write_text(s)
print("[fix_compat] RKLLM/RKNN compatibility patch applied.")
