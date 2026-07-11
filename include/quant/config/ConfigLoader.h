#pragma once

#include "quant/config/ExperimentConfig.h"

#include <string>

namespace quant::config {

class ConfigLoader {
public:
    static ExperimentConfig load_file(const std::string& filepath);
    static void validate(const ExperimentConfig& config);
    static std::string to_json(const ExperimentConfig& config);
};

}  // namespace quant::config
