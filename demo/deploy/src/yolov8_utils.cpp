#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include "yolov8_utils.h"
#include <iostream>
#include <cstring>
#include <opencv2/opencv.hpp>
#include <string>
#include <algorithm>
#include <sys/time.h>
#include <cstdlib>
#include <cctype>
#include <exception>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "send_info.h"
#include "camera.h"
#include "fusion_inference.h"
#include "input/preview_window.h"
#include "input/video_recorder.h"
#include "runtime_config.h"

using namespace std;

// 辅助函数：获取微秒级时间
static double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

std::string current_minute_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm local_time{};
    localtime_r(&now, &local_time);

    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y%m%d_%H%M");
    return oss.str();
}

int read_image_cv(const char* path, image_buffer_t* image) {
    if (!image) {
        printf("错误: image_buffer_t指针为空\n");
        return -1;
    }
    if (!path || strlen(path) == 0) {
        printf("错误: 无效的图像路径\n");
        return -1;
    }

    cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
    if (img.empty()) {
        printf("无法读取图像: %s\n", path);
        return -1;
    }
    if (img.cols == 0 || img.rows == 0) {
        printf("图像宽高为0，无效图像: %s\n", path);
        return -1;
    }

    cv::Mat rgb_img;
    cv::cvtColor(img, rgb_img, cv::COLOR_BGR2RGB);

    const int RGA_ALIGN = 16;
    int aligned_width = (rgb_img.cols + RGA_ALIGN - 1) / RGA_ALIGN * RGA_ALIGN;
    int aligned_height = (rgb_img.rows + RGA_ALIGN - 1) / RGA_ALIGN * RGA_ALIGN;

    cv::Mat aligned_rgb;
    if (rgb_img.cols != aligned_width || rgb_img.rows != aligned_height) {
        cv::copyMakeBorder(
            rgb_img, 
            aligned_rgb, 
            (aligned_height - rgb_img.rows) / 2,
            (aligned_height - rgb_img.rows) - (aligned_height - rgb_img.rows) / 2,
            (aligned_width - rgb_img.cols) / 2,
            (aligned_width - rgb_img.cols) - (aligned_width - rgb_img.cols) / 2,
            cv::BORDER_CONSTANT, 
            cv::Scalar(114, 114, 114)
        );
    } else {
        aligned_rgb = rgb_img;
    }

    if (!aligned_rgb.isContinuous()) {
        aligned_rgb = aligned_rgb.clone();
    }

    image->width = aligned_rgb.cols;
    image->height = aligned_rgb.rows;
    image->width_stride = aligned_rgb.step;
    image->height_stride = aligned_rgb.rows;
    image->format = IMAGE_FORMAT_RGB888;
    image->size = aligned_rgb.total() * aligned_rgb.elemSize();
    image->virt_addr = new (std::nothrow) unsigned char[image->size];
    if (!image->virt_addr) {
        printf("错误: 内存分配失败\n");
        return -1;
    }
    memcpy(image->virt_addr, aligned_rgb.data, image->size);
    image->fd = -1;

    printf("RGA对齐后图像参数: 宽=%d, 高=%d, 步长=%d, 格式=%d, 数据地址=%p\n",
           image->width, image->height, image->width_stride, image->format, image->virt_addr);

    return 0;
}

int write_image_cv(const char* path, const image_buffer_t* img) {
    if (!path || strlen(path) == 0) {
        printf("错误: 无效的输出路径\n");
        return -1;
    }
    if (!img || !img->virt_addr || img->size <= 0) {
        printf("错误: 无效的图像缓冲区\n");
        return -1;
    }

    printf("write_image path: %s width=%d height=%d format=%d data=%p\n",
           path, img->width, img->height, img->format, img->virt_addr);
    int cv_type = 0;
    
    switch (img->format) {
        case IMAGE_FORMAT_RGB888:
            cv_type = CV_8UC3;
            break;
        case IMAGE_FORMAT_RGBA8888:
            cv_type = CV_8UC4;
            break;
        case IMAGE_FORMAT_GRAY8:
            cv_type = CV_8UC1;
            break;
        default:
            printf("错误: 不支持的图像格式 %d\n", img->format);
            return -1;
    }

    if (img->width <= 0 || img->height <= 0) {
        printf("错误: 无效的图像宽高\n");
        return -1;
    }

    cv::Mat image(img->height, img->width, cv_type, img->virt_addr, img->width_stride);
    
    if (img->format == IMAGE_FORMAT_RGB888) {
        cv::Mat bgrImage;
        cv::cvtColor(image, bgrImage, cv::COLOR_RGB2BGR);
        return cv::imwrite(path, bgrImage) ? 0 : -1;
    }
    
    return cv::imwrite(path, image) ? 0 : -1;
}

int init_yolo_weights(const char* model_path, rknn_app_context_t* app_ctx) {
    if (!model_path || strlen(model_path) == 0 || !app_ctx) {
        std::cerr << "无效的模型路径或上下文指针" << std::endl;
        return -1;
    }
    
    memset(app_ctx, 0, sizeof(rknn_app_context_t));
    init_post_process();
    
    int ret = init_yolov8_model(model_path, app_ctx);
    if (ret != 0) {
        std::cerr << "初始化YOLOv8模型失败! ret=" << ret 
                  << " model_path=" << model_path << std::endl;
        deinit_post_process();
    }
    return ret;
}

int detect_image(rknn_app_context_t* app_ctx, const char* image_path, 
                const char* output_path, image_buffer_t* src_image, std::string& result_str) {
    if (!app_ctx || !image_path || !output_path || !src_image) {
        std::cerr << "无效的参数" << std::endl;
        return -1;
    }
    
    memset(src_image, 0, sizeof(image_buffer_t));
    result_str.clear();
    
    int ret = read_image_cv(image_path, src_image);
    if (ret != 0) {
        std::cerr << "读取图像失败! ret=" << ret 
                  << " image_path=" << image_path << std::endl;
        return ret;
    }
    
    object_detect_result_list od_results;
    ret = inference_yolov8_model(app_ctx, src_image, &od_results);
    if (ret != 0) {
        std::cerr << "推理失败! ret=" << ret << std::endl;
        if (src_image->virt_addr) {
            delete[] static_cast<unsigned char*>(src_image->virt_addr);
            src_image->virt_addr = nullptr;
        }
        return ret;
    }

    if (od_results.count < 0) {
        std::cerr << "无效的检测结果数量: " << od_results.count << std::endl;
        if (src_image->virt_addr) {
            delete[] static_cast<unsigned char*>(src_image->virt_addr);
            src_image->virt_addr = nullptr;
        }
        return -1;
    }
    char text[256];
    for (int i = 0; i < od_results.count; i++) {
        object_detect_result* det_result = &(od_results.results[i]);
        std::string cls_name_en = coco_cls_to_name(det_result->cls_id);

        std::cout << cls_name_en 
                  << " @ (" << det_result->box.left << " " << det_result->box.top
                  << " " << det_result->box.right << " " << det_result->box.bottom
                  << ") " << det_result->prop << std::endl;

        if (det_result->prop > 0.6f) {
            result_str+=" ";
            result_str+=cls_name_en;
        }

        int x1 = std::max(0, std::min(det_result->box.left, src_image->width - 1));
        int y1 = std::max(0, std::min(det_result->box.top, src_image->height - 1));
        int x2 = std::max(x1, std::min(det_result->box.right, src_image->width - 1));
        int y2 = std::max(y1, std::min(det_result->box.bottom, src_image->height - 1));

        draw_rectangle(src_image, x1, y1, x2 - x1, y2 - y1, COLOR_BLUE, 3);
        
        snprintf(text, sizeof(text), "%s %.1f%%", 
                 cls_name_en.c_str(), 
                 det_result->prop * 100);
        int text_y = std::max(20, y1);
        draw_text(src_image, text, x1, text_y - 20, COLOR_RED, 10);
    }
    
    
    ret = write_image_cv(output_path, src_image);
    if (ret != 0) {
        std::cerr << "保存图像失败! ret=" << ret << std::endl;
    } else {
        std::cout << "检测结果已保存至: " << output_path << std::endl;
    }
    
    if (src_image->virt_addr) {
        delete[] static_cast<unsigned char*>(src_image->virt_addr);
        src_image->virt_addr = nullptr;
    }
    
    return ret;
}

int release_yolo_resources(rknn_app_context_t* app_ctx) {
    if (!app_ctx) {
        std::cerr << "无效的上下文指针" << std::endl;
        return -1;
    }
    
    int ret = release_yolov8_model(app_ctx);
    if (ret != 0) {
        std::cerr << "释放YOLOv8模型失败! ret=" << ret << std::endl;
    }
    deinit_post_process();
    return ret;
}

int process_fused_camera_shot(rknn_app_context_t* app_ctx,
                              const char* fused_image_path,
                              const char* output_image_path,
                              std::string& result_str) {
    if (!app_ctx || !fused_image_path || !output_image_path) {
        cerr << "Invalid camera fusion arguments" << endl;
        return -1;
    }

    static SeAFusion fusion;
    static bool load_attempted = false;
    static bool fusion_ready = false;

    if (!load_attempted) {
        // 默认模型路径由 runtime_config 模块统一选择。
        // 如果比赛现场需要替换微调/量化后的融合模型，只需要设置：
        // export DEMO_FUSION_MODEL=/path/to/xxx.rknn
        const runtime_config::RuntimeConfig config = runtime_config::loadRuntimeConfig();
        const std::string fusion_model_path = config.fusion_model_path;
        cout << "[fusion] Loading model: " << fusion_model_path << endl;
        fusion_ready = fusion.load(fusion_model_path);
        load_attempted = true;
    }

    if (!fusion_ready || !fusion.isLoaded()) {
        cerr << "[fusion] Fusion model is not available. Set DEMO_FUSION_MODEL to the RKNN model path." << endl;
        return -1;
    }

    cv::Mat rgb_frame;
    cv::Mat ir_frame;
    bool captured = false;
    for (int i = 0; i < 5; ++i) {
        if (get_camera_pair(rgb_frame, ir_frame)) {
            captured = true;
        }
    }
    if (!captured) {
        cerr << "[fusion] Failed to capture RGB/IR frames" << endl;
        return -1;
    }

    const std::string shot_timestamp = current_minute_timestamp();
    const std::string timestamped_rgb_path = "camera_rgb_" + shot_timestamp + ".jpg";
    const std::string timestamped_ir_path = "camera_ir_" + shot_timestamp + ".jpg";
    const std::string timestamped_fused_path = "camera_shot_" + shot_timestamp + ".jpg";
    const std::string timestamped_detected_path = "camera_detected_" + shot_timestamp + ".jpg";
	
    // 模块化说明：
    // camera_shot 是板端 P0 验收的“双光单帧闭环”入口。这里保存原始 RGB/IR
    // 帧，只负责把 camera 模块采到的原始输入落盘，方便比赛演示和问题排查：
    //
    //   camera_rgb.jpg / camera_rgb_YYYYMMDD_HHMM.jpg
    //   camera_ir.jpg / camera_ir_YYYYMMDD_HHMM.jpg
    //   camera_shot.jpg / camera_shot_YYYYMMDD_HHMM.jpg
    //   camera_detected.jpg / camera_detected_YYYYMMDD_HHMM.jpg
    //
    // 固定文件名表示“最新一次结果”，供后续 Qwen 图像编码和快速 scp 使用；
    // 时间戳文件名用于演示留档，精确到分钟，避免多次拍摄后只能看到最后一张。
    //
    // 这些保存动作不改变后续 SeaFusion、YOLO、KG、Qwen 的模块职责，
    // 也不把图像采集细节泄漏到 main.cpp。
    if (!cv::imwrite("camera_rgb.jpg", rgb_frame)) {
        cerr << "[fusion] Failed to save original RGB frame: camera_rgb.jpg" << endl;
    } else {
        cout << "[fusion] Original RGB frame saved to camera_rgb.jpg" << endl;
    }
    if (!cv::imwrite(timestamped_rgb_path, rgb_frame)) {
        cerr << "[fusion] Failed to save timestamped RGB frame: " << timestamped_rgb_path << endl;
    } else {
        cout << "[fusion] Timestamped RGB frame saved to " << timestamped_rgb_path << endl;
    }

    if (!cv::imwrite("camera_ir.jpg", ir_frame)) {
        cerr << "[fusion] Failed to save original IR frame: camera_ir.jpg" << endl;
    } else {
        cout << "[fusion] Original IR frame saved to camera_ir.jpg" << endl;
    }
    if (!cv::imwrite(timestamped_ir_path, ir_frame)) {
        cerr << "[fusion] Failed to save timestamped IR frame: " << timestamped_ir_path << endl;
    } else {
        cout << "[fusion] Timestamped IR frame saved to " << timestamped_ir_path << endl;
    }

    cv::Mat fused_bgr;
    try {
        fused_bgr = fusion.fuse(rgb_frame, ir_frame);
    } catch (const std::exception& e) {
        cerr << "[fusion] Inference failed: " << e.what() << endl;
        return -1;
    }

    if (fused_bgr.empty() || !cv::imwrite(fused_image_path, fused_bgr)) {
        cerr << "[fusion] Failed to save fused image: " << fused_image_path << endl;
        return -1;
    }
    if (!cv::imwrite(timestamped_fused_path, fused_bgr)) {
        cerr << "[fusion] Failed to save timestamped fused image: " << timestamped_fused_path << endl;
    } else {
        cout << "[fusion] Timestamped fused image saved to " << timestamped_fused_path << endl;
    }

    cout << "[fusion] Fused image saved to " << fused_image_path << endl;
    cout << "正在进行融合图像检测..." << endl;

    image_buffer_t src_image;
    int ret = detect_image(app_ctx, fused_image_path, output_image_path, &src_image, result_str);
    if (ret != 0) {
        cerr << "YOLO detection on fused image failed: " << ret << endl;
    } else {
        cout << "检测完成，结果保存至 " << output_image_path << endl;
        cv::Mat detected_image = cv::imread(output_image_path, cv::IMREAD_COLOR);
        if (detected_image.empty() || !cv::imwrite(timestamped_detected_path, detected_image)) {
            cerr << "[fusion] Failed to save timestamped detected image: "
                 << timestamped_detected_path << endl;
        } else {
            cout << "[fusion] Timestamped detected image saved to "
                 << timestamped_detected_path << endl;
        }
    }
    return ret;
}

struct CameraStreamState {
    CameraStreamState() : preview("YOLOv8 Camera Detection") {}

    rknn_app_context_t* app_ctx = nullptr;
    const char* output_video_path = nullptr;
    cv::VideoWriter video_writer;
    input::VideoRecorderInfo recorder_info;
    input::PreviewWindow preview;
    CameraOptions camera_options;
    bool writer_initialized = false;
    bool running = true;
    int ret = 0;
};

static bool handle_camera_stream_frame(cv::Mat& frame, void* userdata) {
    CameraStreamState* state = static_cast<CameraStreamState*>(userdata);
    if (!state || !state->app_ctx) {
        return false;
    }

    if (!state->writer_initialized) {
        const double fps = state->camera_options.fps > 0 ? state->camera_options.fps : 30;
        // 模块化说明：
        // 视频保存不是实时检测的必要前置条件。这里交给 input::VideoRecorder
        // 统一处理 MP4 -> AVI/MJPG 降级，避免摄像头主循环里散落编码器细节。
        if (!input::openVideoRecorder(state->video_writer,
                                      state->output_video_path,
                                      fps,
                                      cv::Size(frame.cols, frame.rows),
                                      &state->recorder_info)) {
            cerr << "警告: 所有视频保存格式都不可用，将继续检测但不保存视频" << endl;
        } else {
            cout << "视频将保存至: " << state->recorder_info.path
                 << " (codec=" << state->recorder_info.codec << ")" << endl;
        }
        state->writer_initialized = true;
    }

    struct timeval start_time;
    struct timeval stop_time;
    gettimeofday(&start_time, NULL);

    cv::Mat rgb_frame;
    cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);

    image_buffer_t camera_image;
    memset(&camera_image, 0, sizeof(image_buffer_t));
    camera_image.width = rgb_frame.cols;
    camera_image.height = rgb_frame.rows;
    camera_image.format = IMAGE_FORMAT_RGB888;
    camera_image.virt_addr = (unsigned char*)rgb_frame.data;

    object_detect_result_list od_results;
    int detect_ret = inference_yolov8_model(state->app_ctx, &camera_image, &od_results);
    if (detect_ret != 0) {
        cerr << "检测失败! ret=" << detect_ret << endl;
        state->ret = detect_ret;
        return false;
    }

    char text[256];
    for (int i = 0; i < od_results.count; i++) {
        object_detect_result* det_result = &(od_results.results[i]);
        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;

        cv::rectangle(frame, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0), 2);
        sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
        cv::putText(frame, text, cv::Point(x1, y1 - 6),
                    cv::FONT_HERSHEY_DUPLEX, 0.7, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
    }

    gettimeofday(&stop_time, NULL);
    float t = (__get_us(stop_time) - __get_us(start_time)) / 1000;
    cv::putText(frame, cv::format("FPS: %.2f", 1000.0 / t),
                cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);

    if (state->video_writer.isOpened()) {
        state->video_writer.write(frame);
    }

    // 模块化说明：
    // 预览窗口只作为“可选显示模块”。如果板端 OpenCV 缺 GTK/Qt 后端，
    // input::PreviewWindow 会自动降级，不能影响 camera 实时检测和视频保存。
    state->preview.show(frame, &state->running);
    return state->running;
}

// 处理摄像头视频流（10秒自动结束）
int process_camera_stream(rknn_app_context_t* app_ctx, const char* output_video_path) {
    if (!app_ctx || !output_video_path) {
        cerr << "无效的参数" << endl;
        return -1;
    }

    cout << "正在打开摄像头..." << endl;
    CameraStreamState state;
    state.app_ctx = app_ctx;
    state.output_video_path = output_video_path;

    state.preview.openFromEnv();
    bool captured = capture_camera_stream(handle_camera_stream_frame, &state, state.camera_options, 10);

    state.video_writer.release();
    state.preview.close();

    if (!captured && state.ret == 0) {
        cerr << "无法打开摄像头或未获取到视频帧" << endl;
        state.ret = -1;
    }

    if (state.ret == 0) {
        cout << "摄像头视频处理完成" << endl;
        const runtime_config::UploadConfig upload_config = runtime_config::loadUploadConfig();
        if (upload_config.enabled) {
            const string upload_video_path = state.recorder_info.path.empty()
                                             ? string(output_video_path)
                                             : state.recorder_info.path;
            send_info_to_host(upload_config.host, "", upload_video_path, "");
        }
    }

    return state.ret;
}
// 处理本地视频
int process_local_video(rknn_app_context_t* app_ctx, const char* input_video_path, const char* output_video_path) {
    if (!app_ctx || !input_video_path || !output_video_path) {
        cerr << "无效的参数" << endl;
        return -1;
    }

    cout << "正在加载视频: " << input_video_path << endl;

    cv::VideoCapture cap(input_video_path);
    if (!cap.isOpened()) {
        cerr << "无法打开视频文件" << endl;
        return -1;
    }

    int frame_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int frame_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0) fps = 30;

    // 本地视频检测也复用同一个视频保存模块：
    // 首选按调用方传入的路径写出；如果板端 OpenCV 不支持 MP4/avc1，
    // 自动降级为同名 AVI/MJPG，保证检测主流程不中断。
    cv::VideoWriter video_writer;
    input::VideoRecorderInfo recorder_info;
    if (!input::openVideoRecorder(video_writer, output_video_path, fps,
                                  cv::Size(frame_width, frame_height), &recorder_info)) {
        cerr << "警告: 所有输出视频格式都不可用，将继续检测但不保存视频" << endl;
    } else {
        cout << "视频将保存至: " << recorder_info.path
             << " (codec=" << recorder_info.codec << ")" << endl;
    }

    cv::Mat frame;
    bool running = true;
    bool processed_any_frame = false;
    int process_ret = 0;
    struct timeval start_time, stop_time;
    image_buffer_t video_image;
    memset(&video_image, 0, sizeof(image_buffer_t));
    object_detect_result_list od_results;

    input::PreviewWindow preview("YOLOv8 Video Detection");
    preview.openFromEnv();

    while (running && cap.read(frame)) {
        processed_any_frame = true;
        gettimeofday(&start_time, NULL);

        cv::Mat rgb_frame;
        cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);

        video_image.width = rgb_frame.cols;
        video_image.height = rgb_frame.rows;
        video_image.format = IMAGE_FORMAT_RGB888;
        video_image.virt_addr = (unsigned char*)rgb_frame.data;

        int detect_ret = inference_yolov8_model(app_ctx, &video_image, &od_results);
        if (detect_ret != 0) {
            cerr << "检测失败! ret=" << detect_ret << endl;
            process_ret = detect_ret;
            break;
        }

        char text[256];
        for (int i = 0; i < od_results.count; i++) {
            object_detect_result *det_result = &(od_results.results[i]);
            cv::rectangle(frame, cv::Point(det_result->box.left, det_result->box.top), 
                         cv::Point(det_result->box.right, det_result->box.bottom), 
                         cv::Scalar(255, 0, 0), 2);
            sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
            cv::putText(frame, text, cv::Point(det_result->box.left, det_result->box.top - 6), 
                       cv::FONT_HERSHEY_DUPLEX, 0.7, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
        }

        gettimeofday(&stop_time, NULL);
        float t = (__get_us(stop_time) - __get_us(start_time)) / 1000;
        cv::putText(frame, cv::format("FPS: %.2f", 1000.0 / t), 
                   cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);

        if (video_writer.isOpened()) {
            video_writer.write(frame);
        }

        preview.show(frame, &running);
    }

    cap.release();
    video_writer.release();
    preview.close();
    if (!processed_any_frame && process_ret == 0) {
        cerr << "视频处理失败: 未读取到有效视频帧" << endl;
        process_ret = -1;
    }

    if (process_ret == 0) {
        cout << "视频处理完成" << endl;
    }

    const runtime_config::UploadConfig upload_config = runtime_config::loadUploadConfig();
    if (process_ret == 0 && upload_config.enabled) {
        const string upload_video_path = recorder_info.path.empty()
                                         ? string(output_video_path)
                                         : recorder_info.path;
        send_info_to_host(upload_config.host, "", upload_video_path, "");
    }
    
    return process_ret;
}
