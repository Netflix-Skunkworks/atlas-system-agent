


#include <thirdparty/spectator-cpp/spectator/registry.h>
#include <lib/collectors/perfspect/src/perfspect.h>
#include <lib/util/src/util.h>
#include <lib/logger/src/logger.h>





int main()
{
    Config config(WriterConfig(WriterTypes::Unix));
    Registry registry(config);

    std::optional<Perfspect> perfspectMetrics{std::nullopt};
    auto instanceInfo = Perfspect::is_valid_instance();
    if (instanceInfo.has_value() == true) 
    {
        perfspectMetrics.emplace(&registry, instanceInfo.value());
    }

    while (true)
    {
        perfspectMetrics->gather_metrics();
        std::this_thread::sleep_for(std::chrono::seconds(61));
    }
}
