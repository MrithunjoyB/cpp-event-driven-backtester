#include "quant/market_data/CorporateAction.h"

namespace quant::market_data {
std::string to_string(AdjustmentPolicy value) {
    if (value == AdjustmentPolicy::RawPrice) return "raw_price";
    return value == AdjustmentPolicy::SplitAdjusted ? "split_adjusted" : "total_return_adjusted";
}
std::string to_string(CorporateActionType value) {
    return value == CorporateActionType::StockSplit ? "stock_split" : "cash_dividend";
}
}  // namespace quant::market_data
