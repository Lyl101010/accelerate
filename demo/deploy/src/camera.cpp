#include "camera.h"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/stat.h>

namespace {

int parse_video_index(const std::string& device, int fallback) {
    const std::string prefix = "/dev/video";
    if (device.compare(0, prefix.size(), prefix) != 0) {
        return fallback;
    }

    const char* number = device.c_str() + prefix.size();
    if (*number == '\0') {
        return fallback;
    }

    char* end = nullptr;
    long parsed = std::strtol(number, &end, 10);
    if (end == number || *end != '\0' || parsed < 0 || parsed > 255) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

CameraConfig override_camera_from_env(const CameraConfig& base, const char* env_name) {
    CameraConfig cfg = base;
    const char* value = std::getenv(env_name);
    if (!value || value[0] == '\0') {
        return cfg;
    }

    // 模块化说明：
    // 设备选择只属于 camera 输入模块。上层 YOLO、SeaFusion、VLM 不需要知道
    // “/dev/video21” 是默认值还是由环境变量覆盖得到。
    //
    // 支持两种写法：
    // - DEMO_RGB_CAMERA=/dev/video21
    // - DEMO_RGB_CAMERA=21
    std::string device(value);
    if (!device.empty() && std::isdigit(static_cast<unsigned char>(device[0]))) {
        device = "/dev/video" + device;
    }

    cfg.device = device;
    cfg.index = parse_video_index(device, cfg.index);
    return cfg;
}

int read_positive_int_from_env(const char* env_name, int fallback, int min_value, int max_value) {
    const char* value = std::getenv(env_name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < min_value || parsed > max_value) {
        std::cerr << "[Camera] Ignore invalid " << env_name << "=" << value
                  << ", valid range is " << min_value << "-" << max_value
                  << ", fallback=" << fallback << "\n";
        return fallback;
    }
    return static_cast<int>(parsed);
}

CameraOptions resolve_camera_options(const CameraOptions& opt) {
    CameraOptions resolved = opt;
    resolved.rgb = override_camera_from_env(opt.rgb, "DEMO_RGB_CAMERA");
    resolved.ir = override_camera_from_env(opt.ir, "DEMO_IR_CAMERA");
    resolved.stream = override_camera_from_env(opt.stream, "DEMO_CAMERA");

    // 模块化说明：
    // 采集分辨率和帧率属于 camera 输入模块，不应散落在 YOLO、SeaFusion
    // 或 main.cpp 中。这里集中读取环境变量，便于板端根据实际摄像头带宽
    // 做快速降级验证。
    //
    // 典型用法：
    //   DEMO_CAMERA_FPS=10 ./demo
    //   DEMO_CAMERA_WIDTH=320 DEMO_CAMERA_HEIGHT=240 DEMO_CAMERA_FPS=15 ./demo
    resolved.width = read_positive_int_from_env("DEMO_CAMERA_WIDTH", resolved.width, 160, 1920);
    resolved.height = read_positive_int_from_env("DEMO_CAMERA_HEIGHT", resolved.height, 120, 1080);
    resolved.fps = read_positive_int_from_env("DEMO_CAMERA_FPS", resolved.fps, 1, 60);
    return resolved;
}

bool open_camera(const CameraConfig& cfg, const CameraOptions& opt, cv::VideoCapture& cap) {
    cap.open(cfg.device, cv::CAP_V4L2);
    if (!cap.isOpened()) {
        cap.open(cfg.index, cv::CAP_V4L2);
    }
    if (!cap.isOpened()) {
        std::cerr << "Failed to open " << cfg.name << " at " << cfg.device
                  << " or index " << cfg.index << "\n";
        return false;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, opt.width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, opt.height);
    cap.set(cv::CAP_PROP_FPS, opt.fps);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    std::cout << cfg.name << " opened: " << cfg.device
              << " (" << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH))
              << "x" << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT))
              << " @" << cap.get(cv::CAP_PROP_FPS) << "fps)\n";
    return true;
}

bool read_pair(cv::VideoCapture& rgb_cap, cv::VideoCapture& ir_cap, cv::Mat& rgb_frame, cv::Mat& ir_frame) {
    if (!rgb_cap.grab() || !ir_cap.grab()) {
        std::cerr << "Failed to grab RGB/IR frames\n";
        return false;
    }
    if (!rgb_cap.retrieve(rgb_frame) || !ir_cap.retrieve(ir_frame)) {
        std::cerr << "Failed to retrieve RGB/IR frames\n";
        return false;
    }
    if (rgb_frame.empty() || ir_frame.empty()) {
        std::cerr << "Empty RGB/IR frame received\n";
        return false;
    }
    return true;
}

std::string get_stream_device(const CameraOptions& opt) {
    // DEMO_CAMERA 已经在 resolve_camera_options() 中统一解析。
    // 这里不再直接读取环境变量，避免 DEMO_CAMERA=21 这种简写绕过
    // “21 -> /dev/video21”的标准化逻辑。
    return opt.stream.device;
}

bool device_exists(const std::string& path) {
    struct stat buffer;
    return stat(path.c_str(), &buffer) == 0;
}

cv::VideoCapture open_stream_camera(const CameraOptions& opt) {
    const std::string dev_path = get_stream_device(opt);
    if (!device_exists(dev_path)) {
        std::cerr << "Warning: " << dev_path << " does not exist, trying camera index "
                  << opt.stream.index << "\n";
    }

    const std::string pipeline =
        "v4l2src device=" + dev_path +
        " ! video/x-raw,format=UYVY,width=" + std::to_string(opt.width) +
        ",height=" + std::to_string(opt.height) +
        ",framerate=" + std::to_string(opt.fps) +
        "/1 ! videoconvert ! video/x-raw,format=BGR ! appsink";

    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
    if (!cap.isOpened()) {
        cap.open(dev_path, cv::CAP_V4L2);
    }
    if (!cap.isOpened()) {
        cap.open(opt.stream.index, cv::CAP_V4L2);
    }
    if (!cap.isOpened()) {
        std::cerr << "Failed to open stream camera at " << dev_path
                  << " or index " << opt.stream.index << "\n";
        return cv::VideoCapture();
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, opt.width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, opt.height);
    cap.set(cv::CAP_PROP_FPS, opt.fps);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    std::cout << "Stream camera opened: " << dev_path
              << " (" << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH))
              << "x" << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT))
              << " @" << cap.get(cv::CAP_PROP_FPS) << "fps)\n";
    return cap;
}

}  // namespace

bool get_camera_pair(cv::Mat& rgb_frame, cv::Mat& ir_frame, const CameraOptions& opt) {
    static cv::VideoCapture rgb_cap;
    static cv::VideoCapture ir_cap;
    static bool opened = false;

    const CameraOptions resolved = resolve_camera_options(opt);
    if (!opened) {
        if (!open_camera(resolved.rgb, resolved, rgb_cap) ||
            !open_camera(resolved.ir, resolved, ir_cap)) {
            return false;
        }
        opened = true;
    }

    const bool ok = read_pair(rgb_cap, ir_cap, rgb_frame, ir_frame);
    if (!ok) {
        // 模块化说明：
        // 双路摄像头在 VIDIOC_STREAMON/QBUF 阶段失败后，V4L2 句柄可能处于
        // 不可继续取帧的异常状态。如果继续复用静态 VideoCapture，后续重试
        // 容易一直出现 "VIDIOC_QBUF: Invalid argument"。
        //
        // 这里把失败恢复限制在 camera 模块内部：释放 RGB/IR 句柄并重置 opened，
        // 上层 SeaFusion/YOLO 不需要知道底层 V4L2 恢复细节。
        rgb_cap.release();
        ir_cap.release();
        opened = false;
    }
    return ok;
}

bool capture_camera_stream(CameraFrameCallback callback,
                           void* userdata,
                           const CameraOptions& opt,
                           int duration_seconds) {
    if (duration_seconds <= 0) {
        duration_seconds = 10;
    }

    const CameraOptions resolved = resolve_camera_options(opt);
    cv::VideoCapture cap = open_stream_camera(resolved);
    if (!cap.isOpened()) {
        return false;
    }

    const auto start = std::chrono::steady_clock::now();
    const std::chrono::seconds max_duration(duration_seconds);
    cv::Mat frame;
    bool got_frame = false;

    while (cap.read(frame)) {
        const auto now = std::chrono::steady_clock::now();
        if (now - start >= max_duration) {
            std::cout << "Camera stream reached " << duration_seconds
                      << " seconds, closing automatically\n";
            break;
        }

        if (frame.empty()) {
            continue;
        }
        got_frame = true;

        if (callback && !callback(frame, userdata)) {
            break;
        }
    }

    cap.release();
    return got_frame;
}
