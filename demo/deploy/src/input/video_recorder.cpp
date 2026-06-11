#include "input/video_recorder.h"

#include <iostream>
#include <vector>

namespace input {
namespace {

struct RecorderCandidate {
    std::string path;
    std::string codec_name;
    int fourcc;
};

// 将首选输出路径改成 .avi。
//
// MP4 在板端常常依赖 FFmpeg/H.264/MP4V 等编码支持；AVI + MJPG 通常更容易被
// 精简版 OpenCV 写出。这里不直接修改调用方传入的路径，而是生成一个备用路径。
std::string replaceExtensionWithAvi(const std::string& path) {
    const std::string::size_type slash_pos = path.find_last_of("/\\");
    const std::string::size_type dot_pos = path.find_last_of('.');

    if (dot_pos == std::string::npos ||
        (slash_pos != std::string::npos && dot_pos < slash_pos)) {
        return path + ".avi";
    }

    return path.substr(0, dot_pos) + ".avi";
}

std::vector<RecorderCandidate> buildCandidates(const std::string& preferred_path) {
    std::vector<RecorderCandidate> candidates;

    // 首选仍然保留原来的 MP4V + 原路径，兼容有 MP4 编码支持的板端镜像。
    candidates.push_back({
        preferred_path,
        "MP4V",
        cv::VideoWriter::fourcc('M', 'P', '4', 'V')
    });

    // 备用使用 AVI + MJPG。这个组合文件更大，但对 OpenCV 精简构建更友好，
    // 适合作为比赛现场和 SSH 调试时的证据视频。
    candidates.push_back({
        replaceExtensionWithAvi(preferred_path),
        "MJPG",
        cv::VideoWriter::fourcc('M', 'J', 'P', 'G')
    });

    return candidates;
}

}  // 匿名命名空间

bool openVideoRecorder(cv::VideoWriter& writer,
                       const std::string& preferred_path,
                       double fps,
                       const cv::Size& frame_size,
                       VideoRecorderInfo* info) {
    writer.release();

    const std::vector<RecorderCandidate> candidates = buildCandidates(preferred_path);
    for (const RecorderCandidate& candidate : candidates) {
        writer.open(candidate.path, candidate.fourcc, fps, frame_size, true);
        if (writer.isOpened()) {
            if (info) {
                info->path = candidate.path;
                info->codec = candidate.codec_name;
            }
            return true;
        }

        // 每个候选只打印一次失败原因，避免像逐帧 rknn_run 那样刷屏。
        std::cerr << "视频保存候选不可用: path=" << candidate.path
                  << ", codec=" << candidate.codec_name << std::endl;
    }

    if (info) {
        info->path.clear();
        info->codec.clear();
    }
    return false;
}

}  // 命名空间 input
