#ifndef BORDER_EVENT_ANALYZER_H
#define BORDER_EVENT_ANALYZER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

// 模块名称：边境事件轻量规则解释模块（BorderEventAnalyzer）
//
// 模块职责：
// 1. 把 YOLO/知识图谱阶段得到的英文关键词整理成中文事件描述。
// 2. 对“士兵 + 枪支 + 盾牌”“平民 + 边境线”等组合做轻量规则判断。
// 3. 作为 SQLite KG 之外的兜底解释层，便于后续在没有命中数据库规则时
//    仍然生成可读的中文事件线索。
//
// 模块边界：
// - 本模块只处理关键词到事件文本的规则转换；
// - 不负责目标检测，不读取摄像头，不调用 RKNN/RKLLM；
// - 不负责 SQLite 查询，SQLite 证据链仍由 BorderKg 模块负责。
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
