// Copyright (c) 2024 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// +kg

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <opencv2/opencv.hpp>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "image_enc.h"
#include "rkllm.h"
#include "pruning/dart.h"
#include "pruning/cdpruner.h"
#include "pruning/saint.h"

#ifndef IMAGE_TOKEN_NUM
#define IMAGE_TOKEN_NUM 196
#endif

#ifndef IMAGE_EMBED_SIZE
#define IMAGE_EMBED_SIZE 2048
#endif

#include "yolov8.h"
#include "file_utils.h"
#include "image_drawing.h"
#include "yolov8_utils.h"
#include "send_info.h"
#include "border_event_analyzer.h"
#include "border_kg.h"
#include "GPS.h"
#include "runtime_config.h"

#define PROMPT_TEXT_PREFIX "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\n"
#define PROMPT_TEXT_POSTFIX "<|im_end|>\n<|im_start|>assistant\n"

#define IMAGE_HEIGHT 392
#define IMAGE_WIDTH 392
#define IMAGE_TOKEN_NUM 196
#define EMBED_SIZE 1536

using namespace std;
LLMHandle llmHandle = nullptr;


void exit_handler(int signal)
{
    if (llmHandle != nullptr)
    {
        {
            cout << "程序即将退出" << endl;
            LLMHandle _tmp = llmHandle;
            llmHandle = nullptr;
            rkllm_destroy(_tmp);
        }
    }
    exit(signal);
}

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

void callback(RKLLMResult *result, void *userdata, LLMCallState state)
{
    if (state == RKLLM_RUN_FINISH)
    {
        printf("\n");
    }
    else if (state == RKLLM_RUN_ERROR)
    {
        printf("\\run error\n");
    }
    else if (state == RKLLM_RUN_NORMAL)
    {
        printf("%s", result->text);
    }
    return;
}

// 创建目录（如果不存在）
void create_directory(const string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
#ifdef _WIN32
        if (_mkdir(path.c_str()) != 0) {
#else
        if (mkdir(path.c_str(), 0777) != 0) {
#endif
            cerr << "警告：无法创建目录 " << path << "，可能导致文件保存失败" << endl;
        }
    } else if (!(info.st_mode & S_IFDIR)) {
        cerr << "警告：" << path << " 不是目录，可能导致文件保存失败" << endl;
    }
}

// 模块名称：Qwen2-VL 对话模板模块
//
// 模块职责：
// - 把普通 prompt 包装成 Qwen2-VL/RKLLM 更稳定的 ChatML 形式；
// - 如果调用方已经传入 ChatML，则保持原样，避免重复嵌套；
// - 同时服务文本问答和多模态问答，保证 <image> 标签仍在 user 消息中。
static string build_chat_prompt(const string& prompt) {
    if (prompt.find("<|im_start|>") != string::npos) {
        return prompt;
    }
    return string(PROMPT_TEXT_PREFIX) + prompt + PROMPT_TEXT_POSTFIX;
}

struct CameraLoopOptions {
    int frame_count = 10;
    int interval_ms = 2000;
    bool valid = true;
};

static std::vector<UploadImageItem> camera_shot_upload_images() {
    // 模块化说明：
    // 这里集中定义“板端一次双光采集事件”要上传给PC端Qt的四张图。
    // main.cpp只组织事件级文件清单，不关心这些图片如何采集、融合或绘制。
    return {
        {"rgb", "camera_rgb.jpg"},
        {"ir", "camera_ir.jpg"},
        {"fused", "camera_shot.jpg"},
        {"detected", "camera_detected.jpg"}
    };
}

static CameraLoopOptions parse_camera_loop_options(const std::string& input) {
    CameraLoopOptions options;
    options.frame_count = runtime_config::positiveEnvIntOr(
        "DEMO_CAMERA_LOOP_FRAMES", options.frame_count, 1000);
    options.interval_ms = runtime_config::positiveEnvIntOr(
        "DEMO_CAMERA_LOOP_INTERVAL_MS", options.interval_ms, 600000);

    std::istringstream iss(input);
    std::string command;
    iss >> command;
    if (command != "camera_loop") {
        options.valid = false;
        return options;
    }

    int parsed_frames = options.frame_count;
    int parsed_interval = options.interval_ms;
    if (iss >> parsed_frames) {
        options.frame_count = parsed_frames;
    }
    if (iss >> parsed_interval) {
        options.interval_ms = parsed_interval;
    }

    std::string extra;
    if (iss >> extra || options.frame_count <= 0 || options.interval_ms <= 0) {
        options.valid = false;
    }
    return options;
}

static std::string build_camera_loop_upload_text(int frame_index,
                                                 int total_frames,
                                                 const std::string& detector_text,
                                                 const BorderKgAnalysis* analysis) {
    std::ostringstream oss;
    const bool has_detector_text = detector_text.find_first_not_of(" \t\r\n") != std::string::npos;

    oss << "事件类型：准实时双光融合检测刷新\n";
    if (!has_detector_text) {
        oss << "风险等级：无风险\n";
    } else if (analysis && !analysis->top_risk_level.empty()) {
        oss << "风险等级：" << analysis->top_risk_level << "\n";
    } else {
        oss << "风险等级：低\n";
    }
    oss << "帧序号：" << frame_index << "/" << total_frames << "\n";
    oss << "原因：本帧完成RGB/IR采集、SeaFusion融合、YOLO检测和四图上传。\n";
    oss << "说明：为保证Qt准实时刷新，本循环帧不触发Qwen2-VL大模型推理；";
    oss << "需要详细语义分析时请单独执行 camera_shot。\n\n";
    oss << "[Detector]\n";
    oss << (has_detector_text ? detector_text : "no detector class matched") << "\n";

    if (analysis) {
        oss << "\n[SQLite Knowledge Graph]\n";
        if (!analysis->text.empty()) {
            oss << analysis->text << "\n";
        } else {
            oss << "no matched KG rule\n";
        }
    }
    return oss.str();
}

static int run_camera_loop_upload(rknn_app_context_t* app_ctx,
                                  const runtime_config::RuntimeConfig& config,
                                  BorderKg& kg,
                                  const CameraLoopOptions& options) {
    if (!app_ctx) {
        cerr << "[camera_loop] invalid YOLO context" << endl;
        return -1;
    }
    if (!config.upload.enabled) {
        cerr << "[camera_loop] DEMO_UPLOAD_MODE is offline; Qt will not receive loop frames." << endl;
    }

    cout << "[camera_loop] start frames=" << options.frame_count
         << " interval_ms=" << options.interval_ms << endl;

    int success_count = 0;
    for (int frame_index = 1; frame_index <= options.frame_count; ++frame_index) {
        const auto t0 = std::chrono::steady_clock::now();
        string yolo_str;
        const int ret = process_fused_camera_shot(
            app_ctx, "camera_shot.jpg", "camera_detected.jpg", yolo_str);

        if (ret != 0) {
            cerr << "[camera_loop] frame=" << frame_index
                 << " operation=CaptureFuseDetect status=failed ret=" << ret << endl;
        } else {
            BorderKgAnalysis analysis;
            BorderKgAnalysis* analysis_ptr = nullptr;
            if (kg.available()) {
                analysis = kg.analyze(yolo_str);
                analysis_ptr = &analysis;
            }

            const std::string upload_text = build_camera_loop_upload_text(
                frame_index, options.frame_count, yolo_str, analysis_ptr);

            bool uploaded = true;
            if (config.upload.enabled) {
                uploaded = send_multimodal_info_to_host(
                    config.upload.host, camera_shot_upload_images(), "", upload_text);
            }

            if (uploaded) {
                success_count++;
            }

            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            cout << "[camera_loop] frame=" << frame_index
                 << " upload=" << (uploaded ? "ok" : "failed")
                 << " detector=\"" << yolo_str << "\""
                 << " elapsed_ms=" << elapsed_ms << endl;
        }

        if (frame_index < options.frame_count) {
            usleep(static_cast<useconds_t>(options.interval_ms) * 1000);
        }
    }

    cout << "[camera_loop] finished success=" << success_count
         << "/" << options.frame_count << endl;
    return success_count > 0 ? 0 : -1;
}


// 推理并捕获结果
char* run_inference_and_capture(LLMHandle llmHandle, RKLLMInput* input, RKLLMInferParam* params, size_t* out_len) {
    int stdout_fd = dup(fileno(stdout));
    if (stdout_fd == -1) {
        perror("dup failed");
        return NULL;
    }

    FILE* fp = tmpfile();
    if (!fp) {
        perror("tmpfile failed");
        close(stdout_fd);
        return NULL;
    }

    if (dup2(fileno(fp), fileno(stdout)) == -1) {
        perror("dup2 failed");
        fclose(fp);
        close(stdout_fd);
        return NULL;
    }

    rkllm_run(llmHandle, input, params, NULL);
    fflush(stdout);

    dup2(stdout_fd, fileno(stdout));
    close(stdout_fd);

    fseek(fp, 0, SEEK_END);
    *out_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* result = (char*)malloc(*out_len + 1);
    if (!result) {
        perror("malloc failed");
        fclose(fp);
        return NULL;
    }

    size_t bytes_read = fread(result, 1, *out_len, fp);
    if (bytes_read != *out_len) {
        perror("fread failed");
        free(result);
        fclose(fp);
        return NULL;
    }
    result[*out_len] = '\0';

    fclose(fp);
    printf("%s", result);

    return result;
}

int main()
{
    const runtime_config::RuntimeConfig config = runtime_config::loadRuntimeConfig();

    // 1. 初始化 BorderKG
    BorderKg kg;
    // 尝试打开默认路径或环境变量指定的数据库
    if (!kg.open("")) { 
        cout << "[Warning] BorderKG database not found or failed to open. KG features disabled." << endl;
    } else {
        cout << "[Info] BorderKG initialized successfully." << endl;
    }

    BorderEventAnalyzer analyzer;
    cout << "[CONFIG] Result delivery: "
         << (config.upload.enabled ? "online -> " + config.upload.host
                                   : "offline (local results only)") << endl;
    cout << "[CONFIG] Prompt: Qwen2-VL chat template enabled" << endl;
    cout << "[CONFIG] YOLO model: " << config.yolo_model_path << endl;
    cout << "[CONFIG] Vision model: " << config.vision_model_path << endl;
    cout << "[CONFIG] LLM model: " << config.llm_model_path << endl;
    cout << "[CONFIG] Fusion model: " << config.fusion_model_path << endl;

    // 初始化时创建output目录
    create_directory("output");

    Gps gps;
    bool gps_available = gps.open(config.gps_device, config.gps_baud);
    if (!gps_available) {
        cout << "[Warning] GPS serial not available. GPS features disabled." << endl;
    }
    auto read_gps_once = [&gps, &gps_available, &config]() -> bool {
        if (!gps_available) {
            gps_available = gps.open(config.gps_device, config.gps_baud);
            if (!gps_available) {
                cout << "[Warning] GPS serial not opened." << endl;
                return false;
            }
        }

        GpsInfo gps_info;
        if (gps.readOnce(gps_info, 3000)) {
            Gps::printInfo(gps_info);
            return true;
        }

        cout << "[Warning] GPS read timeout or no fix." << endl;
        return false;
    };
	
    rknn_app_context_t app_ctx;
    image_buffer_t src_image;
 
    int yolov8_ret = 0;

    // 初始化权重
    yolov8_ret = init_yolo_weights(config.yolo_model_path.c_str(), &app_ctx);
    if (yolov8_ret != 0) {
        return yolov8_ret;
    }

    const char* encoder_model_path = config.vision_model_path.c_str();
    const char* llm_model_path = config.llm_model_path.c_str();
    int max_new_tokens = config.max_new_tokens;
    int max_context_len = config.max_context_len;
    int rknn_core_num = config.rknn_core_num;
    cout << "[CONFIG] LLM: max_new_tokens=" << max_new_tokens
         << ", max_context_len=" << max_context_len << endl;

    // 初始化 LLM 参数
    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = llm_model_path;
    param.top_k = 1;
    param.max_new_tokens = max_new_tokens;
    param.max_context_len = max_context_len;
    param.skip_special_token = true;
    param.img_start = "<|vision_start|>";
    param.img_end = "<|vision_end|>";
    param.img_content = "<|image_pad|>";
    param.extend_param.base_domain_id = 1;

    int ret;

    std::chrono::high_resolution_clock::time_point t_start_us = std::chrono::high_resolution_clock::now();

    ret = rkllm_init(&llmHandle, &param, callback);
    if (ret == 0){
        printf("rkllm init success\n");
    } else {
        printf("rkllm init failed\n");
        exit_handler(-1);
    }
    std::chrono::high_resolution_clock::time_point t_load_end_us = std::chrono::high_resolution_clock::now();

    auto load_time = std::chrono::duration_cast<std::chrono::microseconds>(t_load_end_us - t_start_us);
    printf("%s: LLM Model loaded in %8.2f ms\n", __func__, load_time.count() / 1000.0);

    // imgenc初始化
    rknn_app_context_t rknn_app_ctx;
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

    t_start_us = std::chrono::high_resolution_clock::now();

    const int core_num = rknn_core_num;
    ret = init_imgenc(encoder_model_path, &rknn_app_ctx, core_num);
    if (ret != 0) {
        printf("init_imgenc fail! ret=%d model_path=%s\n", ret, encoder_model_path);
        return -1;
    }
    t_load_end_us = std::chrono::high_resolution_clock::now();

    load_time = std::chrono::duration_cast<std::chrono::microseconds>(t_load_end_us - t_start_us);
    printf("%s: ImgEnc Model loaded in %8.2f ms\n", __func__, load_time.count() / 1000.0);

    size_t n_image_tokens = IMAGE_TOKEN_NUM;
    float* img_vec = nullptr;
    
    RKLLMInput rkllm_input;

    // 初始化 infer 参数结构体
    RKLLMInferParam rkllm_infer_params;
    memset(&rkllm_infer_params, 0, sizeof(RKLLMInferParam));
    rkllm_infer_params.mode = RKLLM_INFER_GENERATE;

    // 模块名称：交互命令入口
    //
    // 模块职责：
    // - load/video/camera/camera_shot/gps 负责触发不同采集与推理链路；
    // - 预设问题 [0]/[1] 只负责把用户选择映射成稳定 prompt；
    // - 真正的图像编码、YOLO 检测、SeaFusion 融合、KG 分析都在各自模块内完成。
    //
    // 保留预设问题的原因：
    // 板端比赛演示时，先 camera_shot 或 load 得到当前图像，再输入 0/1 即可复用
    // 当前图片嵌入向量，不需要每次都重新敲完整多模态问题。
    vector<string> pre_input;
    pre_input.push_back("<image>What is in the image?");
    pre_input.push_back("<image>这张图片中有什么？");
    
    cout << "\n*************************************************************************\n" << endl;
    cout << "[load  图片路径] 加载新图片" << endl;
    cout << "[video 视频路径] 检测视频" << endl;
    cout << "[camera] 从摄像头获取视频流并实时检测 (10秒自动结束，DEMO_PREVIEW=1时可ESC退出)" << endl;
    cout << "[camera_shot] 从RGB/IR摄像头拍摄单张融合图片" << endl;
    cout << "[camera_loop [次数] [间隔ms]] 准实时循环采集/融合/检测/四图上传 (默认10帧/2000ms)" << endl;
    cout << "[gps] read GPS once" << endl;
    for (int i = 0; i < (int)pre_input.size(); i++)
    {
        cout << "[" << i << "] " << pre_input[i] << endl;
    }
    cout << "[exit] 退出程序" << endl;  
    cout << "\n*************************************************************************\n" << endl;


    while(true) {
        std::string input_str;
        printf("\n");
        printf("user: ");
        std::getline(std::cin, input_str);
        if (input_str.find_first_not_of(" \t\r\n") == string::npos) {
            continue;
        }
        if (input_str == "exit")
        {
            break;
        }
        if (input_str == "clear")
        {
            continue;
        }

        if (input_str == "gps")
        {
            read_gps_once();
            continue;
        }

        
        // 摄像头视频流处理（10秒自动结束）
        if (input_str == "camera") {
            read_gps_once();
            process_camera_stream(&app_ctx, "camera_detected_video.mp4");
            continue;
        }

        // 摄像头拍照处理
        if (input_str == "camera_shot") {
            string yolo_str;
            int ret = process_fused_camera_shot(&app_ctx, "camera_shot.jpg", "camera_detected.jpg", yolo_str);
            if (ret != 0) {
                cerr << "摄像头融合拍照处理失败" << endl;
                continue;
            }
            read_gps_once();
            
            // 释放旧图像向量
            if (img_vec) {
                delete[] img_vec;
                img_vec = nullptr;
            }
            
            // 处理拍摄的图片生成嵌入向量
            img_vec = process_image("camera_shot.jpg", &rknn_app_ctx, n_image_tokens);
            if (!img_vec) {
                cerr << "处理拍摄的图片失败" << endl;
                continue;
            }
            n_image_tokens = saint_select(img_vec, n_image_tokens, 1536, n_image_tokens);
            n_image_tokens = dart_select(img_vec, n_image_tokens, 1536, n_image_tokens * 2 / 3);
            n_image_tokens = cdpruner_select(img_vec, n_image_tokens, 1536, n_image_tokens * 2 / 3);
            
            cout << "图片拍摄成功，正在进行大模型分析..." << endl;
            
            // --- BorderKG 集成开始 ---
            string final_prompt = "<image>客观分析图中情况。";
            if (kg.available()) {
                // 1. 分析 YOLO 检测到的文本描述
                BorderKgAnalysis analysis = kg.analyze(yolo_str);
                
                // 2. 构建包含 KG 信息的 Prompt
                // raw_prompt 设为空或者基础指令，detector_text 传入 yolo_str
                final_prompt = kg.buildPrompt(final_prompt, yolo_str, analysis);
                
                cout << "[KG] Analysis completed. Rules matched: " << analysis.rules.size() << endl;
            }
            // --- BorderKG 集成结束 ---

            cout << "自动提问: " << final_prompt.substr(0, 100) << "..." << endl; // 打印前100字符预览
            
            // 设置多模态输入。
            //
            // 这里必须先经过 build_chat_prompt()：
            // - final_prompt 里仍然保留 <image>，用于告诉 Qwen2-VL 这是一条图像问题；
            // - ChatML 外壳让 RKLLM 按 user/assistant 对话格式生成，减少裸 prompt
            //   导致的回答不稳定问题。
            string model_prompt = build_chat_prompt(final_prompt);
            rkllm_input.input_type = RKLLM_INPUT_MULTIMODAL;
            rkllm_input.multimodal_input.prompt = (char*)model_prompt.c_str();
            rkllm_input.multimodal_input.image_embed = img_vec;
            rkllm_input.multimodal_input.n_image_tokens = n_image_tokens;
            
            // 执行推理
            printf("robot: ");
            size_t result_len = 0;
            char* inference_result = run_inference_and_capture(llmHandle, &rkllm_input, &rkllm_infer_params, &result_len);

            if (inference_result) {
                printf("\n===== 推理结果信息 =====");
                printf("\n结果长度: %zu 字节\n", result_len);
                
                if (config.upload.enabled) {
                    // camera_shot会在板端生成四张关键图片：
                    // - camera_rgb.jpg：原始可见光输入；
                    // - camera_ir.jpg：原始红外输入；
                    // - camera_shot.jpg：SeaFusion融合图；
                    // - camera_detected.jpg：融合图上的YOLO检测结果。
                    //
                    // 这里走结构化四图上传协议，让PC端Qt四个画面区能一一对应显示。
                    // send_multimodal_info_to_host() 仍会保留旧的顶层 image 字段，
                    // 因此外部旧接收端也能继续至少看到检测结果图。
                    const std::vector<UploadImageItem> upload_images = {
                        {"rgb", "camera_rgb.jpg"},
                        {"ir", "camera_ir.jpg"},
                        {"fused", "camera_shot.jpg"},
                        {"detected", "camera_detected.jpg"}
                    };
                    bool success = send_multimodal_info_to_host(
                        config.upload.host,
                        upload_images,
                        "",
                        inference_result
                    );
                    if (!success) {
                        cerr << "发送 camera_shot 结果失败" << endl;
                    }
                }
                
                free(inference_result);
            }
            continue;
        }

        // P1.5 准实时四图上传：
        //
        // camera_loop 只跑“采集 -> 融合 -> YOLO -> KG轻量文本 -> 四图上传”，
        // 不逐帧触发 Qwen2-VL/RKLLM。这样 Qt 端可以持续刷新画面，避免大模型
        // 推理耗时把演示刷新率拖到不可用。
        //
        // 用法：
        //   camera_loop           默认10帧，每2000ms一帧
        //   camera_loop 20 1000   20帧，每1000ms一帧
        if (input_str == "camera_loop" || input_str.rfind("camera_loop ", 0) == 0) {
            CameraLoopOptions loop_options = parse_camera_loop_options(input_str);
            if (!loop_options.valid) {
                cerr << "用法: camera_loop [次数] [间隔ms]，例如 camera_loop 20 1000" << endl;
                continue;
            }
            read_gps_once();
            run_camera_loop_upload(&app_ctx, config, kg, loop_options);
            continue;
        }

         // 本地视频处理
        if (input_str.substr(0, 6) == "video ") {
            string video_path_str = input_str.substr(6);
            process_local_video(&app_ctx, video_path_str.c_str(), "detected_video.mp4");
            continue;
        }
        

        // 处理加载新图片命令
        if (input_str.substr(0, 5) == "load ") {
            string new_image_path_str = input_str.substr(5);
            const char* new_image_path = new_image_path_str.c_str();
            cout << "正在加载新图片: " << new_image_path << endl;
            
            // 释放旧图像向量
            if (img_vec) {
                delete[] img_vec;
                img_vec = nullptr;
            }
            std::string yolo_str;
            // 检测图片
            yolov8_ret = detect_image(&app_ctx, new_image_path, "output/yolov8_out.png", &src_image, yolo_str);
            cout << "[YOLO] Detection Result: " << yolo_str << endl;

            
            if (yolov8_ret != 0) {
                cerr << "YOLOv8 检测失败: " << yolov8_ret << endl;
            }

            // 处理新图片
            img_vec = process_image(new_image_path, &rknn_app_ctx, n_image_tokens);
            if (!img_vec) {
                cerr << "加载新图片失败，保留当前图片" << endl;
            } else {
                cout << "新图片加载成功!" << endl;
            n_image_tokens = saint_select(img_vec, n_image_tokens, 1536, n_image_tokens);
            n_image_tokens = dart_select(img_vec, n_image_tokens, 1536, n_image_tokens * 2 / 3);
            n_image_tokens = cdpruner_select(img_vec, n_image_tokens, 1536, n_image_tokens * 2 / 3);
                
                // --- BorderKG 集成开始 ---
                string final_prompt = "<image>客观分析图中情况。";
                if (kg.available()) {
                    // 1. 分析 YOLO 检测到的文本描述
                    BorderKgAnalysis analysis = kg.analyze(yolo_str);
                    
                    // 2. 构建包含 KG 信息的 Prompt
                    final_prompt = kg.buildPrompt(final_prompt, yolo_str, analysis);
                    
                    cout << "[KG] Analysis completed. Top Risk: " << (analysis.top_risk_level.empty() ? "None" : analysis.top_risk_level) << endl;
                }
                // --- BorderKG 集成结束 ---

                cout << "自动提问: " << final_prompt.substr(0, 100) << "..." << endl;
                
                // 设置多模态输入，继续复用 Qwen2-VL ChatML 模板。
                string model_prompt = build_chat_prompt(final_prompt);
                rkllm_input.input_type = RKLLM_INPUT_MULTIMODAL;
                rkllm_input.multimodal_input.prompt = (char*)model_prompt.c_str();
                rkllm_input.multimodal_input.image_embed = img_vec;
                rkllm_input.multimodal_input.n_image_tokens = n_image_tokens;
                
                // 执行推理
                printf("robot: ");

                size_t result_len = 0;
                char* inference_result = run_inference_and_capture(llmHandle, &rkllm_input, &rkllm_infer_params, &result_len);

                if (inference_result) {
                    printf("\n===== 推理结果信息 =====");
                    printf("\n结果长度: %zu 字节\n", result_len);
                    
                    if (config.upload.enabled) {
                        bool success = send_info_to_host(
                            config.upload.host,
                            "output/yolov8_out.png",
                            "",
                            inference_result
                        );
                        if (!success) {
                            cerr << "发送 load 图片结果失败" << endl;
                        }
                    }

                    free(inference_result);
                }
            }
            continue;
        }
        
        for (int i = 0; i < (int)pre_input.size(); i++)
        {
            if (input_str == to_string(i))
            {
                input_str = pre_input[i];
                cout << input_str << endl;
            }
        }

        string model_prompt = build_chat_prompt(input_str);
        if (input_str.find("<image>") == string::npos)
        {
            rkllm_input.input_type = RKLLM_INPUT_PROMPT;
            rkllm_input.prompt_input = (char*)model_prompt.c_str();
        } else {
            if (!img_vec) {
                cerr << "错误：没有可用的图片嵌入向量！请先执行 load 或 camera_shot。" << endl;
                continue;
            }
            rkllm_input.input_type = RKLLM_INPUT_MULTIMODAL;
            rkllm_input.multimodal_input.prompt = (char*)model_prompt.c_str();
            rkllm_input.multimodal_input.image_embed = img_vec;
            rkllm_input.multimodal_input.n_image_tokens = n_image_tokens;
        }

        printf("robot: ");
        size_t result_len = 0;
        char* inference_result = run_inference_and_capture(llmHandle, &rkllm_input, &rkllm_infer_params, &result_len);

        if (inference_result) {
            printf("\n===== 推理结果信息 =====");
            printf("\n结果长度: %zu 字节\n", result_len);
            free(inference_result);
        }
    }

    gps.close();

    bool release_success = true;

    // 释放YOLO资源
    int release_ret = release_yolo_resources(&app_ctx);
    if (release_ret != 0) {
        release_success = false;
        cerr << "YOLOv8 释放失败: " << release_ret << endl;
    }

    // 释放其他资源
    if (img_vec) {
        delete[] img_vec;
    }
    ret = release_imgenc(&rknn_app_ctx);
    
    if (ret != 0) {
        release_success = false;
        printf("release_imgenc fail! ret=%d\n", ret);
    }
    rkllm_destroy(llmHandle);

    if (release_success) {
        cout << "资源已释放" << endl;
    }

    return 0;
}
