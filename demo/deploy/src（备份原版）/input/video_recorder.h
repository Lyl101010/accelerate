#ifndef INPUT_VIDEO_RECORDER_H_
#define INPUT_VIDEO_RECORDER_H_

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <string>

namespace input {

// 模块名称：视频保存辅助模块（input::VideoRecorder）
//
// 模块职责：
// 1. 统一封装 OpenCV VideoWriter 的创建逻辑。
// 2. 当首选视频格式不可用时，自动尝试更通用的备用格式。
// 3. 向调用方返回实际写入路径和编码格式，便于终端日志、上传模块或后续调试使用。
//
// 模块边界：
// - 本模块只负责“创建可写的视频文件句柄”。
// - 本模块不负责摄像头采集，不负责 YOLO 推理，不负责绘制检测框，
//   不负责预览窗口，也不负责 HTTP 上传。
// - 调用方仍然负责逐帧调用 writer.write(frame)。
//
// 设计原因：
// RK3588 板端打包的 OpenCV 可能没有完整 FFmpeg/MP4 编码支持。
// 直接用 MP4V/avc1 写 .mp4 时，VideoWriter 可能打不开，于是之前只能打印
// “无法创建视频文件”。把保存逻辑集中到这里后，可以先尝试 MP4，
// 再自动降级到更常见的 AVI + MJPG，减少板端验证时的误判。
struct VideoRecorderInfo {
    std::string path;   // 实际写入的视频路径，可能是首选路径，也可能是降级后的 .avi 路径。
    std::string codec;  // 实际使用的编码名称，例如 MP4V 或 MJPG。
};

// 尝试打开一个可写的视频文件。
//
// 参数说明：
// - writer：调用方提供的 VideoWriter 对象，打开成功后由调用方继续写帧；
// - preferred_path：首选输出路径，例如 camera_detected_video.mp4；
// - fps：输出视频帧率；
// - frame_size：输出视频尺寸，应与写入 frame 的尺寸一致；
// - info：可选输出参数，用于记录实际路径和编码格式。
//
// 返回值：
// - true：至少一种编码/容器组合可用，writer 已打开；
// - false：所有候选组合都失败，调用方应继续检测但不保存视频。
bool openVideoRecorder(cv::VideoWriter& writer,
                       const std::string& preferred_path,
                       double fps,
                       const cv::Size& frame_size,
                       VideoRecorderInfo* info);

}  // 命名空间 input

#endif  // INPUT_VIDEO_RECORDER_H_
