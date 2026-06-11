#ifndef SEND_INFO_H
#define SEND_INFO_H

#include <string>

bool send_info_to_host(const std::string& host, const std::string& image_path, 
                      const std::string& video_path, const std::string& text);
#endif // SEND_INFO_H