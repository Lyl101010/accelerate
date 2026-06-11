#ifndef YOLOV8_UTILS_H
#define YOLOV8_UTILS_H

#include "rknn_api.h"
#include "yolov8.h"
#include "file_utils.h"
#include "image_drawing.h"
#include <string>
#include <opencv2/opencv.hpp>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运行YOLOv8目标检测
 * 
 * @param model_path RKNN模型路径
 * @param image_path 输入图像路径
 * @param output_path 输出图像路径
 * @return int 成功返回0，失败返回错误码
 */

int init_yolo_weights(const char* model_path, rknn_app_context_t* app_ctx);
int detect_image(rknn_app_context_t* app_ctx, const char* image_path, 
                const char* output_path, image_buffer_t* src_image, std::string& result_str);
int release_yolo_resources(rknn_app_context_t* app_ctx);


// 模块名称：检测流程编排模块
//
// 模块职责：
// - init_yolo_weights / detect_image / release_yolo_resources：管理 YOLO RKNN 检测；
// - process_camera_stream：RGB 实时视频流逐帧检测；
// - process_fused_camera_shot：RGB/IR 双光拍照 -> SeaFusion 融合 -> YOLO 检测；
// - process_local_video：本地视频逐帧检测。
//
// 模块边界：
// - 本模块负责把采集、融合、检测、绘制、保存串起来；
// - 具体摄像头打开由 camera 模块负责；
// - 具体视频编码降级由 input::VideoRecorder 负责；
// - 具体窗口预览降级由 input::PreviewWindow 负责；
// - 大模型 RKLLM 问答在 main.cpp 中触发。
int process_camera_stream(rknn_app_context_t* app_ctx, const char* output_video_path);
int process_fused_camera_shot(rknn_app_context_t* app_ctx, const char* fused_image_path, const char* output_image_path, std::string& result_str);
int process_local_video(rknn_app_context_t* app_ctx, const char* input_video_path, const char* output_video_path);

#ifdef __cplusplus
}
#endif

#endif // YOLOV8_UTILS_H
