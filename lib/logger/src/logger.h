#pragma once

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <absl/time/time.h>

namespace atlasagent
{

class LogManager
{
   public:
    LogManager() noexcept;
    std::shared_ptr<spdlog::logger> Logger() noexcept;
    std::shared_ptr<spdlog::logger> GetLogger(const std::string& name) noexcept;

   private:
    std::shared_ptr<spdlog::logger> logger_;
};

LogManager& log_manager() noexcept;

inline std::shared_ptr<spdlog::logger> Logger() noexcept { return log_manager().Logger(); }
inline std::shared_ptr<spdlog::logger> GetLogger(const std::string& name) noexcept
{
    return log_manager().GetLogger(name);
}

}  // namespace atlasagent

template <>
struct fmt::formatter<absl::Time> : formatter<std::string_view>
{
    static auto format(const absl::Time& t, format_context& ctx) -> format_context::iterator
    {
        return fmt::format_to(ctx.out(), absl::FormatTime(t));
    }
};
