#pragma once

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include "absl/time/time.h"

namespace atlasagent {

class LogManager {
 public:
  LogManager() noexcept;
  std::shared_ptr<spdlog::logger> Logger() noexcept;
  std::shared_ptr<spdlog::logger> GetLogger(const std::string& name) noexcept;

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

LogManager& log_manager() noexcept;

inline std::shared_ptr<spdlog::logger> Logger() noexcept { return log_manager().Logger(); }
inline std::shared_ptr<spdlog::logger> GetLogger(const std::string& name) noexcept {
  return log_manager().GetLogger(name);
}

}  // namespace atlasagent

template <>
struct fmt::formatter<absl::Time> {
  constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.begin(); }
  template <typename FormatContext>
  auto format(const absl::Time& t, FormatContext& context) {
    return fmt::format_to(context.out(), absl::FormatTime(t));
  }
};
