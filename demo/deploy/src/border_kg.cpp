#include "border_kg.h"

#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

/**
 * @brief 构造函数，初始化 BorderKgEntity 对象。
 * 
 * 将风险权重（risk_weight）初始化为 0.0，
 * 将计数（count）初始化为 0。
 */
BorderKgEntity::BorderKgEntity()
    : risk_weight(0.0), count(0)
{
}

BorderKgRule::BorderKgRule()
    : score(0.0),
      upload_keyframe(0),
      upload_video_clip(0),
      trigger_cloud_reasoning(0),
      local_alarm(0)
{
}

BorderKgAnalysis::BorderKgAnalysis()
    : db_available(false), has_entities(false)
{
}

namespace {

static std::string getenv_string_or(const char* name, const char* fallback)
{
    const char* value = getenv(name);
    return (value && value[0] != '\0') ? std::string(value) : std::string(fallback);
}

static std::string text_column(sqlite3_stmt* stmt, int col)
{
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : "";
}

static std::string normalize_text(const std::string& text)
{
    std::string out;
    bool last_space = true;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        bool sep = ch == '_' || ch == '-' || ch == ',' || ch == ';' ||
                   ch == '|' || ch == '/' || ch == '\\' || ch == '\t' ||
                   ch == '\r' || ch == '\n' || ch == '(' || ch == ')' ||
                   ch == '[' || ch == ']' || ch == '{' || ch == '}';
        if (sep || std::isspace(ch)) {
            if (!last_space) {
                out.push_back(' ');
                last_space = true;
            }
            continue;
        }

        if (ch < 128) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else {
            out.push_back(static_cast<char>(ch));
        }
        last_space = false;
    }

    if (!out.empty() && out[out.size() - 1] == ' ') {
        out.erase(out.size() - 1);
    }
    return out;
}

static std::vector<std::string> split_tokens(const std::string& text)
{
    std::vector<std::string> tokens;
    std::stringstream ss(text);
    std::string token;
    while (ss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

static int risk_rank(const std::string& risk_level)
{
    if (risk_level == "critical") return 4;
    if (risk_level == "high") return 3;
    if (risk_level == "medium") return 2;
    if (risk_level == "low") return 1;
    return 0;
}

struct Phrase {
    std::vector<std::string> tokens;
    std::string entity_id;
    std::string phrase;
};

struct RuleCandidate {
    std::string object_entity_id;
    std::string behavior_id;
    std::string location_entity_id;
};

static bool compare_phrase(const Phrase& a, const Phrase& b)
{
    if (a.tokens.size() != b.tokens.size()) {
        return a.tokens.size() > b.tokens.size();
    }
    return a.phrase.size() > b.phrase.size();
}

static bool compare_rule(const BorderKgRule& a, const BorderKgRule& b)
{
    int ar = risk_rank(a.risk_level);
    int br = risk_rank(b.risk_level);
    if (ar != br) {
        return ar > br;
    }
    return a.score > b.score;
}

} // namespace

class BorderKg::Impl {
public:
    Impl() : db_(NULL), time_period_(getenv_string_or("DEMO_TIME_PERIOD", "day")) {}

    ~Impl()
    {
        close();
    }

    bool open(const std::string& db_path)
    {
        close();
        db_path_ = db_path.empty() ? BorderKg::defaultDbPath() : db_path;

        int ret = sqlite3_open_v2(db_path_.c_str(), &db_, SQLITE_OPEN_READONLY, NULL);
        if (ret != SQLITE_OK) {
            std::cerr << "[KG] open sqlite db failed: " << db_path_
                      << " err=" << (db_ ? sqlite3_errmsg(db_) : "unknown") << std::endl;
            close();
            return false;
        }

        if (!loadEntities() || !loadDetectorMappings() || !loadAliases()) {
            close();
            return false;
        }

        rebuildPhraseIndex();
        std::cout << "[KG] sqlite loaded: " << db_path_ << std::endl;
        return true;
    }

    bool available() const
    {
        return db_ != NULL;
    }

    std::string dbPath() const
    {
        return db_path_;
    }

    BorderKgAnalysis analyze(const std::string& detector_text) const
    {
        BorderKgAnalysis analysis;
        analysis.db_available = available();
        analysis.detector_text = detector_text;

        if (!available()) {
            analysis.text = "[KG] sqlite db unavailable, skip KG inference.";
            analysis.prompt_context = analysis.text;
            return analysis;
        }

        std::map<std::string, int> counts = parseDetectorEntities(detector_text);
        for (std::map<std::string, int>::const_iterator it = counts.begin(); it != counts.end(); ++it) {
            std::map<std::string, BorderKgEntity>::const_iterator ent_it = entities_.find(it->first);
            if (ent_it == entities_.end()) {
                continue;
            }
            BorderKgEntity info = ent_it->second;
            info.count = it->second;
            analysis.entities.push_back(info);
        }

        analysis.has_entities = !analysis.entities.empty();
        if (!analysis.has_entities) {
            analysis.text = "[KG] no detector class matched sqlite KG mapping.";
            analysis.prompt_context = analysis.text;
            return analysis;
        }

        analysis.rules = inferRules(counts);
        std::sort(analysis.rules.begin(), analysis.rules.end(), compare_rule);
        if (analysis.rules.size() > 3) {
            analysis.rules.resize(3);
        }
        if (!analysis.rules.empty()) {
            analysis.top_risk_level = analysis.rules[0].risk_level;
        }

        analysis.relation_hints = queryRelationHints(counts, analysis.rules.empty() ? 5 : 2);
        buildAnalysisText(analysis);
        return analysis;
    }

private:
    sqlite3* db_;
    std::string db_path_;
    std::string time_period_;
    std::map<std::string, BorderKgEntity> entities_;
    std::map<std::string, std::string> phrase_to_entity_;
    std::vector<Phrase> phrase_index_;

    void close()
    {
        if (db_) {
            sqlite3_close(db_);
            db_ = NULL;
        }
        entities_.clear();
        phrase_to_entity_.clear();
        phrase_index_.clear();
    }

    bool loadEntities()
    {
        const char* sql =
            "SELECT entity_id, name_cn, name_en, type_id, risk_weight "
            "FROM entities";
        sqlite3_stmt* stmt = NULL;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) {
            std::cerr << "[KG] prepare entities failed: " << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BorderKgEntity info;
            info.entity_id = text_column(stmt, 0);
            info.name_cn = text_column(stmt, 1);
            info.name_en = text_column(stmt, 2);
            info.type_id = text_column(stmt, 3);
            info.risk_weight = sqlite3_column_double(stmt, 4);
            entities_[info.entity_id] = info;

            addPhrase(info.name_en, info.entity_id);
            addPhrase(info.name_cn, info.entity_id);
        }
        sqlite3_finalize(stmt);
        return !entities_.empty();
    }

    bool loadDetectorMappings()
    {
        const char* sql =
            "SELECT detector_class, entity_id "
            "FROM detector_class_mapping";
        sqlite3_stmt* stmt = NULL;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) {
            std::cerr << "[KG] prepare detector mappings failed: " << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string detector_class = text_column(stmt, 0);
            std::string entity_id = text_column(stmt, 1);
            addPhrase(detector_class, entity_id);

            std::string underscore = detector_class;
            std::replace(underscore.begin(), underscore.end(), ' ', '_');
            addPhrase(underscore, entity_id);
        }
        sqlite3_finalize(stmt);
        return true;
    }

    bool loadAliases()
    {
        const char* sql =
            "SELECT alias, entity_id "
            "FROM aliases";
        sqlite3_stmt* stmt = NULL;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) {
            std::cerr << "[KG] prepare aliases failed: " << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            addPhrase(text_column(stmt, 0), text_column(stmt, 1));
        }
        sqlite3_finalize(stmt);

        addPhrase("boundary_tablet", "E_BOUNDARY_TABLET");
        addPhrase("civilian_car", "E_CIVILIAN_CAR");
        addPhrase("armored_car", "E_ARMORED_CAR");
        addPhrase("border_line", "E_BORDERLINE");
        return true;
    }

    void addPhrase(const std::string& phrase, const std::string& entity_id)
    {
        if (phrase.empty() || entity_id.empty()) {
            return;
        }
        std::string normalized = normalize_text(phrase);
        if (!normalized.empty()) {
            phrase_to_entity_[normalized] = entity_id;
        }
    }

    void rebuildPhraseIndex()
    {
        phrase_index_.clear();
        for (std::map<std::string, std::string>::const_iterator it = phrase_to_entity_.begin();
             it != phrase_to_entity_.end(); ++it) {
            Phrase phrase;
            phrase.phrase = it->first;
            phrase.entity_id = it->second;
            phrase.tokens = split_tokens(it->first);
            if (!phrase.tokens.empty()) {
                phrase_index_.push_back(phrase);
            }
        }
        std::sort(phrase_index_.begin(), phrase_index_.end(), compare_phrase);
    }

    std::map<std::string, int> parseDetectorEntities(const std::string& detector_text) const
    {
        std::map<std::string, int> counts;
        std::vector<std::string> tokens = split_tokens(normalize_text(detector_text));

        for (size_t i = 0; i < tokens.size();) {
            const Phrase* matched = NULL;
            for (size_t p = 0; p < phrase_index_.size(); ++p) {
                const Phrase& phrase = phrase_index_[p];
                if (phrase.tokens.size() > tokens.size() - i) {
                    continue;
                }

                bool ok = true;
                for (size_t j = 0; j < phrase.tokens.size(); ++j) {
                    if (tokens[i + j] != phrase.tokens[j]) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    matched = &phrase;
                    break;
                }
            }

            if (matched) {
                counts[matched->entity_id]++;
                i += matched->tokens.size();
            } else {
                ++i;
            }
        }

        return counts;
    }

    bool hasEntity(const std::map<std::string, int>& counts, const std::string& entity_id) const
    {
        std::map<std::string, int>::const_iterator it = counts.find(entity_id);
        return it != counts.end() && it->second > 0;
    }

    int entityCount(const std::map<std::string, int>& counts, const std::string& entity_id) const
    {
        std::map<std::string, int>::const_iterator it = counts.find(entity_id);
        return it == counts.end() ? 0 : it->second;
    }

    void addCandidate(std::vector<RuleCandidate>& candidates,
                      std::set<std::string>& seen,
                      const std::map<std::string, int>& counts,
                      const std::string& object_entity_id,
                      const std::string& behavior_id,
                      const std::string& location_entity_id) const
    {
        if (!hasEntity(counts, object_entity_id) || !hasEntity(counts, location_entity_id)) {
            return;
        }

        std::string key = object_entity_id + "|" + behavior_id + "|" + location_entity_id;
        if (seen.find(key) != seen.end()) {
            return;
        }
        seen.insert(key);

        RuleCandidate candidate;
        candidate.object_entity_id = object_entity_id;
        candidate.behavior_id = behavior_id;
        candidate.location_entity_id = location_entity_id;
        candidates.push_back(candidate);
    }

    std::vector<std::string> detectedLocations(const std::map<std::string, int>& counts) const
    {
        std::vector<std::string> locations;
        for (std::map<std::string, int>::const_iterator it = counts.begin(); it != counts.end(); ++it) {
            std::map<std::string, BorderKgEntity>::const_iterator ent_it = entities_.find(it->first);
            if (ent_it != entities_.end() && ent_it->second.type_id == "LOCATION") {
                locations.push_back(it->first);
            }
        }
        return locations;
    }

    std::vector<BorderKgRule> inferRules(const std::map<std::string, int>& counts) const
    {
        std::vector<RuleCandidate> candidates;
        std::set<std::string> seen;

        addCandidate(candidates, seen, counts, "E_CIVILIAN", "B_CARRY", "E_GUN");
        addCandidate(candidates, seen, counts, "E_SOLDIER", "B_CARRY", "E_GUN");
        addCandidate(candidates, seen, counts, "E_ARMORED_CAR", "B_ESCORT", "E_SOLDIER");
        addCandidate(candidates, seen, counts, "E_SHIELD", "B_APPEAR", "E_SOLDIER");
        addCandidate(candidates, seen, counts, "E_FLAG", "B_APPEAR", "E_SOLDIER");

        std::vector<std::string> locations = detectedLocations(counts);
        for (size_t i = 0; i < locations.size(); ++i) {
            const std::string& loc = locations[i];
            if (entityCount(counts, "E_CIVILIAN") >= 3) {
                addCandidate(candidates, seen, counts, "E_CIVILIAN", "B_GATHER", loc);
            }
            addCandidate(candidates, seen, counts, "E_CIVILIAN", "B_APPROACH", loc);
            addCandidate(candidates, seen, counts, "E_CIVILIAN_CAR", "B_APPROACH", loc);
            addCandidate(candidates, seen, counts, "E_ARMORED_CAR", "B_APPEAR", loc);
            addCandidate(candidates, seen, counts, "E_SOLDIER", "B_PATROL", loc);
            addCandidate(candidates, seen, counts, "E_NON_VEHICLE", "B_APPEAR", loc);
        }

        std::vector<BorderKgRule> rules;
        std::set<std::string> rule_seen;
        for (size_t i = 0; i < candidates.size(); ++i) {
            BorderKgRule rule;
            if (queryRule(candidates[i], &rule) && rule_seen.insert(rule.rule_id).second) {
                rules.push_back(rule);
            }
        }
        return rules;
    }

    bool queryRule(const RuleCandidate& candidate, BorderKgRule* rule) const
    {
        const char* sql =
            "SELECT rt.rule_id, rt.name_cn, rt.result_cn, rt.risk_level, rt.score, "
            "       rt.action_cn, COALESCE(rt.explanation_template_cn, ''), "
            "       COALESCE(ep.upload_keyframe, 0), COALESCE(ep.upload_video_clip, 0), "
            "       COALESCE(ep.trigger_cloud_reasoning, 0), COALESCE(ep.local_alarm, 0) "
            "FROM rule_templates rt "
            "LEFT JOIN escalation_policy ep ON ep.risk_level = rt.risk_level "
            "WHERE rt.enabled = 1 "
            "  AND rt.object_entity_id = ? "
            "  AND rt.behavior_id = ? "
            "  AND rt.location_entity_id = ? "
            "  AND (rt.time_period = 'any' OR rt.time_period = ?) "
            "ORDER BY CASE WHEN rt.time_period = ? THEN 1 ELSE 0 END DESC, rt.score DESC "
            "LIMIT 1";

        sqlite3_stmt* stmt = NULL;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) {
            std::cerr << "[KG] prepare rule query failed: " << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        sqlite3_bind_text(stmt, 1, candidate.object_entity_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, candidate.behavior_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, candidate.location_entity_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, time_period_.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, time_period_.c_str(), -1, SQLITE_TRANSIENT);

        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            rule->rule_id = text_column(stmt, 0);
            rule->name_cn = text_column(stmt, 1);
            rule->result_cn = text_column(stmt, 2);
            rule->risk_level = text_column(stmt, 3);
            rule->score = sqlite3_column_double(stmt, 4);
            rule->action_cn = text_column(stmt, 5);
            rule->explanation_cn = text_column(stmt, 6);
            rule->upload_keyframe = sqlite3_column_int(stmt, 7);
            rule->upload_video_clip = sqlite3_column_int(stmt, 8);
            rule->trigger_cloud_reasoning = sqlite3_column_int(stmt, 9);
            rule->local_alarm = sqlite3_column_int(stmt, 10);
            found = true;
        }
        sqlite3_finalize(stmt);
        return found;
    }

    std::vector<std::string> queryRelationHints(const std::map<std::string, int>& counts, size_t limit) const
    {
        std::vector<std::string> hints;
        const char* sql =
            "SELECT COALESCE(r.description, ''), se.name_cn, r.predicate, oe.name_cn "
            "FROM relations r "
            "JOIN entities se ON se.entity_id = r.subject_entity_id "
            "JOIN entities oe ON oe.entity_id = r.object_entity_id "
            "WHERE r.subject_entity_id = ? "
            "  AND (r.object_entity_id = ? OR oe.type_id = 'RISK') "
            "ORDER BY r.weight DESC "
            "LIMIT 1";

        for (std::map<std::string, int>::const_iterator a = counts.begin(); a != counts.end(); ++a) {
            for (std::map<std::string, int>::const_iterator b = counts.begin(); b != counts.end(); ++b) {
                sqlite3_stmt* stmt = NULL;
                if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) {
                    return hints;
                }
                sqlite3_bind_text(stmt, 1, a->first.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, b->first.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    std::string desc = text_column(stmt, 0);
                    if (desc.empty()) {
                        desc = text_column(stmt, 1) + text_column(stmt, 2) + text_column(stmt, 3);
                    }
                    if (std::find(hints.begin(), hints.end(), desc) == hints.end()) {
                        hints.push_back(desc);
                    }
                }
                sqlite3_finalize(stmt);
                if (hints.size() >= limit) {
                    return hints;
                }
            }
        }
        return hints;
    }

    void buildAnalysisText(BorderKgAnalysis& analysis) const
    {
        std::stringstream ss;
        ss << "[KG] detector: " << analysis.detector_text << "\n";
        ss << "[KG] entities: ";
        for (size_t i = 0; i < analysis.entities.size(); ++i) {
            const BorderKgEntity& ent = analysis.entities[i];
            ss << ent.name_cn << "(" << ent.entity_id << ")x" << ent.count;
            if (i + 1 < analysis.entities.size()) {
                ss << ", ";
            }
        }
        ss << "\n";

        if (!analysis.rules.empty()) {
            ss << "[KG] matched rules:\n";
            for (size_t i = 0; i < analysis.rules.size(); ++i) {
                const BorderKgRule& rule = analysis.rules[i];
                ss << "  - " << rule.rule_id << " " << rule.name_cn
                   << " | risk=" << rule.risk_level
                   << " | score=" << rule.score
                   << " | result=" << rule.result_cn << "\n";
                if (!rule.explanation_cn.empty()) {
                    ss << "    explain=" << rule.explanation_cn << "\n";
                }
                if (!rule.action_cn.empty()) {
                    ss << "    action=" << rule.action_cn << "\n";
                }
            }
        } else {
            ss << "[KG] matched rules: none\n";
        }

        if (!analysis.relation_hints.empty()) {
            ss << "[KG] relation hints:\n";
            for (size_t i = 0; i < analysis.relation_hints.size(); ++i) {
                ss << "  - " << analysis.relation_hints[i] << "\n";
            }
        }

        analysis.text = ss.str();
        analysis.prompt_context = analysis.text;
    }
};

BorderKg::BorderKg()
    : impl_(new Impl())
{
}

BorderKg::~BorderKg()
{
}

bool BorderKg::open(const std::string& db_path)
{
    return impl_->open(db_path);
}

bool BorderKg::available() const
{
    return impl_->available();
}

std::string BorderKg::dbPath() const
{
    return impl_->dbPath();
}

BorderKgAnalysis BorderKg::analyze(const std::string& detector_text) const
{
    return impl_->analyze(detector_text);
}

std::string BorderKg::buildPrompt(const std::string& raw_prompt,
                                  const std::string& detector_text,
                                  const BorderKgAnalysis& analysis) const
{
    if (!analysis.db_available || analysis.prompt_context.empty()) {
        return raw_prompt;
    }

    std::stringstream ss;
    ss << raw_prompt << "\n\n";
    ss << "[Detector]\n" << detector_text << "\n\n";
    ss << "[SQLite Knowledge Graph]\n" << analysis.prompt_context << "\n";
    ss << "Use the image as primary evidence and the SQLite KG as local prior. "
       << "Answer in Chinese with event type, risk level, reason, and action suggestion.";
    return ss.str();
}

std::string BorderKg::buildUploadText(const std::string& vlm_text,
                                      const BorderKgAnalysis& analysis) const
{
    std::stringstream ss;
    if (analysis.db_available && !analysis.text.empty()) {
        ss << analysis.text << "\n";
    }
    ss << "[VLM]\n" << vlm_text;
    return ss.str();
}

std::string BorderKg::defaultDbPath()
{
    const char* env_path = getenv("DEMO_KG_DB");
    if (env_path && env_path[0] != '\0') {
        return std::string(env_path);
    }

    const char* candidates[] = {
        "border_kg_snapshot.db",
        "model/border_kg_snapshot.db",
        "src/border_kg_snapshot.db",
        "../src/border_kg_snapshot.db",
        "deploy/src/border_kg_snapshot.db"
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (access(candidates[i], R_OK) == 0) {
            return std::string(candidates[i]);
        }
    }
    return "border_kg_snapshot.db";
}
