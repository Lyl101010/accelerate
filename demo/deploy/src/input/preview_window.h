#ifndef INPUT_PREVIEW_WINDOW_H_
#define INPUT_PREVIEW_WINDOW_H_

#include <opencv2/core.hpp>
#include <string>

namespace input {

// 模块名称：输入预览窗口模块（input::PreviewWindow）
//
// 模块职责：
// 1. 统一管理 OpenCV HighGUI 相关调用，例如 cv::namedWindow、cv::imshow、
//    cv::waitKey 和 cv::destroyWindow。
// 2. 根据环境变量决定是否启用预览窗口。默认不启用，适配 RK3588 板端
//    SSH/串口/无桌面运行场景。
// 3. 捕获 OpenCV 窗口后端缺失导致的 cv::Exception，自动降级为无预览模式。
//
// 模块边界：
// - 本模块只负责“显示预览”和“显示失败时降级”。
// - 本模块不负责打开摄像头，不负责读取视频，不负责 YOLO/RKNN 推理，
//   不负责 RKLLM 大模型推理，也不负责结果上传。
// - 检测主循环调用本模块时，即使预览失败，也应该继续执行实时检测。
//
// 设计原因：
// 板端打包的 OpenCV 可能没有 GTK/Qt 等 HighGUI 窗口后端。此时直接在
// YOLO 检测循环里调用 cv::namedWindow/cv::imshow 会抛异常并中断程序。
// 把这些调用集中封装到独立模块后，camera/video 流程可以稳定运行在
// “无窗口、只检测、可保存结果”的部署模式下。
class PreviewWindow {
public:
    explicit PreviewWindow(const std::string& window_name);
    ~PreviewWindow();

    // 根据环境变量打开预览窗口。
    //
    // 默认使用 DEMO_PREVIEW：
    // - DEMO_PREVIEW=1/true/yes/on：尝试打开 OpenCV 预览窗口；
    // - DEMO_PREVIEW 未设置、为空、0、false、no、off：保持无窗口模式。
    //
    // 返回值：
    // - true：窗口已成功打开，后续 show() 会显示帧；
    // - false：预览未启用，或窗口后端不可用，调用方应继续执行检测流程。
    bool openFromEnv(const char* env_name = "DEMO_PREVIEW");

    // 显示一帧已经绘制好检测框/FPS 的图像。
    //
    // 参数说明：
    // - frame：由调用方提供的图像帧，本模块只显示，不修改检测结果；
    // - running：可选的循环控制指针。如果窗口可用且用户按 ESC，
    //   本模块会把 *running 设置为 false，让外层视频/摄像头循环退出。
    //
    // 异常策略：
    // 如果 cv::imshow 或 cv::waitKey 在运行时失败，本模块会关闭 active_
    // 状态并返回 false。调用方不需要把它当作检测失败处理。
    bool show(const cv::Mat& frame, bool* running);

    // 关闭预览窗口。
    //
    // 这个函数可以被重复调用；析构函数也会兜底调用一次，防止调用方忘记释放。
    // 对于无窗口模式或窗口创建失败的情况，close() 会直接返回。
    void close();

    // 返回当前是否真的有一个可用的预览窗口。
    bool active() const;

private:
    // 读取并解析环境变量。这里保持在模块内部，避免其他模块重复实现
    // “哪些字符串代表开启预览”的判断逻辑。
    bool enabledByEnv(const char* env_name) const;

    std::string window_name_;
    bool active_;
};

}  // 命名空间 input

#endif  // INPUT_PREVIEW_WINDOW_H_
