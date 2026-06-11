可运行项目文件，到deploy/目录下执行 ./build-Linux.sh 编译，到deploy/install/demo_linux_aarch64/目录执行 ./demo 运行程序。
需要把这个目录放在rknn-llm-mian/examples/下，以使用支持库。
本项目是面向展示的，由于构建的检测模型和知识图谱面向边防场景，不适合展示场景（日常场景），去掉了知识图谱的输出，即运行逻辑变为“拍摄-检测-大模型推理-上传”
板端默认使用离线模式完成检测与推理；运行 `DEMO_UPLOAD_MODE=offline ./demo` 不进行网络上传。
在线展示时，先在电脑端执行 `cd host && python3 receive_server.py`，再在板端执行 `DEMO_UPLOAD_MODE=online DEMO_HOST=192.168.0.100:8000 ./demo` 上传图片与结果。
