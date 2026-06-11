#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include "yolov8_utils.h"
#include <iostream>
#include <cstring>
#include <opencv2/opencv.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>
#include <sys/time.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cctype>
#include "input/preview_window.h"
#include "input/video_recorder.h"
#include "send_info.h"

using namespace std;

// 辅助函数：获取微秒级时间
static double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

static string getenv_string_or(const char* name, const char* fallback) {
    const char* value = getenv(name);
    return (value && value[0] != '\0') ? string(value) : string(fallback);
}

static bool getenv_bool_or(const char* name, bool fallback) {
    const char* value = getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    string normalized(value);
    transform(normalized.begin(), normalized.end(), normalized.begin(),
              [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

static string demo_upload_host() {
    return getenv_string_or("DEMO_HOST", "192.168.0.100:8000");
}

static bool demo_upload_enabled() {
    string mode = getenv_string_or("DEMO_UPLOAD_MODE", "");
    transform(mode.begin(), mode.end(), mode.begin(),
              [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (mode == "online") {
        return true;
    }
    if (mode == "offline") {
        return false;
    }
    return getenv_bool_or("DEMO_UPLOAD", false);
}

// 检查设备是否存在
static bool device_exists(const string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// 移除static修饰符
void list_video_devices() {
    cout << "可用视频设备:" << endl;
    for (int i = 0; i < 64; i++) {
        string dev = "/dev/video" + to_string(i);
        if (device_exists(dev)) {
            cout << "  " << dev << endl;
        }
    }
    if (device_exists("/dev/video41")) {
        cout << "  /dev/video41" << endl;
    }
}

// 尝试打开摄像头的辅助函数
static cv::VideoCapture open_camera() {
    string dev_path = getenv_string_or("DEMO_CAMERA", "/dev/video41");
    
    if (!device_exists(dev_path)) {
        cerr << "错误: " << dev_path << " 设备不存在" << endl;
        list_video_devices();
        return cv::VideoCapture();
    }

    string pipeline1 = "v4l2src device=" + dev_path + " ! video/x-raw,format=UYVY,width=800,height=600,framerate=30/1 ! videoconvert ! video/x-raw,format=BGR ! appsink";
    cv::VideoCapture cap(pipeline1, cv::CAP_GSTREAMER);
    if (cap.isOpened()) {
        cout << "成功使用UYVY格式打开摄像头 (800x600)" << endl;
        return cap;
    }

    cap.open(dev_path, cv::CAP_V4L2);
    if (cap.isOpened()) {
        cout << "成功使用V4L2接口打开摄像头" << endl;
        return cap;
    }

    cap.open(dev_path);
    if (cap.isOpened()) {
        cout << "成功使用默认参数打开摄像头" << endl;
        return cap;
    }

    cerr << "所有尝试均失败，无法打开摄像头" << endl;
    return cv::VideoCapture();
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

    int channels = 0;
    int cv_type = 0;
    
    switch (img->format) {
        case IMAGE_FORMAT_RGB888:
            channels = 3;
            cv_type = CV_8UC3;
            break;
        case IMAGE_FORMAT_RGBA8888:
            channels = 4;
            cv_type = CV_8UC4;
            break;
        case IMAGE_FORMAT_GRAY8:
            channels = 1;
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
    
    std::unordered_map<std::string, int> cls_count;
    char text[256];
    for (int i = 0; i < od_results.count; i++) {
        if (i >= od_results.count) break;
        
        object_detect_result* det_result = &(od_results.results[i]);
        std::string cls_name_en = coco_cls_to_name(det_result->cls_id);

        std::cout << cls_name_en 
                  << " @ (" << det_result->box.left << " " << det_result->box.top
                  << " " << det_result->box.right << " " << det_result->box.bottom
                  << ") " << det_result->prop << std::endl;
        
        if (det_result->prop > 0.3f) {
            cls_count[cls_name_en]++;
        }

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

int run_yolov8_detection(const char* model_path, 
                         const char* image_path, 
                         const char* output_path,
                         std::string& result_str) {
    if (!model_path || strlen(model_path) == 0 || 
        !image_path || strlen(image_path) == 0 || 
        !output_path || strlen(output_path) == 0) {
        std::cerr << "无效的路径参数（空指针或空字符串）" << std::endl;
        return -1;
    }

    rknn_app_context_t app_ctx;
    image_buffer_t src_image;
    int ret = 0;

    ret = init_yolo_weights(model_path, &app_ctx);
    if (ret != 0) {
        return ret;
    }

    ret = detect_image(&app_ctx, image_path, output_path, &src_image, result_str);
    
    int release_ret = release_yolo_resources(&app_ctx);
    if (release_ret != 0 && ret == 0) {
        ret = release_ret;
    }

    return ret;
}

// 处理摄像头视频流（添加10秒自动结束功能）
int process_camera_stream(rknn_app_context_t* app_ctx, const char* output_video_path) {
    if (!app_ctx || !output_video_path) {
        cerr << "无效的参数" << endl;
        return -1;
    }

    cout << "正在打开摄像头..." << endl;
    cv::VideoCapture cap = open_camera();
    
    if (!cap.isOpened()) {
        cerr << "无法打开摄像头，请检查设备和权限" << endl;
        return -1;
    }

    int frame_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int frame_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0) fps = 30;
    
    cout << "摄像头参数: " << frame_width << "x" << frame_height << ", " << fps << "fps" << endl;

    // 模块化说明：
    // 视频保存只是一种可选证据输出，不属于实时检测的必要路径。
    // 这里交给 input::openVideoRecorder 统一尝试 MP4 和 AVI/MJPG 降级，
    // 避免不同视频流分支各自硬编码 fourcc，后续双光/融合视频也能复用。
    cv::VideoWriter video_writer;
    input::VideoRecorderInfo recorder_info;
    if (!input::openVideoRecorder(video_writer, output_video_path, fps,
                                  cv::Size(frame_width, frame_height), &recorder_info)) {
        cerr << "警告: 所有视频保存格式都不可用，将继续检测但不保存视频" << endl;
    } else {
        cout << "视频将保存至: " << recorder_info.path
             << " (codec=" << recorder_info.codec << ")" << endl;
    }

    cv::Mat frame;
    bool running = true;
    struct timeval start_time, stop_time;
    image_buffer_t camera_image;
    memset(&camera_image, 0, sizeof(image_buffer_t));
    object_detect_result_list od_results;

    // 模块化说明：
    // 摄像头采集、YOLO 推理、结果绘制仍然保留在当前检测流程中；
    // OpenCV 窗口预览被下沉到 input::PreviewWindow 模块统一管理。
    //
    // 这样划分后，当前函数只需要关心“每一帧是否继续检测”，不需要关心
    // 板端 OpenCV 是否带 GTK/Qt 窗口后端。即使 DEMO_PREVIEW=1 时窗口创建失败，
    // PreviewWindow 也会自动降级，camera 实时检测不会因此退出。
    input::PreviewWindow preview("YOLOv8 Camera Detection");
    preview.openFromEnv();

    // 记录开始时间（用于10秒自动停止）
    auto start = std::chrono::steady_clock::now();
    const std::chrono::seconds max_duration(10);  // 最大拍摄时长10秒

    while (running && cap.read(frame)) {
        // 检查是否超过10秒
        auto now = std::chrono::steady_clock::now();
        if (now - start > max_duration) {
            cout << "拍摄已超过10秒，自动结束" << endl;
            running = false;
            break;
        }

        gettimeofday(&start_time, NULL);

        cv::Mat rgb_frame;
        cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);

        camera_image.width = rgb_frame.cols;
        camera_image.height = rgb_frame.rows;
        camera_image.format = IMAGE_FORMAT_RGB888;
        camera_image.virt_addr = (unsigned char*)rgb_frame.data;

        int detect_ret = inference_yolov8_model(app_ctx, &camera_image, &od_results);
        if (detect_ret != 0) {
            cerr << "检测失败! ret=" << detect_ret << endl;
            break;
        }

        char text[256];
        for (int i = 0; i < od_results.count; i++) {
            object_detect_result *det_result = &(od_results.results[i]);
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

        if (video_writer.isOpened()) {
            video_writer.write(frame);
        }

        // 预览属于可选输出：失败只会关闭预览，不影响下一帧继续检测。
        preview.show(frame, &running);
    }

    cap.release();
    video_writer.release();
    preview.close();
    cout << "摄像头视频处理完成" << endl;
    
    if (demo_upload_enabled()) {
        send_info_to_host(demo_upload_host(), "", recorder_info.path.empty() ? output_video_path : recorder_info.path, "");
    }
    
    return 0;
}

// 处理摄像头拍照
int process_camera_shot(rknn_app_context_t* app_ctx, const char* input_image_path, const char* output_image_path, std::string& result_str) {
    if (!app_ctx || !input_image_path || !output_image_path) {
        cerr << "无效的参数" << endl;
        return -1;
    }

    cout << "正在打开摄像头拍照..." << endl;
    cv::VideoCapture cap = open_camera();
    
    if (!cap.isOpened()) {
        cerr << "无法打开摄像头，请检查设备和权限" << endl;
        return -1;
    }

    cv::Mat frame;
    bool success = false;
    for (int i = 0; i < 5; i++) {
        if (cap.read(frame)) {
            success = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    cap.release();
    
    if (!success) {
        cerr << "无法捕获图像" << endl;
        return -1;
    }

    cv::imwrite(input_image_path, frame);
    
    cout << "正在进行图像检测..." << endl;
    image_buffer_t src_image;
    int ret = detect_image(app_ctx, input_image_path, output_image_path, &src_image, result_str);
    
    if (ret != 0) {
        cerr << "YOLO检测失败: " << ret << endl;
    } else {
        cout << "检测完成，结果保存至 " << output_image_path << endl;
        //send_info_to_host("192.168.123.198:8000", output_image_path, "", result_str);
    }
    return ret;
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

    // 本地视频也复用同一个视频保存模块。首选沿用传入路径，若 MP4 不可写，
    // 自动退到同名 .avi + MJPG，避免因为编码器差异影响检测主流程。
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
    struct timeval start_time, stop_time;
    image_buffer_t video_image;
    memset(&video_image, 0, sizeof(image_buffer_t));
    object_detect_result_list od_results;

    // 模块化说明：
    // 本地视频检测和摄像头实时检测复用同一个 PreviewWindow 模块。
    // 预览模块只处理“显示窗口”这一件事；视频读取、YOLO 推理、检测框绘制、
    // 输出视频保存仍然由本函数负责，避免显示逻辑散落在多个检测循环里。
    //
    // 之后接入双光同步采集或 SeaFusion 融合图时，也可以继续复用该模块，
    // 不需要在新的视频流循环里重复写 namedWindow/imshow 异常处理。
    input::PreviewWindow preview("YOLOv8 Video Detection");
    preview.openFromEnv();

    while (running && cap.read(frame)) {
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

        // 预览失败不代表视频检测失败，因此不改变 detect_ret 或函数返回值。
        preview.show(frame, &running);
    }

    cap.release();
    video_writer.release();
    preview.close();
    cout << "视频处理完成" << endl;
    if (demo_upload_enabled()) {
        send_info_to_host(demo_upload_host(), "", recorder_info.path.empty() ? output_video_path : recorder_info.path, "");
    }
    
    return 0;
}
