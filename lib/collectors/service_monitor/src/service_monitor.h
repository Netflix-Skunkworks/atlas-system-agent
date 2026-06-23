#pragma once

// The service monitor publishes per-service metrics for the systemd units matched by the configured
// regexes. For each unit it reports the main PID's RSS / FDs / CPU% (scope="process") and the whole
// cgroup's memory / FDs / CPU% (scope="service"), plus the unit's active/sub state.
//
// CPU% is tracked by CpuRateTracker (below): one tracker per counter owns its baseline and the
// identity (PID or cgroup path) the counter was read from, and advances itself in one update() call.
// Bundling the identity with the baseline makes restarts safe, and because the collect functions
// skip update() on a failed read, a missed sample preserves the prior baseline rather than
// corrupting the next delta.

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <thirdparty/spectator-cpp/spectator/registry.h>

#include "cpu_rate_tracker.h"
#include "service_monitor_utils.h"

struct ServiceMonitorConstants
{
    static constexpr auto DefaultMonitoredServices{10};
    static constexpr auto GaugeTTLSeconds{60};
    static constexpr auto ConfigPath{"/etc/atlas-system-agent/conf.d"};
    static constexpr auto RssName{"systemd.service.rss"};        // main PID resident set size
    static constexpr auto MemoryName{"systemd.service.memory"};  // whole-service cgroup memory.current
    static constexpr auto FdsName{"systemd.service.fds"};
    static constexpr auto CpuUsageName{"systemd.service.cpuUsage"};
    static constexpr auto ServiceStatusName{"systemd.service.status"};
};

namespace detail
{

// Unscoped gauge: the metric name itself carries the distinction (e.g. RssName is always the main
// PID, MemoryName is always the whole cgroup), so no scope tag is needed.
inline auto gauge(Registry* registry, const std::string_view name, const std::string_view serviceName)
{
    auto tags = std::unordered_map<std::string, std::string>{{"service.name", fmt::format("{}", serviceName)}};
    return registry->CreateGauge(std::string(name), tags, ServiceMonitorConstants::GaugeTTLSeconds);
}

// Scoped gauge: the same metric name is published for both the main PID ("process") and the whole
// cgroup ("service"); the scope tag tells them apart.
inline auto gaugeScoped(Registry* registry, const std::string_view name, const std::string_view serviceName,
                        const std::string_view scope)
{
    auto tags = std::unordered_map<std::string, std::string>{{"service.name", fmt::format("{}", serviceName)},
                                                             {"scope", fmt::format("{}", scope)}};
    return registry->CreateGauge(std::string(name), tags, ServiceMonitorConstants::GaugeTTLSeconds);
}

inline auto gaugeServiceState(Registry* registry, const std::string_view name, const std::string_view serviceName,
                              const std::string_view state)
{
    auto tags = std::unordered_map<std::string, std::string>{{"service.name", fmt::format("{}", serviceName)}};
    tags.emplace("state", fmt::format("{}", state));
    return registry->CreateGauge(std::string(name), tags, ServiceMonitorConstants::GaugeTTLSeconds);
}
}  // namespace detail

// The two independent CPU counters a service exposes: its main PID and its whole cgroup. Held in one
// map entry per service so both advance under the same key.
struct ServiceCpuState
{
    CpuRateTracker<unsigned int> process;  // keyed by main PID
    CpuRateTracker<std::string> cgroup;    // keyed by control-group path
};

class ServiceMonitor
{
   public:
    ServiceMonitor(Registry* registry, std::vector<std::regex> config, unsigned int max_services);
    ~ServiceMonitor() = default;

    // Abide by the C++ rule of 5
    ServiceMonitor(const ServiceMonitor& other) = delete;
    ServiceMonitor& operator=(const ServiceMonitor& other) = delete;
    ServiceMonitor(ServiceMonitor&& other) = delete;
    ServiceMonitor& operator=(ServiceMonitor&& other) = delete;

    bool gather_metrics();

   private:
    bool init_monitored_services();
    bool update_metrics();

    // Publish an optional metric. When the value is present it sets the gauge to value * scale;
    // when absent it logs errMsg and reports failure -- unless errMsg is empty, in which case the
    // absence is silently treated as success (used for CPU on the first cycle / across a gap, where
    // having no sample yet is normal rather than an error). An empty scope publishes an unscoped
    // gauge; a non-empty scope adds the "scope" tag.
    template <typename T>
    bool publish_metric(const std::string& service, std::optional<T> val, double scale, std::string_view name,
                        std::string_view scope, std::string_view errMsg = {}) const;

    // Publish the per-main-PID metrics (rss, process-scope fds, process-scope cpu) and advance the
    // process CPU tracker. Returns whether every metric this cycle was collected successfully. const:
    // the only mutated state is the caller-owned tracker passed by reference, not a member of this.
    bool collect_process_metrics(const std::string& service, const ServiceProperties& props,
                                 std::chrono::steady_clock::time_point now, CpuRateTracker<unsigned int>& cpu) const;
    // Publish the whole-cgroup metrics (memory, summed fds, service-scope cpu) and advance the cgroup
    // CPU tracker. Returns whether every metric this cycle was collected successfully. const for the
    // same reason as collect_process_metrics.
    bool collect_cgroup_metrics(const std::string& service, const ServiceProperties& props,
                                std::chrono::steady_clock::time_point now, CpuRateTracker<std::string>& cpu) const;

    Registry* registry_;
    std::vector<std::regex> config_;
    unsigned int maxMonitoredServices{};
    std::unordered_map<std::string, ServiceCpuState> cpuState_{};
    long clkTck_{};    // sysconf(_SC_CLK_TCK) -- converts /proc/[pid]/stat jiffies to microseconds
    long pageSize_{};  // sysconf(_SC_PAGESIZE) -- converts RSS pages to bytes
    bool initSuccess{false};
    std::vector<std::string> monitoredServices_{};
};
