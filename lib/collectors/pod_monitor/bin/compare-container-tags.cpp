// Standalone debug tool, not part of atlas_system_agent and not run by ctest. It runs one PodMonitor
// refresh -- the same cgroup discovery, kubelet identity fetch, and reconcile k8s-agent performs --
// then walks every tracked pod and each of its containers, reading the common tags PodMonitor applied
// to that container's CGroup: the tags merged into every metric the container publishes. For each pod
// it prints the tags that are the same on all of its containers, and the tags that differ (or are not
// set on some), with every container's value. A per-tag summary table closes the report.
//
// Registry-level common tags (k8s-agent.cpp's metric-type=pod-container) are attached by the Registry
// to every pod metric alike, so they are not part of the comparison. Nothing is published: the
// Registry uses spectator-cpp's in-memory writer.
//
// Usage: compare-container-tags [cgroup_path_prefix] [filtered]  (either order; both optional)
// "filtered" enables debug logging so excluded entries are visible. Export the agent's K8S_CLUSTER
// first so k8s.cluster.name matches what the agent publishes.

#include <lib/collectors/pod_monitor/src/pod_monitor.h>
#include <lib/http_client/src/http_client.h>
#include <lib/logger/src/logger.h>

#include <thirdparty/spectator-cpp/spectator/registry.h>

#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/async.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

using TagMap = std::unordered_map<std::string, std::string>;

constexpr std::size_t kRuleWidth = 80;
// crictl's truncated container-id length, so an id can be pasted straight into `crictl inspect`.
constexpr std::size_t kShortIdLength = 13;

// Exposes PodMonitor's protected read-only tracked view. After Refresh(), each tracked container's
// CGroup holds exactly the tags PodMonitor merges into that container's metrics.
class TrackedPodMonitor final : public atlasagent::PodMonitor
{
   public:
    using PodMonitor::PodMonitor;
    using PodMonitor::TrackedPods;
};

struct ContainerView
{
    std::string id;
    std::string name;
    // Points into the PodMonitor's tracked state, which is not modified after the views are built.
    const TagMap* tags;
};

struct PodView
{
    std::string uid;
    std::string name;
    std::string pod_namespace;
    // Sorted by container name, then id.
    std::vector<ContainerView> containers;
};

// A tag carried with the same value by every container of a pod.
struct SharedTag
{
    std::string key;
    std::string value;
};

// A tag whose value differs between a pod's containers, or that some of them do not carry. values[i]
// belongs to PodView::containers[i]; nullopt means that container does not carry the tag.
struct DifferingTag
{
    std::string key;
    std::vector<std::optional<std::string>> values;
};

// Both lists are sorted by tag key.
struct TagComparison
{
    std::vector<SharedTag> shared;
    std::vector<DifferingTag> differing;
};

// Applies ANSI styles only when stdout is a terminal and NO_COLOR (https://no-color.org) is unset or
// empty, so piped or redirected output stays plain text. Callers style already-padded text: escape
// codes would otherwise count toward fmt's field widths and break column alignment.
class Painter
{
   public:
    explicit Painter(bool enabled) noexcept : enabled_(enabled) {}

    [[nodiscard]] std::string Bold(std::string_view text) const { return Apply(text, fmt::emphasis::bold); }

    [[nodiscard]] std::string Same(std::string_view text) const
    {
        return Apply(text, fmt::fg(fmt::terminal_color::green) | fmt::emphasis::bold);
    }

    [[nodiscard]] std::string Different(std::string_view text) const
    {
        return Apply(text, fmt::fg(fmt::terminal_color::yellow) | fmt::emphasis::bold);
    }

    [[nodiscard]] std::string NotSet(std::string_view text) const
    {
        return Apply(text, fmt::fg(fmt::terminal_color::red));
    }

   private:
    [[nodiscard]] std::string Apply(std::string_view text, const fmt::text_style& style) const
    {
        return enabled_ ? fmt::format(style, "{}", text) : std::string(text);
    }

    bool enabled_;
};

bool ColorEnabled()
{
    const auto* no_color = std::getenv("NO_COLOR");
    return isatty(STDOUT_FILENO) == 1 && (no_color == nullptr || no_color[0] == '\0');
}

// PodMonitor and spectator-cpp both log through spdlog's shared async thread pool, so lines logged
// during Refresh() can still be queued when the report starts printing. That pool has a single worker
// thread, so once a flush posted behind them has been dequeued, every earlier line has been written.
// Waiting for that (bounded) keeps log output from interleaving with the report.
void WaitForQueuedLogs()
{
    auto pool = spdlog::thread_pool();
    if (!pool)
    {
        return;
    }
    atlasagent::Logger()->flush();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool->queue_size() > 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::vector<PodView> CollectPods(const atlasagent::PodTrackedMap& tracked_pods)
{
    std::vector<PodView> pods;
    pods.reserve(tracked_pods.size());
    for (const auto& [uid, pod] : tracked_pods)
    {
        PodView view{uid, pod.name, pod.pod_namespace, {}};
        view.containers.reserve(pod.containers.size());
        for (const auto& [container_id, container] : pod.containers)
        {
            view.containers.push_back(
                ContainerView{container_id, container.container_name, &container.cgroup.ExtraTags()});
        }
        std::sort(view.containers.begin(), view.containers.end(),
                  [](const ContainerView& a, const ContainerView& b) {
                      return std::tie(a.name, a.id) < std::tie(b.name, b.id);
                  });
        pods.push_back(std::move(view));
    }
    std::sort(pods.begin(), pods.end(), [](const PodView& a, const PodView& b) {
        return std::tie(a.pod_namespace, a.name, a.uid) < std::tie(b.pod_namespace, b.name, b.uid);
    });
    return pods;
}

TagComparison CompareContainerTags(const PodView& pod)
{
    std::set<std::string> keys;
    for (const auto& container : pod.containers)
    {
        for (const auto& entry : *container.tags)
        {
            keys.insert(entry.first);
        }
    }

    TagComparison comparison;
    for (const auto& key : keys)
    {
        std::vector<std::optional<std::string>> values;
        values.reserve(pod.containers.size());
        for (const auto& container : pod.containers)
        {
            auto it = container.tags->find(key);
            values.push_back(it != container.tags->end() ? std::optional<std::string>{it->second} : std::nullopt);
        }

        // Never empty: every key was collected from at least one container.
        const bool same = std::all_of(values.begin(), values.end(), [&values](const auto& value) {
            return value.has_value() && value == values.front();
        });
        if (same)
        {
            comparison.shared.push_back(SharedTag{key, *values.front()});
        }
        else
        {
            comparison.differing.push_back(DifferingTag{key, std::move(values)});
        }
    }
    return comparison;
}

std::size_t CountContainers(const std::vector<PodView>& pods)
{
    std::size_t count = 0;
    for (const auto& pod : pods)
    {
        count += pod.containers.size();
    }
    return count;
}

void PrintRule() { fmt::print("{}\n", std::string(kRuleWidth, '=')); }

void PrintSharedTags(const std::vector<SharedTag>& tags)
{
    std::size_t key_width = 0;
    for (const auto& tag : tags)
    {
        key_width = std::max(key_width, tag.key.size());
    }
    for (const auto& tag : tags)
    {
        fmt::print("    {:<{}} = {}\n", tag.key, key_width, tag.value);
    }
}

void PrintHeader(const std::string& path_prefix, const std::vector<PodView>& pods, const Painter& paint)
{
    fmt::print("{}\n", paint.Bold("Common tags PodMonitor applies to each container, compared per pod"));
    fmt::print("  cgroup root:  {}\n", path_prefix);
    fmt::print("  kubelet:      {}/pods\n", atlasagent::PodIdentityClientConstants::KubeletUrl);
    const auto* k8s_cluster = std::getenv("K8S_CLUSTER");
    if (k8s_cluster != nullptr && k8s_cluster[0] != '\0')
    {
        fmt::print("  K8S_CLUSTER:  {}\n", k8s_cluster);
    }
    else
    {
        fmt::print("  K8S_CLUSTER:  {}\n",
                   paint.NotSet("(not set) -- k8s.cluster.name is missing below; export the agent's value"));
    }
    fmt::print("  tracked:      {} pod(s), {} container(s)\n", pods.size(), CountContainers(pods));
    fmt::print("  {} = identical on every container of the pod; {} = differs, or is not set, on at least one\n",
               paint.Same("SAME"), paint.Different("DIFFERENT"));
}

void PrintPod(const PodView& pod, const TagComparison& comparison, std::size_t position, std::size_t total,
              const Painter& paint)
{
    std::size_t name_width = 0;
    for (const auto& container : pod.containers)
    {
        name_width = std::max(name_width, container.name.size());
    }

    fmt::print("\n");
    PrintRule();
    fmt::print("{}\n", paint.Bold(fmt::format("Pod {}/{}  {}/{}", position, total, pod.pod_namespace, pod.name)));
    fmt::print("  uid:         {}\n", pod.uid);
    fmt::print("  containers:  {}\n", pod.containers.size());
    for (const auto& container : pod.containers)
    {
        fmt::print("    {:<{}}  {}\n", container.name, name_width, container.id.substr(0, kShortIdLength));
    }
    fmt::print("\n");

    if (pod.containers.size() == 1)
    {
        fmt::print("  {}\n", paint.Same(fmt::format("Single container, nothing to compare. Its tags ({}):",
                                                    comparison.shared.size())));
        PrintSharedTags(comparison.shared);
        return;
    }

    fmt::print("  {}\n", paint.Same(fmt::format("SAME on all {} containers ({}):", pod.containers.size(),
                                                comparison.shared.size())));
    if (comparison.shared.empty())
    {
        fmt::print("    (none)\n");
    }
    PrintSharedTags(comparison.shared);

    fmt::print("\n  {}\n", paint.Different(fmt::format("DIFFERENT between containers ({}):",
                                                       comparison.differing.size())));
    if (comparison.differing.empty())
    {
        fmt::print("    (none)\n");
    }
    for (const auto& tag : comparison.differing)
    {
        fmt::print("    {}\n", paint.Bold(tag.key));
        for (std::size_t i = 0; i < pod.containers.size(); ++i)
        {
            const auto& value = tag.values[i];
            fmt::print("      {:<{}} = {}\n", pod.containers[i].name, name_width,
                       value.has_value() ? *value : paint.NotSet("(not set)"));
        }
    }
}

// Per tag key, how many multi-container pods carry it with the same value on every container, and in
// how many its value differs or is set on only some containers. The remaining multi-container pods
// carry it on none of their containers.
struct TagTally
{
    std::size_t same = 0;
    std::size_t different = 0;
};

void PrintSummary(const std::vector<PodView>& pods, const std::vector<TagComparison>& comparisons,
                  const Painter& paint)
{
    std::size_t multi_container_pods = 0;
    std::size_t pods_with_differences = 0;
    std::map<std::string, TagTally> tallies;
    for (std::size_t i = 0; i < pods.size(); ++i)
    {
        if (pods[i].containers.size() < 2)
        {
            continue;
        }
        ++multi_container_pods;
        if (!comparisons[i].differing.empty())
        {
            ++pods_with_differences;
        }
        for (const auto& tag : comparisons[i].shared)
        {
            ++tallies[tag.key].same;
        }
        for (const auto& tag : comparisons[i].differing)
        {
            ++tallies[tag.key].different;
        }
    }

    fmt::print("\n");
    PrintRule();
    fmt::print("{}\n", paint.Bold("Summary"));
    fmt::print("  pods:        {} ({} multi-container, {} single-container)\n", pods.size(), multi_container_pods,
               pods.size() - multi_container_pods);
    fmt::print("  containers:  {}\n", CountContainers(pods));
    if (multi_container_pods == 0)
    {
        fmt::print("\n  No multi-container pods, so there is nothing to compare.\n");
        return;
    }

    std::size_t key_width = std::string_view("TAG").size();
    for (const auto& [key, tally] : tallies)
    {
        key_width = std::max(key_width, key.size());
    }

    fmt::print("\n  Tags across the {} multi-container pod(s), counted per pod "
               "(NOT SET = no container in the pod carries the tag):\n",
               multi_container_pods);
    fmt::print("    {:<{}}  {:>4}  {:>9}  {:>7}\n", "TAG", key_width, "SAME", "DIFFERENT", "NOT SET");
    std::vector<std::string> differing_keys;
    for (const auto& [key, tally] : tallies)
    {
        const auto different = fmt::format("{:>9}", tally.different);
        fmt::print("    {:<{}}  {:>4}  {}  {:>7}\n", key, key_width, tally.same,
                   tally.different > 0 ? paint.Different(different) : different,
                   multi_container_pods - tally.same - tally.different);
        if (tally.different > 0)
        {
            differing_keys.push_back(key);
        }
    }

    fmt::print("\n  Pods whose containers have differing tags: {} of {}\n", pods_with_differences,
               multi_container_pods);
    if (differing_keys.empty())
    {
        fmt::print("  {}\n", paint.Same("Every multi-container pod's containers share all of their tags."));
    }
    else
    {
        fmt::print("  Tags that differ within a pod: {}\n",
                   paint.Different(fmt::format("{}", fmt::join(differing_keys, ", "))));
    }
}

int Run(const std::string& path_prefix)
{
    auto config = Config(WriterConfig(WriterTypes::Memory));
    auto registry = Registry(config);
    TrackedPodMonitor pod_monitor{&registry, path_prefix};

    const auto refresh = pod_monitor.Refresh();
    WaitForQueuedLogs();
    if (refresh.state == atlasagent::PodMonitorState::kCgroupUnavailable)
    {
        const auto& error = *refresh.cgroup_error;
        fmt::print(stderr, "Cgroup discovery failed under {}: kind={}", path_prefix, atlasagent::ToString(error.kind));
        if (!error.path.empty())
        {
            fmt::print(stderr, " path={}", error.path.string());
        }
        if (error.cause)
        {
            fmt::print(stderr, " cause={}", error.cause.message());
        }
        fmt::print(stderr, "\n");
        return 1;
    }
    if (refresh.state == atlasagent::PodMonitorState::kIdentityUnavailable)
    {
        const auto& error = *refresh.identity_error;
        fmt::print(stderr, "Kubelet identity fetch failed: kind={} status={} response_bytes={} parse_offset={}\n",
                   atlasagent::ToString(error.kind), error.http_status, error.response_size, error.parse_offset);
        return 1;
    }

    const Painter paint{ColorEnabled()};
    const auto pods = CollectPods(pod_monitor.TrackedPods());
    std::vector<TagComparison> comparisons;
    comparisons.reserve(pods.size());
    for (const auto& pod : pods)
    {
        comparisons.push_back(CompareContainerTags(pod));
    }

    PrintHeader(path_prefix, pods, paint);
    for (std::size_t i = 0; i < pods.size(); ++i)
    {
        PrintPod(pods[i], comparisons[i], i + 1, pods.size(), paint);
    }
    PrintSummary(pods, comparisons, paint);
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    std::string path_prefix = "/sys/fs/cgroup";
    bool show_excluded = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            fmt::print("Usage: {} [cgroup_path_prefix] [filtered]  (either order; both optional)\n", argv[0]);
            fmt::print("  cgroup_path_prefix  cgroup v2 mount to scan (default /sys/fs/cgroup)\n");
            fmt::print("  filtered            enable debug logging so excluded pods and containers are visible\n");
            return 0;
        }
        if (arg == "filtered")
        {
            show_excluded = true;
        }
        else
        {
            path_prefix = arg;
        }
    }

    // Quiet both PodMonitor's logger and spectator-cpp's own (::Logger, which logs its writer and config
    // setup at info) to warnings unless "filtered" is given, so the report is not buried under every
    // "Tracking new pod/container" line. Set before the Config is built, since construction logs.
    const auto log_level = show_excluded ? spdlog::level::debug : spdlog::level::warn;
    atlasagent::Logger()->set_level(log_level);
    if (auto* spectator_logger = ::Logger::GetLogger(); spectator_logger != nullptr)
    {
        spectator_logger->set_level(log_level);
    }

    atlasagent::HttpClient::GlobalInit();
    const auto exit_code = Run(path_prefix);
    atlasagent::HttpClient::GlobalShutdown();
    return exit_code;
}
