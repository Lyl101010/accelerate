#include "border_event_analyzer.h"
#include <algorithm>
#include <sstream>
#include <set>

// 判断是否为地点关键词
bool BorderEventAnalyzer::isLocationKeyword(const std::string& keyword) {
    return std::find(locationKeywords.begin(), locationKeywords.end(), keyword)
           != locationKeywords.end();
}

// 分离地点关键词和物体关键词
void BorderEventAnalyzer::separateKeywords(const std::vector<std::string>& keywords,
                                           std::vector<std::string>& locations,
                                           std::vector<std::string>& objects) {
    for (const auto& kw : keywords) {
        if (isLocationKeyword(kw)) {
            locations.push_back(kw);
        } else {
            objects.push_back(kw);
        }
    }
}

// 优化物体描述（合并相关物体）
std::string BorderEventAnalyzer::optimizeObjectDesc(const std::vector<std::string>& objects) {

    // 转换为中文并去重
    std::set<std::string> uniqueObjects;
    for (const auto& obj : objects) {
        auto it = keywordMap.find(obj);
        if (it != keywordMap.end()) {
            uniqueObjects.insert(it->second);
        } 
    }

    // 特殊组合处理：士兵、枪、盾的组合
    bool hasSoldier = uniqueObjects.count("士兵") > 0;
    bool hasGun = uniqueObjects.count("枪支") > 0;
    bool hasShield = uniqueObjects.count("盾牌") > 0;
    bool hasCivilian = uniqueObjects.count("平民") > 0;

    // 处理士兵+枪+盾的组合
    if (hasSoldier && hasGun && hasShield) {
        std::string desc = "持枪、持盾士兵";
        // 移除已处理的关键词
        uniqueObjects.erase("士兵");
        uniqueObjects.erase("枪支");
        uniqueObjects.erase("盾牌");

        // 如果有平民，特殊处理
        if (hasCivilian) {
            desc += "及平民";
            uniqueObjects.erase("平民");
        }

        // 处理剩余物体
        if (!uniqueObjects.empty()) {
            desc += "及";
            size_t count = 0;
            for (const auto& item : uniqueObjects) {
                desc += item;
                if (++count < uniqueObjects.size()) {
                    desc += "、";
                }
            }
        }
        return desc;
    }

    // 士兵+枪组合
    if (hasSoldier && hasGun) {
        std::string desc = "持枪士兵";
        uniqueObjects.erase("士兵");
        uniqueObjects.erase("枪支");

        // 处理剩余物体
        if (!uniqueObjects.empty()) {
            desc += "及";
            size_t count = 0;
            for (const auto& item : uniqueObjects) {
                desc += item;
                if (++count < uniqueObjects.size()) {
                    desc += "、";
                }
            }
        }
        return desc;
    }

    // 普通组合处理
    std::string desc;
    size_t count = 0;
    for (const auto& item : uniqueObjects) {
        desc += item;
        if (++count < uniqueObjects.size()) {
            desc += "、";
        }
    }
    return desc;
}

// 核心判断逻辑
std::string BorderEventAnalyzer::determineEventType(const std::vector<std::string>& keywords) {
    bool hasSoldier = std::find(keywords.begin(), keywords.end(), "soldier") != keywords.end();
    bool hasGun = std::find(keywords.begin(), keywords.end(), "gun") != keywords.end();
    bool hasShield = std::find(keywords.begin(), keywords.end(), "shield") != keywords.end();
    bool hasCivilian = std::find(keywords.begin(), keywords.end(), "civilian") != keywords.end() ||
                       std::find(keywords.begin(), keywords.end(), "civilian_car") != keywords.end();
    bool hasPatrolEquip = std::find(keywords.begin(), keywords.end(), "flag") != keywords.end() ||
                          std::find(keywords.begin(), keywords.end(), "armored_car") != keywords.end();
    bool hasOutPost = std::find(keywords.begin(), keywords.end(), "outpost") != keywords.end() ||
                          std::find(keywords.begin(), keywords.end(), "borderline") != keywords.end();
    // 冲突情况
    if ((hasSoldier && hasGun && hasShield && hasCivilian) || 
        (hasGun && hasCivilian && hasOutPost)||(hasGun && hasCivilian &&hasSoldier)) {
        return "边境冲突";
    }
    if (hasSoldier && hasGun && hasShield) {
        return "武装对峙";
    }
    if (hasGun && hasCivilian && !hasSoldier) {
        return "平民武装冲突";
    }

    // 巡逻情况增强
    if ((hasSoldier&& hasOutPost)||(hasSoldier && hasPatrolEquip)||(hasSoldier && hasGun) ) {
        return "巡逻";
    }
    
    // 偷渡情况
    if (hasCivilian && !hasSoldier && hasOutPost)  {
        return "偷渡";
    }
	 if (hasGun && !hasSoldier && !hasCivilian) {
        return "武器遗弃";
    }

    return "未发生异常事件";
}

// 获取边境地点的中文描述
std::string BorderEventAnalyzer::getBorderLocationDesc(const std::vector<std::string>& locations) {
    if (locations.empty()) return "";

    std::set<std::string> uniqueLocations;
    for (const auto& loc : locations) {
        auto it = keywordMap.find(loc);
        if (it != keywordMap.end()) {
            uniqueLocations.insert(it->second);
        }
    }

    if (uniqueLocations.empty()) return "";

    std::string desc = "在";
    size_t count = 0;
    for (const auto& loc : uniqueLocations) {
        desc += loc;
        if (++count < uniqueLocations.size()) {
            desc += "、";
        }
    }
    return desc;
}

// 分析多个关键词，返回中文句子
std::string BorderEventAnalyzer::analyzeKeywords(const std::vector<std::string>& keywords) {
    // 先判断是否有异常事件类型
    std::string eventType = determineEventType(keywords);
    
    // 如果是无异常情况，直接返回简化信息
    if (eventType == "未发生异常事件") {
        return eventType;
    }

    // 分离地点和物体
    std::vector<std::string> locations, objects;
    separateKeywords(keywords, locations, objects);

    // 处理地点描述
    std::string locationDesc = getBorderLocationDesc(locations);

    // 处理物体描述
    std::string objectDesc = optimizeObjectDesc(objects);

    // 构建最终句子（仅用于有异常的情况）
    std::stringstream ss;

    if (!locationDesc.empty()) {
        ss << locationDesc << "出现了" << objectDesc;
    } else {
        ss << "发现了" << objectDesc;
    }

    ss << "，可能发生了" << eventType << "事件。";

    return ss.str();
}

// 获取所有支持的关键词
std::vector<std::string> BorderEventAnalyzer::getSupportedKeywords() const {
    std::vector<std::string> keywords;
    for (const auto& pair : keywordMap) {
        keywords.push_back(pair.first);
    }
    return keywords;
}