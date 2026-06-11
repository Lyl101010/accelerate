#pragma once

#include <memory>
#include <string>

#include <opencv2/opencv.hpp>

// 模块名称：SeaFusion 双光融合推理模块
//
// 模块职责：
// 1. 加载由 seafusion.onnx 转换得到的 RKNN 模型。
// 2. 将 RGB 可见光图像转换为 YCbCr，其中 Y 分量与红外图像一起送入融合模型。
// 3. 将模型输出的融合亮度分量与原始色度分量合成为 BGR 图像，供后续 YOLO/VLM 使用。
//
// 模块边界：
// - 本模块只负责“RGB + IR -> 融合图像”；
// - 不负责打开摄像头，不负责 YOLO 检测，不负责大模型问答；
// - 模型路径由上层通过 DEMO_FUSION_MODEL 或默认值传入，便于板端替换不同 RKNN 文件。
class SeAFusion {
public:
    struct Options {
        bool resize_to_model = true;
    };

    SeAFusion();
    explicit SeAFusion(Options options);
    ~SeAFusion();

    SeAFusion(const SeAFusion&) = delete;
    SeAFusion& operator=(const SeAFusion&) = delete;

    bool load(const std::string& rknn_model_path);
    bool isLoaded() const;

    int inputWidth() const;
    int inputHeight() const;

    cv::Mat fuse(const cv::Mat& vis_bgr, const cv::Mat& ir_frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
