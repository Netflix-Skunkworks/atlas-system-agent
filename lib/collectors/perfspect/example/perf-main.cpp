


#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <lib/collectors/perfspect/src/perfspect.h>
#include <lib/util/src/util.h>
#include <lib/logger/src/logger.h>





int main()
{
    Config config(WriterConfig(WriterTypes::Unix));
    Registry registry(config);

    std::optional<Perfspect> perfspectMetrics{std::nullopt};
    if (atlasagent::is_file_present(PerfspectConstants::BinaryLocation)) {
        perfspectMetrics.emplace(&registry);
    }

    if (perfspectMetrics.has_value() == false) {
        atlasagent::Logger()->error("Perfspect binary not found at {}", PerfspectConstants::BinaryLocation);
        return 0;
    }

    

    while (true)
    {
        perfspectMetrics->gather_metrics();
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

}
