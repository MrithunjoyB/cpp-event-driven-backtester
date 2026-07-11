#pragma once

#include <stdexcept>
#include <string>

namespace quant {

class QuantError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ConfigurationError : QuantError { using QuantError::QuantError; };
struct DataError : QuantError { using QuantError::QuantError; };
struct SimulationError : QuantError { using QuantError::QuantError; };
struct MethodologyError : QuantError { using QuantError::QuantError; };
struct ExportError : QuantError { using QuantError::QuantError; };
struct CliError : QuantError { using QuantError::QuantError; };

}  // namespace quant
