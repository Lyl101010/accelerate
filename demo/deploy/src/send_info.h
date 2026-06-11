#ifndef SEND_INFO_H
#define SEND_INFO_H

#include <string>
#include <vector>

// 单张上传图片的结构化描述。
//
// 模块化功能说明：
// - role 是跨模块协议字段，例如 rgb / ir / fused / detected；
// - path 是板端本地文件路径；
// - send_info 模块只负责读取文件并序列化上传，不关心图片来自相机、融合还是检测。
struct UploadImageItem {
    std::string role;
    std::string path;
};

bool send_info_to_host(const std::string& host, const std::string& image_path, 
                      const std::string& video_path, const std::string& text);

bool send_multimodal_info_to_host(const std::string& host,
                                  const std::vector<UploadImageItem>& images,
                                  const std::string& video_path,
                                  const std::string& text);
#endif // SEND_INFO_H
