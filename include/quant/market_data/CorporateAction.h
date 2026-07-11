#pragma once

#include <string>

namespace quant::market_data {

enum class AdjustmentPolicy { RawPrice, SplitAdjusted, TotalReturnAdjusted };
enum class CorporateActionType { StockSplit, CashDividend };

struct CorporateAction {
    std::string date;
    std::string ticker;
    CorporateActionType type{CorporateActionType::StockSplit};
    double value{0.0};
    std::string source{"csv"};
};

std::string to_string(AdjustmentPolicy value);
std::string to_string(CorporateActionType value);

}  // namespace quant::market_data
