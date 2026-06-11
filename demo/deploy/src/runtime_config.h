#ifndef DEMO_RUNTIME_CONFIG_H_
#define DEMO_RUNTIME_CONFIG_H_

#include <string>

namespace runtime_config {

// 模块名称：运行配置模块（runtime_config）
//
// 模块职责：
// 1. 集中管理板端运行时路径、开关和数值参数，避免 main.cpp/yolov8_utils.cpp
//    各自散落 getenv、默认 IP、默认模型路径。
// 2. 对模型路径提供“环境变量优先、现有文件优先、默认路径兜底”的选择策略。
// 3. 向上层返回结构化配置，调用方只关心业务含义，不直接解析环境变量。
//
// 模块边界：
// - 本模块只读取环境变量、选择默认值、给出配置结构；
// - 不初始化 RKNN/RKLLM，不打开摄像头，不执行上传，不参与推理流程；
// - 如果后续迁移到 config/*.json 或 YAML，应优先替换本模块内部实现，
//   而不是让各业务模块重新散落配置读取逻辑。

struct UploadConfig {
    bool enabled = false;
    std::string host;
};

struct RuntimeConfig {
    std::string yolo_model_path;
    std::string vision_model_path;
    std::string llm_model_path;
    std::string fusion_model_path;
    std::string gps_device;
    int gps_baud = 9600;
    int max_new_tokens = 128;
    int max_context_len = 512;
    int rknn_core_num = 3;
    UploadConfig upload;
};

std::string envStringOr(const char* name, const std::string& fallback);
bool envBoolOr(const char* name, bool fallback);
int positiveEnvIntOr(const char* name, int fallback, int max_allowed);

UploadConfig loadUploadConfig();
RuntimeConfig loadRuntimeConfig();

}  // namespace runtime_config

#endif  // DEMO_RUNTIME_CONFIG_H_
