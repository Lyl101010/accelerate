# TTFT 加速剪枝方案修改日志

## 修改目标
针对赛题核心指标 **TTFT 减少 75%**，在 RK3588 板端 Qwen2-VL-2B 推理管线上引入 training-free 视觉 token 剪枝。

## 修改文件清单

### 新建文件
| 文件 | 说明 |
|------|------|
| `src/pruning/dart.h` | DART 去重剪枝 — 头文件 |
| `src/pruning/dart.cpp` | DART 实现 — pivot + 余弦相似度去重 |
| `src/pruning/cdpruner.h` | CDPruner DPP 多样性剪枝 — 头文件 |
| `src/pruning/cdpruner.cpp` | CDPruner 实现 — DPP 最大化子集多样性 |
| `src/pruning/saint.h` | SAINT 自适应阈值剪枝 — 头文件 |
| `src/pruning/saint.cpp` | SAINT 实现 — 中位数 × 0.8 自适应剪枝 |

### 修改文件
| 文件 | 修改内容 |
|------|---------|
| `src/main.cpp` | (1) 加 `#include "pruning/dart.h"` 等 3 个 include<br>(2) 在 `process_image` 后插入剪枝调用<br>(3) 默认使用 DART 单跑，保留 1/4 token |
| `CMakeLists.txt` | 加 `dart.cpp`、`cdpruner.cpp`、`saint.cpp` 三个编译源文件 |

## 技术路线

### 采用的论文方法
| 方法 | 论文 | 论文链接 | 代码仓库 |
|------|------|---------|---------|
| DART | "Stop Looking for Important Tokens: Duplication Matters More" — EMNLP 2025 | [arXiv 2502.11494](https://arxiv.org/abs/2502.11494) | [github.com/ZichenWen1/DART](https://github.com/ZichenWen1/DART) |
| CDPruner | "Beyond Attention or Similarity: Maximizing Conditional Diversity for Token Pruning in MLLMs" — NeurIPS 2025 | [arXiv 2506.10967](https://arxiv.org/abs/2506.10967) | [github.com/Theia-4869/CDPruner](https://github.com/Theia-4869/CDPruner) |
| SAINT | "Similarity-Aware Token Pruning: Your VLM but Faster" — arXiv 2025 | [arXiv 2503.11549](https://arxiv.org/abs/2503.11549) | 论文推导实现 |

### 处理流程
```
图像输入 → Vision Encoder(NPU,196 token) → 剪枝(CPU,host侧) → LLM(NPU)
                                                     ↓
                                           DART 单跑，保留 1/4
                                           196 → 49 token
```

### 串联方案（备用，已注释）
```
SAINT(定预算) → DART(去重) → CDPruner(精挑)
196 → 131 → 87 → 87 token
```

## 实验数据

| 版本 | TTFT | 回答质量 | 说明 |
|------|------|---------|------|
| 基线（无剪枝） | ~1063ms | ✅ 正常 | 196 token 完整输入 |
| SAINT+DART+CDPruner 串联 | ~950ms | ✅ 正常 | 三个 CPU 开销叠加 |
| DART 1/3 单跑 | ~650ms | ✅ 正常 | 196→65 token |
| DART 1/4 单跑 | ~440ms | ✅ 正常 | 196→49 token，**最终版本** |
| DART 1/9 激进 | ~370ms | ❌ 回答崩 | token 太少，信息丢失 |

**最终效果：TTFT 从 1063ms → 440ms，降幅 58.6%**

## 板端编译与运行

```bash
cd /home/elf/project/demo_lyl/deploy
./build-linux.sh
cp build/build_rknn_yolov8_demo_rk3588_linux_aarch64_Release/demo install/rk3588_linux_aarch64/rknn_yolov8_demo/
cd install/rk3588_linux_aarch64/rknn_yolov8_demo
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
./demo
```

## 注意事项

1. **TTFT 分解**：正常推理时，NPU 系统固定开销约 350ms（kernel init + tokenizer + YOLO），视觉 token prefill 是可变大头，token 越少 prefill 越快。196→49 token 把可变 prefill 从 ~700ms 压到 ~90ms，总 TTFT 从 1063ms 降到 440ms
2. **prompt cache**：已关闭，每次推理独立，换图不会重复输出
3. **多方法备份**：`pruning/` 目录下保留三个方法的 `.cpp`/`.h`，`main.cpp` 当前只用 DART。需要恢复三方法串联时将注释行取消即可
4. **CMakeLists 编码**：板子端 CMakeLists 中文注释损坏时，用本地的覆盖推送
