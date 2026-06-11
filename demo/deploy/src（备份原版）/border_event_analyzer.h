#ifndef BORDER_EVENT_ANALYZER_H
#define BORDER_EVENT_ANALYZER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

class BorderEventAnalyzer {
private:
    // 关键词到中文的映射
    const std::unordered_map<std::string, std::string> keywordMap = {
        {"borderline", "边境线"},
        {"civilian", "平民"},
        {"soldier", "士兵"},
        {"boundary_tablet", "界碑"},
        {"gun", "枪支"},
        {"flag", "旗帜"},
        {"shield", "盾牌"},
        {"civilian_car", "民用车辆"},
        {"armored_car", "装甲车"},
        {"outpost", "前哨站"}
    };

    // 地点关键词列表（单独区分，避免重复）
    const std::vector<std::string> locationKeywords = {
        "borderline", "boundary_tablet", "outpost"
    };

    // 判断是否为地点关键词
    bool isLocationKeyword(const std::string& keyword);

    // 分离地点关键词和物体关键词
    void separateKeywords(const std::vector<std::string>& keywords,
                          std::vector<std::string>& locations,
                          std::vector<std::string>& objects);

    // 优化物体描述（合并相关物体，如士兵 + 枪支→持枪士兵）
    std::string optimizeObjectDesc(const std::vector<std::string>& objects);

    // 核心判断逻辑
    std::string determineEventType(const std::vector<std::string>& keywords);

    // 获取边境地点的中文描述
    std::string getBorderLocationDesc(const std::vector<std::string>& locations);

public:
    // 分析多个关键词，返回中文句子
    std::string analyzeKeywords(const std::vector<std::string>& keywords);

    // 获取所有支持的关键词
    std::vector<std::string> getSupportedKeywords() const;
};

#endif // BORDER_EVENT_ANALYZER_H