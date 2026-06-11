#include "runtime_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
#include <vector>

namespace runtime_config {
namespace {

const char* kDefaultUploadHost = "192.168.0.100:8000";
const char* kDefaultGpsDevice = "/dev/ttyUSB0";

bool fileExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFREG);
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string selectModelPath(const char* env_name,
                            const std::vector<std::string>& candidates) {
    const char* env_value = std::getenv(env_name);
    if (env_value && env_value[0] != '\0') {
        return std::string(env_value);
    }

    for (const std::string& candidate : candidates) {
        if (fileExists(candidate)) {
            return candidate;
        }
    }

    const std::string fallback = candidates.empty() ? std::string() : candidates.front();
    std::cerr << "[RuntimeConfig] operation=SelectModelPath env=" << env_name
              << " status=missing fallback=" << fallback
              << " error_message=no candidate model file exists in current working directory"
              << std::endl;
    return fallback;
}

}  // namespace

std::string envStringOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return (value && value[0] != '\0') ? std::string(value) : fallback;
}

bool envBoolOr(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    const std::string normalized = lowerCopy(value);
    if (normalized == "0" || normalized == "false" ||
        normalized == "no" || normalized == "off") {
        return false;
    }
    if (normalized == "1" || normalized == "true" ||
        normalized == "yes" || normalized == "on") {
        return true;
    }
    return fallback;
}

int positiveEnvIntOr(const char* name, int fallback, int max_allowed) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > max_allowed) {
        std::cerr << "[RuntimeConfig] operation=ParseInt env=" << name
                  << " value=" << value
                  << " status=invalid fallback=" << fallback << std::endl;
        return fallback;
    }

    return static_cast<int>(parsed);
}

UploadConfig loadUploadConfig() {
    UploadConfig config;
    config.host = envStringOr("DEMO_HOST", kDefaultUploadHost);

    std::string mode = lowerCopy(envStringOr("DEMO_UPLOAD_MODE", "offline"));
    if (mode == "online") {
        config.enabled = true;
        return config;
    }
    if (mode == "offline") {
        config.enabled = false;
        return config;
    }

    std::cerr << "[RuntimeConfig] operation=LoadUploadConfig env=DEMO_UPLOAD_MODE"
              << " value=" << mode
              << " status=invalid fallback=DEMO_UPLOAD/offline" << std::endl;
    config.enabled = envBoolOr("DEMO_UPLOAD", false);
    return config;
}

RuntimeConfig loadRuntimeConfig() {
    RuntimeConfig config;

    config.yolo_model_path = selectModelPath("DEMO_YOLO_MODEL", {
        "model/ztl_yolov8n_rk3588_i8_ver01.rknn",
        "model/yolov8n_rk3588.rknn",
        "self_best_i8_ver01.rknn",
        "ztl_yolov8n_rk3588_i8_ver01.rknn"
    });

    config.vision_model_path = selectModelPath("DEMO_VISION_MODEL", {
        "Qwen2-VL-2B_vision_rk3588.rknn",
        "model/Qwen2-VL-2B_vision_rk3588.rknn"
    });

    config.llm_model_path = selectModelPath("DEMO_LLM_MODEL", {
        "qwen2_vl_2b_instruct.rkllm",
        "model/qwen2_vl_2b_instruct.rkllm"
    });

    config.fusion_model_path = selectModelPath("DEMO_FUSION_MODEL", {
        "model/seafusion_rk3588_640x512.rknn",
        "seafusion_rk3588_640x512.rknn",
        "SeAFusion.rknn"
    });

    config.gps_device = envStringOr("DEMO_GPS_DEVICE", kDefaultGpsDevice);
    config.gps_baud = positiveEnvIntOr("DEMO_GPS_BAUD", 9600, 921600);
    config.max_new_tokens = positiveEnvIntOr("DEMO_MAX_NEW_TOKENS", 128, 4096);
    config.max_context_len = positiveEnvIntOr("DEMO_MAX_CONTEXT_LEN", 512, 32768);
    config.rknn_core_num = positiveEnvIntOr("DEMO_RKNN_CORE_NUM", 3, 3);
    config.upload = loadUploadConfig();

    return config;
}

}  // namespace runtime_config
