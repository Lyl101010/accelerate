#ifndef DEMO_COMMON_SYSTEM_RESULT_H_
#define DEMO_COMMON_SYSTEM_RESULT_H_

#include <opencv2/core.hpp>

#include <string>
#include <vector>

// 单个目标检测结果。
// 这个结构用于把 YOLO/RKNN 的内部检测结果转换成更通用的数据格式，
// 方便后续传给知识图谱、Qt界面、日志模块或上传模块。
struct Detection {
    std::string label;     // 目标类别名称，例如 person、vehicle、gun。
    int class_id;          // 模型输出的类别编号；未知时为 -1。
    float confidence;      // 检测置信度，范围通常为 0.0~1.0。
    cv::Rect box;          // 目标框，使用图像像素坐标。

    Detection()
        : class_id(-1),
          confidence(0.0f),
          box()
    {
    }
};

// 系统级结果总线。
// 它只负责承载“一次处理流程”的统一结果，不在这里写检测、融合、
// 知识图谱推理、大模型推理或上传逻辑，避免 common 层反向依赖业务模块。
struct SystemResult {
    // 图像数据：当前项目主要使用 rgb；ir/fused 预留给红外和融合图像链路。
    cv::Mat rgb;
    cv::Mat ir;
    cv::Mat fused;

    // YOLO/RKNN 检测结果。
    std::vector<Detection> detections;
    std::string detector_text;  // 兼容当前 yolo_str 文本链路，例如 "person vehicle"。

    // 知识图谱、大模型和最终决策结果。
    std::string kg_result;
    std::string qwen_result;
    std::string decision_level;
    std::string decision_advice;

    // 各阶段耗时，单位为毫秒。
    double fusion_ms;
    double detect_ms;
    double reasoning_ms;
    double total_ms;

    // 关键模块状态，用于 Qt 展示、日志记录和上传诊断。
    bool camera_ok;
    bool npu_ok;
    bool network_ok;

    SystemResult()
        : fusion_ms(0.0),
          detect_ms(0.0),
          reasoning_ms(0.0),
          total_ms(0.0),
          camera_ok(false),
          npu_ok(false),
          network_ok(false)
    {
    }
};

#endif  // DEMO_COMMON_SYSTEM_RESULT_H_
