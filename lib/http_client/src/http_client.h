#pragma once

#include <absl/time/time.h>
#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace atlasagent {

class CurlHeaders;

struct HttpClientConfig {
  absl::Duration connect_timeout;
  absl::Duration read_timeout;
};

using HttpHeaders = std::unordered_map<std::string, std::string>;
struct HttpResponse {
  int status;
  std::string raw_body;
  HttpHeaders headers;
};

class HttpClient {
 public:
  HttpClient(Registry* registry,HttpClientConfig config) : config_{config}, registry_{registry} {}

  [[nodiscard]] HttpResponse Get(const std::string& url) const {
    return perform("GET", url, std::make_shared<CurlHeaders>(), nullptr, 0u, 0);
  }

  [[nodiscard]] HttpResponse Get(const std::string& url,
                                 const std::vector<std::string>& headers) const {
    return method_header("GET", url, headers);
  }

  [[nodiscard]] HttpResponse Put(const std::string& url,
                                 const std::vector<std::string>& headers) const {
    return method_header("PUT", url, headers);
  }

  static void GlobalInit() noexcept;
  static void GlobalShutdown() noexcept;

 private:
  HttpClientConfig config_;
  Registry* registry_{nullptr};

  HttpResponse perform(const char* method, const std::string& url,
                       std::shared_ptr<CurlHeaders> headers, const char* payload, size_t size,
                       uint32_t attempt_number) const;

  HttpResponse method_header(const char* method, const std::string& url,
                             const std::vector<std::string>& headers) const;
};

}  // namespace atlasagent