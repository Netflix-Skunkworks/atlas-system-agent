#pragma once

#include <spdlog/spdlog.h>
#include <vector>

namespace atlasagent {

class LogManager {
 public:
  LogManager() noexcept;
  std::shared_ptr<spdlog::logger> Logger() noexcept;

 private:
  std::shared_ptr<spdlog::logger> logger_;
};

LogManager& log_manager() noexcept;

inline std::shared_ptr<spdlog::logger> Logger() noexcept { return log_manager().Logger(); }

}  // namespace atlasagent
