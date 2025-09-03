#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <lib/collectors/perfspect/src/perfspect.h>
#include <lib/util/src/util.h>
#include <lib/logger/src/logger.h>

using atlasagent::Logger;

int main()
{
    auto logger = Logger();
    logger->set_level(spdlog::level::debug);

    Config config(WriterConfig(WriterTypes::Unix));
    Registry registry(config);

    std::optional<Perfspect> perfspectMetrics{std::nullopt};
    auto instanceInfo = Perfspect::IsValidInstance();
    if (instanceInfo.has_value() == false)
    {
        Logger::error("Perfspect", "No valid AMD instance found, exiting");
        return 1;
    }

    perfspectMetrics.emplace(&registry, instanceInfo.value());
    while (true)
    {
        perfspectMetrics->GatherMetrics();
        sleep(5);
    }
}
