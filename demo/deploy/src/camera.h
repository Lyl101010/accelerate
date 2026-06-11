#pragma once

#include <string>

#include <opencv2/opencv.hpp>

struct CameraConfig {
    std::string name;
    std::string device;
    int index;
};

struct CameraOptions {
    // 模块名称：摄像头设备配置模块
    //
    // 模块职责：
    // - rgb/ir 用于双光拍照和 SeaFusion 融合链路；
    // - stream 用于 camera 实时视频流检测链路；
    // - width/height/fps 是采集目标参数，实际值仍以 V4L2 返回为准。
    //
    // 双路 RGB/IR 同时取流时，UVC/V4L2 设备常见瓶颈是 USB/CSI 带宽，
    // 不是磁盘空间。板端如果在 VIDIOC_STREAMON 阶段报
    // "No space left on device"，通常表示当前分辨率、帧率或像素格式组合
    // 超出了摄像头总线可承载范围。
    //
    // 默认设备按当前 RK3588 板端双光摄像头配置设置：
    // - RGB: /dev/video21
    // - IR : /dev/video23
    //
    // 环境变量覆盖在 camera.cpp 中处理：
    // - DEMO_RGB_CAMERA：覆盖 RGB 设备；
    // - DEMO_IR_CAMERA ：覆盖 IR 设备；
    // - DEMO_CAMERA       ：覆盖实时视频流设备；
    // - DEMO_CAMERA_WIDTH ：覆盖采集宽度；
    // - DEMO_CAMERA_HEIGHT：覆盖采集高度；
    // - DEMO_CAMERA_FPS   ：覆盖采集帧率。
    CameraConfig rgb{"RGB Camera", "/dev/video21", 21};
    CameraConfig ir{"IR Camera", "/dev/video23", 23};
    CameraConfig stream{"Stream Camera", "/dev/video21", 21};
    int width = 640;
    int height = 480;
    int fps = 15;
};

typedef bool (*CameraFrameCallback)(cv::Mat& frame, void* userdata);

bool get_camera_pair(cv::Mat& rgb_frame, cv::Mat& ir_frame, const CameraOptions& opt = CameraOptions{});
bool capture_camera_stream(CameraFrameCallback callback,
                           void* userdata = nullptr,
                           const CameraOptions& opt = CameraOptions{},
                           int duration_seconds = 10);
