# input

图像、视频、摄像头输入与输入侧辅助功能模块。

## 模块职责

- 从磁盘读取图片。
- 打开 USB/MIPI 摄像头设备。
- 读取本地视频文件。
- 从摄像头抓取单帧图像。
- 将 OpenCV 帧转换为下游模型可使用的图像缓冲区。
- 管理输入侧的可选预览窗口，例如 `PreviewWindow`。
- 管理输入流的可选证据视频保存，例如 `VideoRecorder`。

## 当前已拆分功能

### `PreviewWindow`

文件：

- `preview_window.h`
- `preview_window.cpp`

功能边界：

- 只负责 OpenCV HighGUI 预览窗口。
- 只读取 `DEMO_PREVIEW` 环境变量决定是否启用窗口。
- 只捕获 `cv::namedWindow`、`cv::imshow`、`cv::waitKey`、`cv::destroyWindow` 相关异常。
- 在板端 OpenCV 缺少 GTK/Qt 窗口后端时，自动切换为无预览模式。

不负责：

- 不打开摄像头。
- 不读取视频帧。
- 不执行 YOLO/RKNN 检测。
- 不执行 RKLLM/Qwen2-VL 推理。
- 不判断风险等级。
- 不上传结果。

### `VideoRecorder`

文件：

- `video_recorder.h`
- `video_recorder.cpp`

功能边界：

- 只负责创建 OpenCV `VideoWriter`。
- 优先尝试首选输出路径和 MP4V 编码。
- 如果 MP4 不可用，自动退到同名 `.avi` 和 MJPG 编码。
- 返回实际写入路径和编码名称，便于终端日志和后续上传逻辑使用。

不负责：

- 不打开摄像头。
- 不读取视频帧。
- 不写检测框。
- 不执行 YOLO/RKNN 检测。
- 不显示预览窗口。
- 不上传结果。

设计原因：

RK3588 板端 OpenCV 可能缺少完整 FFmpeg/MP4 编码支持。把视频保存能力封装
成独立模块后，实时检测流程不需要关心具体 fourcc，也不会因为 MP4 写不出来
就误判为摄像头或检测失败。

## 设计原则

本模块不应该决定风险等级，也不应该直接调用 RKLLM。

输入模块只向下游模块提供帧、图像或输入侧辅助能力。检测、风险判断、
大模型推理、上传通信应分别留在 detection、risk、vlm、transport 等模块中。
