#pragma once

#include "PortfolioBacktester.h"

#include <string>
#include <vector>

namespace quant::analytics {

struct AssetContribution {
    std::string start_date;
    std::string end_date;
    std::string ticker;
    double beginning_quantity{0.0};
    double ending_quantity{0.0};
    double beginning_mark{0.0};
    double ending_mark{0.0};
    double beginning_value{0.0};
    double ending_value{0.0};
    double trade_cash_flow{0.0};
    double market_pnl{0.0};
    double dividend_income{0.0};
    double split_adjustment{0.0};
    double commission{0.0};
    double spread_cost{0.0};
    double slippage_cost{0.0};
    double net_contribution{0.0};
    double beginning_weight{0.0};
    double ending_weight{0.0};
    bool tradable{false};
    bool stale_mark{false};
    int stale_age_days{0};
    std::string regime{"unavailable"};
};

struct PeriodAttribution {
    std::string start_date;
    std::string end_date;
    double beginning_value{0.0};
    double ending_value{0.0};
    double external_cash_flow{0.0};
    double market_pnl{0.0};
    double dividend_income{0.0};
    double corporate_action_effect{0.0};
    double cash_return{0.0};
    double commission{0.0};
    double spread_cost{0.0};
    double slippage_cost{0.0};
    double residual{0.0};
};

struct RiskContribution {
    std::string ticker;
    double component_volatility{0.0};
    double percentage_contribution{0.0};
    double beta_contribution{0.0};
    std::size_t observations{0};
};

struct DrawdownContribution {
    int episode_id{0};
    std::string peak_date;
    std::string trough_date;
    std::string recovery_date;
    double depth{0.0};
    double peak_value{0.0};
    double trough_value{0.0};
    int duration_observations{0};
    int recovery_observations{0};
    std::string ticker;
    double contribution{0.0};
    int stale_mark_observations{0};
    bool unresolved{false};
};

struct AttributionSummaryRow {
    std::string component;
    double contribution{0.0};
    double contribution_return{0.0};
    double percentage_of_net_profit{0.0};
};

struct CashContribution {
    std::string start_date;
    std::string end_date;
    double beginning_cash{0.0};
    double ending_cash{0.0};
    double average_cash{0.0};
    double average_allocation{0.0};
    double cash_return{0.0};
    double benchmark_drag{0.0};
    double dividend_cash{0.0};
    double costs_paid{0.0};
};

struct CorporateActionContribution {
    std::string date;
    std::string ticker;
    std::string action_type;
    double declared_value{0.0};
    double economic_contribution{0.0};
    double cash_effect{0.0};
    std::string adjustment_policy;
    std::string source;
};

struct CalendarYearContribution {
    int year{0};
    std::string ticker;
    double contribution{0.0};
    double residual{0.0};
};

struct RebalanceAttribution {
    int rebalance_id{0};
    std::string scheduled_date;
    std::string decision_date;
    std::string execution_date;
    std::string next_rebalance_date;
    double turnover{0.0};
    double gross_trade_value{0.0};
    double costs{0.0};
    double cash_after{0.0};
    double holding_period_contribution{0.0};
    int deferred_assets{0};
    int skipped_assets{0};
    bool partial{false};
};

struct RegimeContribution {
    std::string regime;
    std::string ticker;
    double contribution{0.0};
    int observations{0};
    int trades{0};
    int rebalances{0};
};

struct PortfolioAttributionResult {
    int schema_version{3};
    std::string experiment_id;
    std::string policy;
    std::string benchmark;
    std::string adjustment_basis;
    std::string calendar_mode;
    double residual_tolerance{1e-6};
    std::vector<AssetContribution> assets;
    std::vector<PeriodAttribution> periods;
    std::vector<AttributionSummaryRow> summary;
    std::vector<CashContribution> cash;
    std::vector<CorporateActionContribution> corporate_actions;
    std::vector<RiskContribution> risk;
    std::vector<DrawdownContribution> drawdowns;
    std::vector<CalendarYearContribution> calendar_years;
    std::vector<RebalanceAttribution> rebalances;
    std::vector<RegimeContribution> regimes;
    double total_residual{0.0};
};

class PortfolioAttributionAnalyzer {
public:
    static PortfolioAttributionResult analyze(
        const PortfolioBacktestResult& portfolio,
        const PortfolioBacktestConfig& config,
        const std::string& experiment_id,
        double residual_tolerance = 1e-6);
};

}  // namespace quant::analytics
