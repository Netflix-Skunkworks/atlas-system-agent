
#include <sstream>
#include <iostream>
#include <optional>
#include "util.h"
#include "tagging_registry.h"
#include "spectator/registry.h"


struct DataLine {
    std::string fieldName;
    int fieldId;
    double value;
    std::string valueInterpretation;

    // Constructor to initialize the struct
    DataLine(std::string name, int id, double val, std::string interpretation)
        : fieldName(name), fieldId(id), value(val), valueInterpretation(interpretation) {}
};


struct DCGMExecutorConstants
{
    static constexpr auto UbuntuDCGMSharedLibPath{"/usr/lib/x86_64-linux-gnu/libdcgm.so"};
    static constexpr auto ExternalBinaryPath{"/opt/dcgm_stats_main"};
    static constexpr auto ConsecutiveFailureThreshold {5};
};

namespace detail {
    template <typename Reg>
    inline auto gauge(Reg* registry, const char* name, unsigned gpu, const char* id = nullptr) {
      auto tags = spectator::Tags{{"gpu", fmt::format("gpu-{}", gpu)}};
      if (id != nullptr) {
        tags.add("id", id);
      }
      return registry->GetGauge(name, tags);
    }
    
    }  // namespace detail


template <typename Reg = atlasagent::TaggingRegistry>
class DCGMExecutor
{
public:
    DCGMExecutor(Reg* registry) : registry_{registry} {};
    ~DCGMExecutor() = default;

    // Abide by the C++ rule of 5
    DCGMExecutor(const DCGMExecutor &other) = delete;
    DCGMExecutor &operator=(const DCGMExecutor &other) = delete;
    DCGMExecutor(DCGMExecutor &&other) noexcept = delete;
    DCGMExecutor &operator=(DCGMExecutor &&other) noexcept = delete;



    void ParseLines(std::vector<std::string> &lines, std::map<int, std::vector<DataLine>> &dataMap);
    void PrintDataMap(std::map<int, std::vector<DataLine>> &dataMap);
    bool DCGMMetrics();
    void UpdateMetrics(std::map<int, std::vector<DataLine>> &dataMap);
private:
    Reg* registry_;
};