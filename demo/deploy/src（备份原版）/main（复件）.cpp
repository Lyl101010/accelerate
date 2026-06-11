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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <opencv2/opencv.hpp>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "image_enc.h"
#include "rkllm.h"
#include "yolov8.h"
#include "file_utils.h"
#include "image_drawing.h"
#include "yolov8_utils.h"
#include "send_info.h"
#include "border_event_analyzer.h"

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
        if (mkdir(path.c_str(), 0777) != 0) {
            cerr << "警告：无法创建目录 " << path << "，可能导致文件保存失败" << endl;
        }
    } else if (!(info.st_mode & S_IFDIR)) {
        cerr << "警告：" << path << " 不是目录，可能导致文件保存失败" << endl;
    }
}

// 图像正方形扩展
cv::Mat expand2square(const cv::Mat& img, const cv::Scalar& background_color) {
    int width = img.cols;
    int height = img.rows;

    if (width == height) {
        return img.clone();
    }

    int size = std::max(width, height);
    cv::Mat result(size, size, img.type(), background_color);

    int x_offset = (size - width) / 2;
    int y_offset = (size - height) / 2;

    cv::Rect roi(x_offset, y_offset, width, height);
    img.copyTo(result(roi));

    return result;
}

// 处理图像并生成嵌入向量
float* process_image(const char* image_path, rknn_app_context_t* rknn_app_ctx, size_t& n_image_tokens) {
    cv::Mat img = cv::imread(image_path);
    if (img.empty()) {
        cerr << "无法加载图片: " << image_path << endl;
        return nullptr;
    }
    cv::cvtColor(img, img, cv::COLOR_BGR2RGB);

    cv::Scalar background_color(127.5, 127.5, 127.5);
    cv::Mat square_img = expand2square(img, background_color);

    cv::Mat resized_img;
    cv::Size new_size(IMAGE_WIDTH, IMAGE_HEIGHT);
    cv::resize(square_img, resized_img, new_size, 0, 0, cv::INTER_LINEAR);

    size_t image_embed_len = EMBED_SIZE;
    int rkllm_image_embed_len = IMAGE_TOKEN_NUM * image_embed_len;
    float* img_vec = new float[rkllm_image_embed_len];
    
    int ret = run_imgenc(rknn_app_ctx, resized_img.data, img_vec);
    if (ret != 0) {
        cerr << "run_imgenc 失败! ret=" << ret << endl;
        delete[] img_vec;
        return nullptr;
    }
    
    n_image_tokens = IMAGE_TOKEN_NUM;
    return img_vec;
}

std::vector<std::string> splitKeywords(const std::string& input) {
    std::vector<std::string> keywords;
    std::stringstream ss(input);
    std::string token;
    
    while (ss >> token) {
        if (!token.empty()) {
            keywords.push_back(token);
        }
    }
    return keywords;
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
    BorderEventAnalyzer analyzer;

    // 初始化时创建output目录
    create_directory("output");
	
    rknn_app_context_t app_ctx;
    image_buffer_t src_image;
 
    int yolov8_ret = 0;

    // 初始化权重
    yolov8_ret = init_yolo_weights("self_best_i8_ver01.rknn", &app_ctx);
    if (yolov8_ret != 0) {
        return yolov8_ret;
    }

    const char* image_path = "model/bus.jpg";          
    const char* encoder_model_path = "Qwen2-VL-2B_vision_rk3588.rknn";
    const char* llm_model_path = "qwen2_vl_2b_instruct.rkllm";         
    int max_new_tokens = 512;                        
    int max_context_len = 512;                      
    int rknn_core_num = 3;                           

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

    // 初始图像处理
    size_t n_image_tokens = IMAGE_TOKEN_NUM;
    float* img_vec = process_image(image_path, &rknn_app_ctx, n_image_tokens);
    if (!img_vec) {
        cerr << "初始图像处理失败!" << endl;
        return -1;
    }
    
    RKLLMInput rkllm_input;

    // 初始化 infer 参数结构体
    RKLLMInferParam rkllm_infer_params;
    memset(&rkllm_infer_params, 0, sizeof(RKLLMInferParam));
    rkllm_infer_params.mode = RKLLM_INFER_GENERATE;
    // RKLLM API compatibility: rkllm_infer_params.keep_history = 0;
    // RKLLM API compatibility: rkllm_set_chat_template(llmHandle, "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n", "<|im_start|>user\n", "<|im_end|>\n<|im_start|>assistant\n");
    vector<string> pre_input;
    pre_input.push_back("<image>What is in the image?");
    pre_input.push_back("<image>这张图片中有什么？");
  

    cout << "\n*************************************************************************\n" << endl;
    cout << "[load  图片路径] 加载新图片" << endl;
    cout << "[video 视频路径] 检测视频" << endl;
    cout << "[camera] 从摄像头获取视频流 (ESC退出，10秒自动结束)" << endl;
    cout << "[camera_shot] 从摄像头拍摄单张图片" << endl;


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
        if (input_str == "exit")
        {
            break;
        }
        if (input_str == "clear")
        {
    // RKLLM API compatibility: ret = rkllm_clear_kv_cache(llmHandle, 1, nullptr, nullptr);
            if (ret != 0)
            {
                printf("clear kv cache failed!\n");
            }
            continue;
        }

        
        // 摄像头视频流处理（10秒自动结束）
        if (input_str == "camera") {
            process_camera_stream(&app_ctx, "camera_detected_video.mp4");
            continue;
        }

        // 摄像头拍照处理
        if (input_str == "camera_shot") {
            string yolo_str;
            int ret = process_camera_shot(&app_ctx, "camera_shot.jpg", "camera_detected.jpg", yolo_str);
            if (ret != 0) {
                cerr << "摄像头拍照处理失败" << endl;
                continue;
            }
            
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
            
            cout << "图片拍摄成功，正在进行大模型分析..." << endl;
            string prompt = "<image>客观分析图中情况。";
            cout << "自动提问: " << prompt << endl;
            
            // 设置多模态输入
            rkllm_input.input_type = RKLLM_INPUT_MULTIMODAL;
    // RKLLM API compatibility: rkllm_input.role = "user";
            rkllm_input.multimodal_input.prompt = (char*)prompt.c_str();
            rkllm_input.multimodal_input.image_embed = img_vec;
            rkllm_input.multimodal_input.n_image_tokens = n_image_tokens;
    // RKLLM API compatibility: rkllm_input.multimodal_input.n_image = 1;
    // RKLLM API compatibility: rkllm_input.multimodal_input.image_height = IMAGE_HEIGHT;
    // RKLLM API compatibility: rkllm_input.multimodal_input.image_width = IMAGE_WIDTH;
            // 执行推理
            printf("robot: ");
            size_t result_len = 0;
            char* inference_result = run_inference_and_capture(llmHandle, &rkllm_input, &rkllm_infer_params, &result_len);

            if (inference_result) {
                printf("\n===== 推理结果信息 =====");
                printf("\n结果长度: %zu 字节\n", result_len);
                
                // 发送结果
                bool success = send_info_to_host(
                    "10.135.0.99:8000",  
                    "camera_detected.jpg",   
                    "",                      
                    inference_result
                );
                
                free(inference_result);
            }
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
            yolov8_ret = detect_image(&app_ctx, new_image_path, "output/yolov8_out.png", &src_image,yolo_str);
            cout << yolo_str << endl;

            
            if (yolov8_ret != 0) {
                cerr << "YOLOv8 检测失败: " << yolov8_ret << endl;
            }

            // 处理新图片
            img_vec = process_image(new_image_path, &rknn_app_ctx, n_image_tokens);
            if (!img_vec) {
                cerr << "加载新图片失败，保留当前图片" << endl;
            } else {
                cout << "新图片加载成功!" << endl;
                
                input_str = "<image>客观分析图中情况。";
                cout << "自动提问: " << input_str << endl;
                
                // 设置多模态输入
                rkllm_input.input_type = RKLLM_INPUT_MULTIMODAL;
    // RKLLM API compatibility: rkllm_input.role = "user";
                rkllm_input.multimodal_input.prompt = (char*)input_str.c_str();
                rkllm_input.multimodal_input.image_embed = img_vec;
                rkllm_input.multimodal_input.n_image_tokens = n_image_tokens;
    // RKLLM API compatibility: rkllm_input.multimodal_input.n_image = 1;
    // RKLLM API compatibility: rkllm_input.multimodal_input.image_height = IMAGE_HEIGHT;
    // RKLLM API compatibility: rkllm_input.multimodal_input.image_width = IMAGE_WIDTH;
                // 执行推理
                printf("robot: ");

                size_t result_len = 0;
                char* inference_result = run_inference_and_capture(llmHandle, &rkllm_input, &rkllm_infer_params, &result_len);

                if (inference_result) {
                    printf("\n===== 推理结果信息 =====");
                    printf("\n结果长度: %zu 字节\n", result_len);
                    
                    bool success = send_info_to_host(
                        "10.135.0.99:8000",  
                        "output/yolov8_out.png",  
                        "",
                        inference_result    
                    );

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
        if (input_str.find("<image>") == std::string::npos) 
        {
            rkllm_input.input_type = RKLLM_INPUT_PROMPT;
    // RKLLM API compatibility: rkllm_input.role = "user";
            rkllm_input.prompt_input = (char*)input_str.c_str();
        } else {
            if (!img_vec) {
                cerr << "错误：没有可用的图片嵌入向量！" << endl;
                continue;
            }
            rkllm_input.input_type = RKLLM_INPUT_MULTIMODAL;
    // RKLLM API compatibility: rkllm_input.role = "user";
            rkllm_input.multimodal_input.prompt = (char*)input_str.c_str();
            rkllm_input.multimodal_input.image_embed = img_vec;
            rkllm_input.multimodal_input.n_image_tokens = n_image_tokens;
    // RKLLM API compatibility: rkllm_input.multimodal_input.n_image = 1;
    // RKLLM API compatibility: rkllm_input.multimodal_input.image_height = IMAGE_HEIGHT;
    // RKLLM API compatibility: rkllm_input.multimodal_input.image_width = IMAGE_WIDTH;
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

    // 释放YOLO资源
    int release_ret = release_yolo_resources(&app_ctx);
    if (release_ret != 0) {
        cerr << "YOLOv8 释放失败: " << yolov8_ret << endl;
    }
    else
    {
        cerr << "YOLOv8 释放成功 " << endl;
    }

    // 释放其他资源
    if (img_vec) {
        delete[] img_vec;
    }
    ret = release_imgenc(&rknn_app_ctx);
    
    if (ret != 0) {
        printf("release_imgenc fail! ret=%d\n", ret);
    }
    rkllm_destroy(llmHandle);

    return 0;
}
