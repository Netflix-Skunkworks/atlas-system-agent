#pragma once

#include "../../Files/files.h"
#include "../../Logger/logger.h"
#include "../../spectator/id.h"
#include <fmt/format.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/filereadstream.h>
#include <string_view>

namespace atlasagent {

enum class TagRuleOp { Prefix, Name };

class Tagger {
 public:
  static auto Nop() -> Tagger { return Tagger{{}}; }

  static auto FromJson(const rapidjson::Document& doc) -> std::optional<Tagger> {
    auto logger = Logger();
    if (!doc.IsObject()) {
      logger->error("Invalid config file. Should be a JSON object");
      return {};
    }

    std::vector<Tagger::Rule> rules;
    if (doc.HasMember("tag_rules")) {
      auto jsonRules = doc["tag_rules"].GetArray();
      for (const auto& jsonRule : jsonRules) {
        auto op = TagRuleOp::Name;
        std::string match;
        if (jsonRule.HasMember("prefix")) {
          op = TagRuleOp::Prefix;
          match = jsonRule["prefix"].GetString();
        } else if (jsonRule.HasMember("name")) {
          match = jsonRule["name"].GetString();
        } else {
          logger->error("Invalid tag rule. Need 'name' or 'prefix'");
          return {};
        }

        // parse tags
        if (!jsonRule.HasMember("tags")) {
          logger->error("Rule with {} = {} must include tags to add", op, match);
          return {};
        }

        spectator::Tags tags;
        auto jsonTags = jsonRule["tags"].GetObject();
        for (const auto& kv : jsonTags) {
          auto k = kv.name.GetString();
          auto v = kv.value.GetString();
          tags.add(k, v);
        }

        rules.emplace_back(op, match, std::move(tags));
      }
    }

    return Tagger{std::move(rules)};
  }

  static auto FromConfigFile(const char* file) -> std::optional<Tagger> {
    if (access(file, R_OK) == -1) {
      return Tagger::Nop();
    }

    StdIoFile fp{file};
    if (!fp) {
      return {};
    }

    rapidjson::Document doc;
    char buf[64 * 1024];
    rapidjson::FileReadStream is(fp, buf, sizeof buf);
    doc.ParseStream(is);
    if (doc.HasParseError()) {
      Logger()->error("Invalid config file. Parse error at offset {}: {}", doc.GetErrorOffset(),
                      rapidjson::GetParseError_En(doc.GetParseError()));
      return {};
    }

    return FromJson(doc);
  }

  class Rule {
   public:
    Rule(TagRuleOp op, std::string_view match, spectator::Tags tags)
        : op_{op}, match_{match}, tags_{std::move(tags)} {}

    [[nodiscard]] auto Op() const -> TagRuleOp { return op_; }
    [[nodiscard]] auto Match() const -> const std::string& { return match_; }
    [[nodiscard]] auto Tags() const -> const spectator::Tags& { return tags_; }

    auto operator==(const Rule& that) const -> bool {
      return op_ == that.op_ && match_ == that.match_ && tags_ == that.tags_;
    }

    [[nodiscard]] auto Matches(const spectator::Id& id) const -> bool {
      if (op_ == TagRuleOp::Name) {
        return id.Name() == match_;
      }
      // startsWith()
      return id.Name().rfind(match_, 0) == 0;
    }

   private:
    TagRuleOp op_;
    std::string match_;
    spectator::Tags tags_;
  };

  explicit Tagger(std::vector<Rule> rules) : rules_{std::move(rules)} {}
  Tagger(const Tagger&) = default;
  Tagger(Tagger&&) = default;
  Tagger& operator=(Tagger&&) = default;
  Tagger& operator=(const Tagger&) = default;
  ~Tagger() = default;

  [[nodiscard]] auto GetRules() const -> const std::vector<Rule>& { return rules_; }

  auto operator==(const Tagger& that) const -> bool { return that.GetRules() == rules_; }

  [[nodiscard]] auto GetId(spectator::IdPtr id) const -> spectator::IdPtr {
    for (const auto& rule : rules_) {
      if (rule.Matches(*id)) {
        return id->WithTags(rule.Tags());
      }
    }
    return id;
  }

  [[nodiscard]] auto GetId(absl::string_view name, spectator::Tags tags = {}) const -> spectator::IdPtr {
    return GetId(spectator::Id::of(name, std::move(tags)));
  }

 private:
  std::vector<Rule> rules_;
};
}  // namespace atlasagent

// for debugging
template <> struct fmt::formatter<atlasagent::TagRuleOp>: formatter<std::string_view> {
  static auto format(const atlasagent::TagRuleOp& op, format_context& ctx) -> format_context::iterator {
    return fmt::format_to(ctx.out(), "op={}", op == atlasagent::TagRuleOp::Name ? "name" : "prefix");
  }
};

template <> struct fmt::formatter<atlasagent::Tagger::Rule>: formatter<std::string_view> {
  static auto format(const atlasagent::Tagger::Rule& rule, format_context& ctx) -> format_context::iterator {
    return fmt::format_to(ctx.out(), "rule({} -> {}, tags={})", rule.Op(), rule.Match(), rule.Tags());
  }
};

template <> struct fmt::formatter<atlasagent::Tagger>: formatter<std::string_view> {
  static auto format(const atlasagent::Tagger& tagger, format_context& ctx) -> format_context::iterator {
    return fmt::format_to(ctx.out(), "tagger(rules={})", tagger.GetRules());
  }
};
