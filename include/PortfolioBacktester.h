#pragma once

#include "AllocationPolicy.h"
#include "Event.h"
#include "MarketData.h"
#include "quant/market_data/TradingCalendar.h"
#include "quant/market_data/CorporateAction.h"

#include <map>
#include <string>
#include <vector>

enum class RebalanceFrequency {
    Weekly,
    Monthly
};

struct PortfolioBacktestConfig {
    std::vector<std::string> tickers;
    double starting_capital{100000.0};
    double transaction_cost_rate{0.001};
    double slippage_rate{0.0005};
    std::string data_dir{"data/synthetic"};
    std::string results_dir{"results/portfolio"};
    RebalanceFrequency rebalance_frequency{RebalanceFrequency::Monthly};
    AllocationPolicyConfig allocation;
    std::string benchmark_ticker{"SYN_BENCH"};
    quant::market_data::CalendarPolicy calendar{
        quant::market_data::CalendarMode::LegacyIntersection,
        quant::market_data::StaleMarkPolicy::LastKnown,
        quant::market_data::MissingBarPolicy::Error,
        quant::market_data::ClosedAssetPolicy::PartialRebalance,
        7};
    std::string annualization_method{"inferred_observed_periods"};
    double configured_periods_per_year{252.0};
    int result_schema_version{2};
    quant::market_data::AdjustmentPolicy adjustment_policy{quant::market_data::AdjustmentPolicy::RawPrice};
};

struct PortfolioFill {
    int rebalance_id{0};
    std::string date;
    std::string ticker;
    std::string side;
    int quantity{0};
    double price{0.0};
    double transaction_cost{0.0};
    double slippage_cost{0.0};
    double cash_after{0.0};
    std::string scheduled_rebalance_date;
    std::string decision_date;
    std::string execution_date;
};

struct PortfolioValuationMark {
    std::string date;
    std::string ticker;
    bool tradable{false};
    bool has_bar{false};
    double mark_price{0.0};
    std::string mark_source;
    int stale_age_days{0};
    double position_quantity{0.0};
    double marked_value{0.0};
    double actual_weight{0.0};
};

struct PortfolioRebalanceRecord {
    int rebalance_id{0};
    std::string scheduled_rebalance_date;
    std::string decision_date;
    std::string execution_date;
    int deferred_asset_count{0};
    int skipped_asset_count{0};
    bool partial_rebalance{false};
    std::string policy;
    double turnover{0.0};
};

struct PortfolioCorporateActionRecord {
    std::string date;
    std::string ticker;
    std::string action_type;
    double value{0.0};
    double quantity_before{0.0};
    double quantity_after{0.0};
    double cash_effect{0.0};
    double portfolio_value_before{0.0};
    double portfolio_value_after{0.0};
    std::string policy;
    std::string source;
};

struct PortfolioPositionPoint {
    std::string date;
    std::string ticker;
    double quantity{0.0};
    double price{0.0};
    double market_value{0.0};
    double target_weight{0.0};
    double actual_weight{0.0};
    int rebalance_id{0};
};

struct PortfolioEquityPoint {
    std::string date;
    double portfolio_value{0.0};
    double cash{0.0};
    double total_holdings_value{0.0};
    double total_return{0.0};
    double drawdown{0.0};
    double gross_exposure{0.0};
};

struct PortfolioSummary {
    int schema_version{2};
    std::string policy_name;
    double total_return{0.0};
    double equal_weight_benchmark_return{0.0};
    double spy_benchmark_return{0.0};
    double excess_return{0.0};
    double annualized_return{0.0};
    double volatility{0.0};
    double sharpe{0.0};
    double sortino{0.0};
    double max_drawdown{0.0};
    double calmar{0.0};
    double var_95{0.0};
    double expected_shortfall_95{0.0};
    double beta{0.0};
    double alpha{0.0};
    double information_ratio{0.0};
    double turnover{0.0};
    double total_transaction_costs{0.0};
    int number_of_rebalances{0};
    int number_of_fills{0};
    double average_cash_allocation{0.0};
    double average_gross_exposure{0.0};
    std::string calendar_mode{"intersection_legacy"};
    std::string valuation_frequency{"intersection_sessions"};
    double observations_per_year{252.0};
    std::string annualization_method{"configured"};
    int total_valuation_observations{0};
    int weekend_observations{0};
    int stale_mark_observations{0};
};

struct PortfolioBacktestResult {
    PortfolioSummary summary;
    std::vector<PortfolioEquityPoint> equity_curve;
    std::vector<PortfolioPositionPoint> positions;
    std::vector<PortfolioFill> fills;
    std::vector<std::map<std::string, double>> target_weights;
    std::vector<std::string> rebalance_dates;
    std::vector<double> turnover_by_rebalance;
    std::vector<PortfolioValuationMark> valuations;
    std::vector<PortfolioRebalanceRecord> rebalances;
    std::vector<PortfolioCorporateActionRecord> corporate_actions;
};

class PortfolioBacktester {
public:
    explicit PortfolioBacktester(PortfolioBacktestConfig config);

    PortfolioBacktestResult run();

    static RebalanceFrequency parse_frequency(const std::string& value);
    static std::string frequency_name(RebalanceFrequency frequency);
    static std::vector<std::size_t> rebalance_indices(const std::vector<std::string>& dates, RebalanceFrequency frequency);
    static double value_at_risk_95(std::vector<double> returns);
    static double expected_shortfall_95(std::vector<double> returns);

private:
    PortfolioBacktestConfig config_;
    PortfolioBacktestResult run_union();

    void load_data(std::map<std::string, std::vector<Bar>>& history) const;
    std::vector<std::string> common_dates(const std::map<std::string, std::vector<Bar>>& history) const;
    std::map<std::string, std::size_t> indices_for_date(
        const std::map<std::string, std::vector<Bar>>& history,
        const std::string& date) const;
    PortfolioSummary summarize(
        const AllocationPolicy& policy,
        const std::vector<PortfolioEquityPoint>& equity,
        const std::map<std::string, std::vector<Bar>>& history,
        const std::vector<std::string>& dates,
        double total_turnover,
        double total_costs,
        int rebalances,
        int fills,
        double cash_allocation_sum,
        double gross_exposure_sum,
        const std::vector<double>& benchmark_returns) const;
};
