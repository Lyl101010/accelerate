#ifndef BORDER_KG_H
#define BORDER_KG_H

#include <memory>
#include <string>
#include <vector>

struct BorderKgEntity {
    std::string entity_id;
    std::string name_cn;
    std::string name_en;
    std::string type_id;
    double risk_weight;
    int count;

    BorderKgEntity();
};

struct BorderKgRule {
    std::string rule_id;
    std::string name_cn;
    std::string result_cn;
    std::string risk_level;
    std::string action_cn;
    std::string explanation_cn;
    double score;
    int upload_keyframe;
    int upload_video_clip;
    int trigger_cloud_reasoning;
    int local_alarm;

    BorderKgRule();
};

struct BorderKgAnalysis {
    bool db_available;
    bool has_entities;
    std::string detector_text;
    std::string top_risk_level;
    std::string text;
    std::string prompt_context;
    std::vector<BorderKgEntity> entities;
    std::vector<BorderKgRule> rules;
    std::vector<std::string> relation_hints;

    BorderKgAnalysis();
};

class BorderKg {
public:
    BorderKg();
    ~BorderKg();

    bool open(const std::string& db_path = std::string());
    bool available() const;
    std::string dbPath() const;

    BorderKgAnalysis analyze(const std::string& detector_text) const;
    std::string buildPrompt(const std::string& raw_prompt,
                            const std::string& detector_text,
                            const BorderKgAnalysis& analysis) const;
    std::string buildUploadText(const std::string& vlm_text,
                                const BorderKgAnalysis& analysis) const;

    static std::string defaultDbPath();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
