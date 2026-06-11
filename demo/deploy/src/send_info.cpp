#include "send_info.h"

#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

const size_t kMaxUploadPayloadBytes = 1024 * 1024 * 50;

std::string basename_of(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string extension_of(const std::string& path, const std::string& fallback) {
    const size_t pos = path.find_last_of(".");
    return pos == std::string::npos ? fallback : path.substr(pos);
}

}  // namespace

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

static bool append_video_payload(json& j, const std::string& video_path) {
    if (video_path.empty()) {
        return true;
    }

    std::string video_base64 = file_to_base64(video_path);
    if (video_base64.empty()) {
        std::cerr << "警告：视频处理失败，跳过视频发送" << std::endl;
        return false;
    }

    j["video"] = video_base64;
    j["video_ext"] = extension_of(video_path, ".mp4");
    return true;
}

static bool post_json_to_host(const std::string& host, const json& j) {
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

    std::string post_data = j.dump();

    // 检查数据大小。四图上传仍然走同一保护阈值，避免误传超大文件卡死演示链路。
    if (post_data.size() > kMaxUploadPayloadBytes) {
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);
    bool success = (res == CURLE_OK);

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

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return success;
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

    append_video_payload(j, video_path);
    return post_json_to_host(host, j);
}

bool send_multimodal_info_to_host(const std::string& host,
                                  const std::vector<UploadImageItem>& images,
                                  const std::string& video_path,
                                  const std::string& text) {
    json j;
    j["text"] = text;

    json image_map = json::object();
    json image_file_map = json::object();
    std::string legacy_primary_image;

    // 四图上传协议：
    // images.rgb      -> 原始可见光图 camera_rgb.jpg
    // images.ir       -> 原始红外图 camera_ir.jpg
    // images.fused    -> SeaFusion融合图 camera_shot.jpg
    // images.detected -> 检测结果图 camera_detected.jpg
    //
    // 同时保留顶层 image 字段，优先使用 detected，兼容旧PC接收端。
    for (const UploadImageItem& item : images) {
        if (item.role.empty() || item.path.empty()) {
            continue;
        }

        const std::string encoded = file_to_base64(item.path);
        if (encoded.empty()) {
            std::cerr << "警告：图片处理失败，跳过 role=" << item.role
                      << " path=" << item.path << std::endl;
            continue;
        }

        image_map[item.role] = encoded;
        image_file_map[item.role] = basename_of(item.path);
        if (item.role == "detected" || legacy_primary_image.empty()) {
            legacy_primary_image = encoded;
        }
    }

    if (!image_map.empty()) {
        j["images"] = image_map;
        j["image_files"] = image_file_map;
        j["image"] = legacy_primary_image;
    }

    append_video_payload(j, video_path);
    return post_json_to_host(host, j);
}
