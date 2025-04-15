#pragma once

#include "../spectator/registry.h"
#include "absl/time/clock.h"

namespace atlasagent {

namespace detail {

inline absl::string_view path_from(absl::string_view url) noexcept {
  if (url.empty()) {
    return "/";
  }

  auto proto_end = std::find(url.begin(), url.end(), ':');
  if (proto_end == url.end()) {
    return url;  // no protocol, assume just a path
  }

  std::string protocol = &*(proto_end);
  if (protocol.length() < 3) {
    return url;
  }
  proto_end += 3;  // skip over ://

  auto path_begin = std::find(proto_end, url.end(), '/');
  if (path_begin == url.end()) {
    return "/";
  }

  auto query_begin = std::find(path_begin, url.end(), '?');
  return absl::string_view{path_begin, static_cast<size_t>(query_begin - path_begin)};
}

}  // namespace detail

template <typename Reg>
class LogEntry {
 public:
  LogEntry(Reg* registry, const std::string& method, const std::string& url)
      : registry_{registry},
        start_{absl::Now()},
        id_{spectator::Id::of("ipc.client.call", {{"owner", "spectator-cpp"},
                                                  {"ipc.endpoint", detail::path_from(url)},
                                                  {"http.method", method},
                                                  {"http.status", "-1"}})} {}

  [[nodiscard]] absl::Time start() const { return start_; }

  void log() {
    auto timer = registry_->GetPercentileTimer(id_, absl::Milliseconds(1), absl::Seconds(10));
    timer->Record(absl::Now() - start_);
  }

  void set_status_code(int code) { id_ = id_->WithTag("http.status", absl::StrCat(code)); }

  void set_attempt(int attempt_number, bool is_final) {
    id_ = id_->WithTag("ipc.attempt", attempt(attempt_number))
              ->WithTag("ipc.attempt.final", is_final ? "true" : "false");
  }

  void set_error(const std::string& error) {
    id_ = id_->WithTag("ipc.result", "failure")->WithTag("ipc.status", error);
  }

  void set_success() {
    static std::string ipc_success("success");
    id_ = id_->WithTag("ipc.status", ipc_success)->WithTag("ipc.result", ipc_success);
  }

 private:
  Reg* registry_;
  absl::Time start_;
  spectator::IdPtr id_;

  static const std::string& attempt(int attempt_number) {
    static std::string initial = "initial";
    static std::string second = "second";
    static std::string third_up = "third_up";

    switch (attempt_number) {
      case 0:
        return initial;
      case 1:
        return second;
      default:
        return third_up;
    }
  }
};

}  // namespace atlasagent
