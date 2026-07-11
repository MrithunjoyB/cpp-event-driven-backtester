#include "quant/config/ConfigLoader.h"
#include "quant/domain/Date.h"
#include "quant/domain/Errors.h"

#include <iostream>

int main() {
    try {
        if (quant::Date::parse("2020-02-29").add_years(3).to_string() != "2023-02-28") return 1;
        if (quant::Date::parse("2020-01-31").add_months(1).to_string() != "2020-02-29") return 1;
        bool rejected = false;
        try { (void)quant::Date::parse("2023-02-29"); } catch (const quant::DataError&) { rejected = true; }
        if (!rejected) return 1;
        const auto config = quant::config::ConfigLoader::load_file("configs/ma_walk_forward.json");
        if (config.walk_forward.window_mode != "calendar_duration" || config.benchmark.ticker != "SPY") return 1;
        if (quant::config::ConfigLoader::to_json(config).find("\"result_schema_version\": 2") == std::string::npos) return 1;
        std::cout << "domain_config_tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
