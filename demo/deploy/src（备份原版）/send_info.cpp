#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static long positive_env_long_or(const char* name, long fallback) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    return end && *end == '\0' && parsed > 0 ? parsed : fallback;
}

// Base64编码辅助函数（支持图片和视频文件）
static std::string file_to_base64(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "错误：无法打开文件 " << path << std::endl;
        return "";
    }

    std::streamsize file_size = file.tellg();
    if (file_size <= 0) {
        std::cerr << "错误：文件为空或无法读取大小 " << path << std::endl;
        file.close();
        return "";
    }
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(file_size);
    if (!file.read(buffer.data(), file_size)) {
        std::cerr << "错误：读取文件失败 " << path << std::endl;
        file.close();
        return "";
    }
    file.close();

    const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string result;
    int i = 0;
    unsigned char char_array_3[3], char_array_4[4];

    for (size_t pos = 0; pos < buffer.size();) {
        char_array_3[i++] = buffer[pos++];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                result += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i > 0) {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = 64;

        for (int j = 0; j < i + 1; j++)
            result += base64_chars[char_array_4[j]];
        while (i++ < 3)
            result += '=';
    }

    if (result.empty()) {
        std::cerr << "错误：文件base64编码失败" << std::endl;
    }
    return result;
}

// 回调函数（处理响应）
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t newLength = size * nmemb;
    try {
        std::string* s = static_cast<std::string*>(userp);
        s->append((char*)contents, newLength);
    } catch (std::bad_alloc& e) {
        return 0;
    }
    return newLength;
}

/**
 * 发送信息到指定主机
 * @param host 主机地址（格式："IP:端口"，例如"192.168.1.1:8000"）
 * @param image_path 图片路径（为空字符串则不发送图片）
 * @param video_path 视频路径（为空字符串则不发送视频）
 * @param text 文本内容
 * @return 发送成功返回true，失败返回false
 */
bool send_info_to_host(const std::string& host, const std::string& image_path, 
                      const std::string& video_path, const std::string& text) {
    // 解析主机地址和端口
    size_t colon_pos = host.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "错误：主机地址格式不正确，应为\"IP:端口\"" << std::endl;
        return false;
    }
    std::string pc_ip = host.substr(0, colon_pos);
    std::string port_str = host.substr(colon_pos + 1);
    int pc_port;
    try {
        pc_port = std::stoi(port_str);
    } catch (...) {
        std::cerr << "错误：端口号格式不正确" << std::endl;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "错误：初始化curl失败" << std::endl;
        return false;
    }

    // 构建JSON数据
    json j;
    j["text"] = text;

    // 处理图片（路径为空则不添加图片字段）
    if (!image_path.empty()) {
        std::string image_base64 = file_to_base64(image_path);
        if (!image_base64.empty()) {
            j["image"] = image_base64;
        } else {
            std::cerr << "警告：图片处理失败，跳过图片发送" << std::endl;
        }
    }

    // 处理视频（路径为空则不添加视频字段）
    if (!video_path.empty()) {
        std::string video_base64 = file_to_base64(video_path);
        if (!video_base64.empty()) {
            j["video"] = video_base64;
            // 提取视频文件扩展名（如.mp4、.avi）
            size_t dot_pos = video_path.find_last_of(".");
            if (dot_pos != std::string::npos) {
                j["video_ext"] = video_path.substr(dot_pos);
            } else {
                j["video_ext"] = ".mp4"; // 默认扩展名
            }
        } else {
            std::cerr << "警告：视频处理失败，跳过视频发送" << std::endl;
        }
    }

    std::string post_data = j.dump();

    // 检查数据大小（视频可能较大，调整为50MB限制）
    if (post_data.size() > 1024 * 1024 * 50) {
        std::cerr << "错误：数据过大（超过50MB），发送失败" << std::endl;
        curl_easy_cleanup(curl);
        return false;
    }

    // 设置请求参数
    std::string url = "http://" + pc_ip + ":" + std::to_string(pc_port) + "/receive";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data.size());

    // 设置请求头
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Connection: close");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 处理响应
    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                     positive_env_long_or("DEMO_UPLOAD_CONNECT_TIMEOUT_SEC", 3L));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,
                     positive_env_long_or("DEMO_UPLOAD_TIMEOUT_SEC", 60L));

    // 执行请求
    CURLcode res = curl_easy_perform(curl);
    bool success = (res == CURLE_OK);
    
    // 检查HTTP响应码
    if (success) {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if (response_code != 200) {
            std::cerr << "错误：服务器返回状态码 " << response_code << std::endl;
            success = false;
        } else {
            std::cout << "服务器接收成功，响应：" << response << std::endl;
        }
    } else {
        std::cerr << "错误：curl请求失败，原因：" << curl_easy_strerror(res) << std::endl;
    }
    
    // 清理资源
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    
    return success;
}
