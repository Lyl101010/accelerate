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
int run_yolov8_detection(const char* model_path, 
                         const char* image_path, 
                         const char* output_path,
                         std::string& result_str);

// 新增：声明视频设备列表函数
void list_video_devices();

// 新增功能函数声明
int process_camera_stream(rknn_app_context_t* app_ctx, const char* output_video_path);
int process_camera_shot(rknn_app_context_t* app_ctx, const char* input_image_path, const char* output_image_path, std::string& result_str);
int process_local_video(rknn_app_context_t* app_ctx, const char* input_video_path, const char* output_video_path);

#ifdef __cplusplus
}
#endif

#endif // YOLOV8_UTILS_H