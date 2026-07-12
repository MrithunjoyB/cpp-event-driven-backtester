#pragma once

#include <string>

namespace quant::app {

struct LegacyRunRequest {
    std::string mode{"compare"};
    std::string ticker;
    std::string strategy;
    std::string start_date;
    std::string end_date;
    std::string benchmark{"same_asset"};
    double capital{100000.0};
    double transaction_cost{0.001};
    double slippage{0.0005};
};

class Application {
public:
    static int run_config(const std::string& config_path, bool dry_run,
                          const std::string& execution_mode = {}, int threads = 0);
    static int run_legacy(const LegacyRunRequest& request);
};

}  // namespace quant::app
