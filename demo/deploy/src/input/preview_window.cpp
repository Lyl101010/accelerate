#include "input/preview_window.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <opencv2/highgui.hpp>

namespace input {
namespace {

// 解析 shell 环境变量里的布尔值。
//
// 这里单独放在匿名命名空间，表示它只服务于 preview_window.cpp 内部，
// 不暴露给摄像头采集、YOLO 检测或 VLM 推理模块。
//
// 约定：
// - 只有 1/true/yes/on 代表显式开启；
// - 未设置、空字符串、拼写错误和其他未知值都按 false 处理。
//
// 这样做是为了板端安全：如果用户写错了 DEMO_PREVIEW，就保持无窗口模式，
// 避免在没有桌面/没有 HighGUI 后端的 RK3588 环境里意外触发窗口崩溃。
bool parseBoolEnv(const char* value) {
    if (!value || value[0] == '\0') {
        return false;
    }

    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return normalized == "1" || normalized == "true" ||
           normalized == "yes" || normalized == "on";
}

}  // 匿名命名空间

PreviewWindow::PreviewWindow(const std::string& window_name)
    : window_name_(window_name), active_(false) {
}

PreviewWindow::~PreviewWindow() {
    close();
}

bool PreviewWindow::openFromEnv(const char* env_name) {
    if (!enabledByEnv(env_name)) {
        std::cout << "预览窗口已关闭，可设置 " << env_name
                  << "=1 尝试打开窗口预览" << std::endl;
        return false;
    }

    // 这里必须保护 cv::namedWindow：
    //
    // 板端日志里已经出现过典型错误：
    // "The function is not implemented. Rebuild the library with Windows,
    // GTK+ 2.x or Carbon support."
    //
    // 这说明摄像头、YOLO 模型和 NPU 都可能是正常的，只是 OpenCV 缺少窗口
    // 后端。如果不在这里捕获异常，camera 命令会在摄像头成功打开后直接 abort。
    // 本模块把这种问题限定为“预览功能不可用”，而不是“检测链路失败”。
    try {
        cv::namedWindow(window_name_, cv::WINDOW_AUTOSIZE);
        active_ = true;
        return true;
    } catch (const cv::Exception& e) {
        active_ = false;
        std::cerr << "警告: OpenCV窗口预览不可用，已切换为无预览模式: "
                  << e.what() << std::endl;
        return false;
    }
}

bool PreviewWindow::show(const cv::Mat& frame, bool* running) {
    if (!active_) {
        return false;
    }

    // show() 只在窗口创建成功后才会走到这里。
    //
    // 仍然继续 try/catch 的原因：
    // - 某些板端环境即使 namedWindow 成功，imshow/waitKey 也可能因为显示服务、
    //   DISPLAY、权限或 HighGUI 初始化不完整而失败；
    // - 这些都属于“显示预览失败”，不能影响 YOLO 实时检测和后续保存/上传逻辑。
    //
    // ESC 退出只作为带窗口调试时的人工停止方式；无窗口模式下外层循环仍然依赖
    // 原来的 10 秒自动结束或视频读取结束。
    try {
        cv::imshow(window_name_, frame);
        char key = static_cast<char>(cv::waitKey(1));
        if (key == 27 && running) {
            *running = false;
        }
        return true;
    } catch (const cv::Exception& e) {
        active_ = false;
        std::cerr << "警告: OpenCV窗口显示失败，已关闭预览: "
                  << e.what() << std::endl;
        return false;
    }
}

void PreviewWindow::close() {
    if (!active_) {
        return;
    }

    try {
        cv::destroyWindow(window_name_);
    } catch (const cv::Exception&) {
        // 关闭窗口失败不需要向外抛出。
        //
        // 关闭阶段通常发生在摄像头/视频检测循环已经结束之后，即使 HighGUI 清理
        // 出错，也不应该覆盖前面检测流程的真实执行结果。
    }
    active_ = false;
}

bool PreviewWindow::active() const {
    return active_;
}

bool PreviewWindow::enabledByEnv(const char* env_name) const {
    return parseBoolEnv(std::getenv(env_name));
}

}  // 命名空间 input
