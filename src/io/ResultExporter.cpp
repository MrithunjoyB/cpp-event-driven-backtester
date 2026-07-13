#include "quant/io/ResultExporter.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <sstream>

namespace quant::io {
namespace {
std::string sanitize(const std::string& value) {
    std::string output = value;
    for (char& ch : output) {
        if (ch == ' ' || ch == '-' || ch == '/') {
            ch = '_';
        }
    }
    return output;
}

std::ofstream open_output(const std::string& filepath) {
    std::ofstream output(filepath);
    if (!output.is_open()) {
        throw std::runtime_error("Could not open result file for writing: " + filepath);
    }
    output << std::fixed << std::setprecision(6);
    return output;
}

void verify_output(const std::ofstream& output, const std::string& filepath) {
    if (!output.good()) {
        throw std::runtime_error("Failed while writing result file: " + filepath);
    }
}

void write_summary_header(std::ofstream& output) {
    output << "schema_version,ticker,strategy,parameter_set,benchmark_ticker,benchmark_execution_policy,benchmark_cost_policy,excess_return_basis,total_return,benchmark_gross_return,benchmark_net_return,excess_return,annualized_return,volatility,sharpe,max_drawdown,win_rate,profit_factor,num_trades,turnover,total_transaction_costs,cost_drag,average_trade_return\n";
}

void write_summary_row(std::ofstream& output, const PerformanceSummary& summary) {
    output << summary.schema_version << ',' << summary.ticker << ',' << summary.strategy << ','
           << summary.parameter_set << ',' << summary.benchmark_ticker << ','
           << summary.benchmark_execution_policy << ',' << summary.benchmark_cost_policy << ','
           << summary.excess_return_basis << ',' << summary.total_return << ','
           << summary.benchmark_gross_return << ',' << summary.benchmark_net_return << ','
           << summary.excess_return << ',' << summary.annualized_return << ',' << summary.volatility << ','
           << summary.sharpe << ',' << summary.max_drawdown << ',' << summary.win_rate << ','
           << summary.profit_factor << ',' << summary.num_trades << ',' << summary.turnover << ','
           << summary.total_transaction_costs << ',' << summary.cost_drag << ','
           << summary.average_trade_return << '\n';
}
}

void CsvResultExporter::write_backtest(const BacktestResult& result, const std::string& results_dir) {
    std::error_code error;
    std::filesystem::create_directories(results_dir, error);
    if (error) {
        throw std::runtime_error("Could not create result directory " + results_dir + ": " + error.message());
    }
    const std::string prefix = sanitize(result.summary.ticker) + "_" + sanitize(result.summary.strategy);
    const std::vector<std::string> trade_paths = {
        results_dir + "/" + prefix + "_trades.csv", results_dir + "/trades.csv"};
    const std::vector<std::string> equity_paths = {
        results_dir + "/" + prefix + "_equity_curve.csv", results_dir + "/equity_curve.csv"};
    const std::vector<std::string> summary_paths = {
        results_dir + "/" + prefix + "_performance_summary.csv", results_dir + "/performance_summary.csv"};

    for (const auto& path : trade_paths) {
        auto output = open_output(path);
        output << "date,ticker,strategy,action,price,quantity,cost,slippage,portfolio_value,realized_pnl,trade_return\n";
        for (const auto& trade : result.trades) {
            output << trade.date << ',' << trade.ticker << ',' << trade.strategy << ',' << trade.action << ','
                   << trade.price << ',' << trade.quantity << ',' << trade.cost << ',' << trade.slippage << ','
                   << trade.portfolio_value << ',' << trade.realized_pnl << ',' << trade.trade_return << '\n';
        }
        verify_output(output, path);
    }
    for (const auto& path : equity_paths) {
        auto output = open_output(path);
        output << "date,portfolio_value,cash,holdings,total_return,drawdown\n";
        for (const auto& point : result.equity_curve) {
            output << point.date << ',' << point.portfolio_value << ',' << point.cash << ',' << point.holdings << ','
                   << point.total_return << ',' << point.drawdown << '\n';
        }
        verify_output(output, path);
    }
    for (const auto& path : summary_paths) {
        auto output = open_output(path);
        write_summary_header(output);
        write_summary_row(output, result.summary);
        verify_output(output, path);
    }
}

void CsvResultExporter::write_combined_summary(
    const std::string& filepath,
    const std::vector<PerformanceSummary>& summaries) {
    auto output = open_output(filepath);
    write_summary_header(output);
    for (const auto& summary : summaries) {
        write_summary_row(output, summary);
    }
    verify_output(output, filepath);
}

void CsvResultExporter::write_portfolio(
    const PortfolioBacktestResult& result,
    const PortfolioBacktestConfig& config) {
    std::error_code error;
    std::filesystem::create_directories(config.results_dir, error);
    if (error) {
        throw std::runtime_error("Could not create portfolio result directory " + config.results_dir + ": " + error.message());
    }
    auto equity = open_output(config.results_dir + "/portfolio_equity_curve.csv");
    auto positions = open_output(config.results_dir + "/portfolio_positions.csv");
    auto orders = open_output(config.results_dir + "/portfolio_orders.csv");
    auto fills = open_output(config.results_dir + "/portfolio_fills.csv");
    auto rebalances = open_output(config.results_dir + "/portfolio_rebalances.csv");
    auto weights = open_output(config.results_dir + "/portfolio_allocation_weights.csv");
    auto costs = open_output(config.results_dir + "/portfolio_costs.csv");
    auto summary = open_output(config.results_dir + "/portfolio_performance_summary.csv");
    std::ofstream valuations;
    std::ofstream corporate_actions;
    if (config.result_schema_version >= 3) valuations = open_output(config.results_dir + "/portfolio_valuations.csv");
    if (config.result_schema_version >= 3) corporate_actions = open_output(config.results_dir + "/portfolio_corporate_actions.csv");

    equity << "date,portfolio_value,cash,total_holdings_value,total_return,drawdown,gross_exposure\n";
    for (const auto& point : result.equity_curve) {
        equity << point.date << ',' << point.portfolio_value << ',' << point.cash << ',' << point.total_holdings_value
               << ',' << point.total_return << ',' << point.drawdown << ',' << point.gross_exposure << '\n';
    }
    positions << "date,ticker,quantity,price,market_value,target_weight,actual_weight,rebalance_id\n";
    for (const auto& point : result.positions) {
        positions << point.date << ',' << point.ticker << ',' << point.quantity << ',' << point.price << ','
                  << point.market_value << ',' << point.target_weight << ',' << point.actual_weight << ','
                  << point.rebalance_id << '\n';
    }
    fills << "rebalance_id,date,ticker,side,quantity,price,transaction_cost,slippage_cost,cash_after";
    if (config.result_schema_version >= 3) fills << ",scheduled_rebalance_date,decision_date,execution_date";
    fills << '\n';
    orders << "rebalance_id,date,ticker,side,quantity,price,transaction_cost,slippage_cost\n";
    costs << "rebalance_id,date,ticker,transaction_cost,slippage_cost,total_cost\n";
    for (const auto& fill : result.fills) {
        fills << fill.rebalance_id << ',' << fill.date << ',' << fill.ticker << ',' << fill.side << ','
              << fill.quantity << ',' << fill.price << ',' << fill.transaction_cost << ',' << fill.slippage_cost
              << ',' << fill.cash_after;
        if (config.result_schema_version >= 3) fills << ',' << fill.scheduled_rebalance_date << ',' << fill.decision_date << ',' << fill.execution_date;
        fills << '\n';
        orders << fill.rebalance_id << ',' << fill.date << ',' << fill.ticker << ',' << fill.side << ','
               << fill.quantity << ',' << fill.price << ',' << fill.transaction_cost << ',' << fill.slippage_cost << '\n';
        costs << fill.rebalance_id << ',' << fill.date << ',' << fill.ticker << ',' << fill.transaction_cost << ','
              << fill.slippage_cost << ',' << fill.transaction_cost + fill.slippage_cost << '\n';
    }
    if (config.result_schema_version >= 3) {
        rebalances << "rebalance_id,scheduled_rebalance_date,decision_date,execution_date,deferred_asset_count,skipped_asset_count,partial_rebalance,closed_asset_policy,turnover,cash_buffer,max_weight,min_trade_value\n";
    } else {
        rebalances << "rebalance_id,date,policy_name,frequency,turnover,cash_buffer,max_weight,min_trade_value\n";
    }
    weights << "rebalance_id,policy_name,ticker,target_weight\n";
    for (std::size_t i = 0; i < result.target_weights.size(); ++i) {
        const std::string date = i < result.rebalance_dates.size() ? result.rebalance_dates[i] : "";
        if (config.result_schema_version >= 3 && i < result.rebalances.size()) {
            const auto& record = result.rebalances[i];
            rebalances << record.rebalance_id << ',' << record.scheduled_rebalance_date << ',' << record.decision_date << ','
                       << record.execution_date << ',' << record.deferred_asset_count << ',' << record.skipped_asset_count << ','
                       << (record.partial_rebalance ? 1 : 0) << ',' << record.policy << ',' << record.turnover << ','
                       << config.allocation.cash_buffer << ',' << config.allocation.max_weight << ',' << config.allocation.min_trade_value << '\n';
        } else {
            rebalances << i << ',' << date << ',' << result.summary.policy_name << ','
                       << PortfolioBacktester::frequency_name(config.rebalance_frequency) << ','
                       << (i < result.turnover_by_rebalance.size() ? result.turnover_by_rebalance[i] : 0.0) << ','
                       << config.allocation.cash_buffer << ',' << config.allocation.max_weight << ','
                       << config.allocation.min_trade_value << '\n';
        }
        for (const auto& weight : result.target_weights[i]) {
            weights << i << ',' << result.summary.policy_name << ',' << weight.first << ',' << weight.second << '\n';
        }
    }
    summary << "policy_name,total_return,equal_weight_benchmark_return,spy_benchmark_return,excess_return,annualized_return,volatility,sharpe,sortino,max_drawdown,calmar,var_95,expected_shortfall_95,beta,alpha,information_ratio,turnover,total_transaction_costs,number_of_rebalances,number_of_fills,average_cash_allocation,average_gross_exposure";
    if (config.result_schema_version >= 3) summary << ",schema_version,calendar_mode,valuation_frequency,observations_per_year,annualization_method,total_valuation_observations,weekend_observations,stale_mark_observations,stale_mark_policy,max_stale_calendar_days";
    summary << '\n';
    const auto& value = result.summary;
    summary << value.policy_name << ',' << value.total_return << ',' << value.equal_weight_benchmark_return << ','
            << value.spy_benchmark_return << ',' << value.excess_return << ',' << value.annualized_return << ','
            << value.volatility << ',' << value.sharpe << ',' << value.sortino << ',' << value.max_drawdown << ','
            << value.calmar << ',' << value.var_95 << ',' << value.expected_shortfall_95 << ',' << value.beta << ','
            << value.alpha << ',' << value.information_ratio << ',' << value.turnover << ','
            << value.total_transaction_costs << ',' << value.number_of_rebalances << ',' << value.number_of_fills << ','
            << value.average_cash_allocation << ',' << value.average_gross_exposure;
    if (config.result_schema_version >= 3) {
        summary << ',' << value.schema_version << ',' << value.calendar_mode << ',' << value.valuation_frequency << ','
                << value.observations_per_year << ',' << value.annualization_method << ',' << value.total_valuation_observations << ','
                << value.weekend_observations << ',' << value.stale_mark_observations << ','
                << quant::market_data::to_string(config.calendar.stale_mark_policy) << ',' << config.calendar.max_stale_calendar_days;
        valuations << "date,ticker,tradable,has_bar,mark_price,mark_source,stale_age_days,position_quantity,marked_value,actual_weight\n";
        for (const auto& mark : result.valuations) {
            valuations << mark.date << ',' << mark.ticker << ',' << (mark.tradable ? 1 : 0) << ',' << (mark.has_bar ? 1 : 0) << ','
                       << mark.mark_price << ',' << mark.mark_source << ',' << mark.stale_age_days << ',' << mark.position_quantity << ','
                       << mark.marked_value << ',' << mark.actual_weight << '\n';
        }
        corporate_actions << "action_date,ticker,action_type,value,quantity_before,quantity_after,cash_effect,portfolio_value_before,portfolio_value_after,adjustment_policy,source\n";
        for (const auto& action : result.corporate_actions) {
            corporate_actions << action.date << ',' << action.ticker << ',' << action.action_type << ',' << action.value << ','
                              << action.quantity_before << ',' << action.quantity_after << ',' << action.cash_effect << ','
                              << action.portfolio_value_before << ',' << action.portfolio_value_after << ',' << action.policy << ','
                              << action.source << '\n';
        }
    }
    summary << '\n';

    verify_output(equity, config.results_dir + "/portfolio_equity_curve.csv");
    verify_output(positions, config.results_dir + "/portfolio_positions.csv");
    verify_output(orders, config.results_dir + "/portfolio_orders.csv");
    verify_output(fills, config.results_dir + "/portfolio_fills.csv");
    verify_output(rebalances, config.results_dir + "/portfolio_rebalances.csv");
    verify_output(weights, config.results_dir + "/portfolio_allocation_weights.csv");
    verify_output(costs, config.results_dir + "/portfolio_costs.csv");
    verify_output(summary, config.results_dir + "/portfolio_performance_summary.csv");
    if (config.result_schema_version >= 3) verify_output(valuations, config.results_dir + "/portfolio_valuations.csv");
    if (config.result_schema_version >= 3) verify_output(corporate_actions, config.results_dir + "/portfolio_corporate_actions.csv");
}

void CsvResultExporter::write_bootstrap(
    const quant::experiments::BootstrapResult& result,
    const std::string& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) throw std::runtime_error("Could not create bootstrap result directory " + directory + ": " + error.message());
    const std::string paths_path = directory + "/bootstrap_paths_sample.csv";
    const std::string metrics_path = directory + "/bootstrap_metric_distributions.csv";
    const std::string summary_path = directory + "/bootstrap_summary.csv";
    auto paths = open_output(paths_path);
    auto metrics = open_output(metrics_path);
    auto summary = open_output(summary_path);
    paths << "path,step,equity\n";
    for (const auto& point : result.sampled_paths) paths << point.path << ',' << point.step << ',' << point.equity << '\n';
    metrics << "path,total_return,terminal_wealth,max_drawdown,sharpe\n";
    for (const auto& metric : result.metrics) {
        metrics << metric.path << ',' << metric.total_return << ',' << metric.terminal_wealth << ','
                << metric.max_drawdown << ',' << metric.sharpe << '\n';
    }
    summary << "metric,value\n"
            << "paths," << result.path_count << '\n'
            << "seed," << result.seed << '\n'
            << "terminal_wealth_p05," << result.terminal_wealth_p05 << '\n'
            << "terminal_wealth_p50," << result.terminal_wealth_p50 << '\n'
            << "terminal_wealth_p95," << result.terminal_wealth_p95 << '\n'
            << "probability_of_loss," << result.probability_of_loss << '\n';
    verify_output(paths, paths_path);
    verify_output(metrics, metrics_path);
    verify_output(summary, summary_path);
}

void CsvResultExporter::write_attribution(
    const quant::analytics::PortfolioAttributionResult& result,
    const std::string& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) throw std::runtime_error("Could not create attribution directory " + directory + ": " + error.message());
    std::ostringstream metadata_stream;
    metadata_stream << result.schema_version << ',' << result.experiment_id << ',' << result.policy << ',' << result.benchmark << ','
        << result.adjustment_basis << ',' << result.calendar_mode << ",trade_aware_accounting,pnl_currency," << std::scientific
        << std::setprecision(3) << result.residual_tolerance;
    const std::string metadata = metadata_stream.str();
    const std::string prefix = "schema_version,experiment_id,policy,benchmark,adjustment_basis,calendar_mode,attribution_methodology,contribution_units,residual_tolerance,";

    const std::string daily_path = directory + "/daily_asset_attribution.csv";
    auto daily = open_output(daily_path);
    daily << prefix << "start_date,end_date,ticker,beginning_quantity,ending_quantity,beginning_mark,ending_mark,beginning_marked_value,ending_marked_value,trade_cash_flow,market_pnl,dividend_income,split_adjustment,commission,spread_cost,slippage_cost,net_contribution,beginning_weight,ending_weight,tradable,stale_mark,stale_age_days,regime\n";
    for (const auto& row : result.assets) daily << metadata << ',' << row.start_date << ',' << row.end_date << ',' << row.ticker << ','
        << row.beginning_quantity << ',' << row.ending_quantity << ',' << row.beginning_mark << ',' << row.ending_mark << ','
        << row.beginning_value << ',' << row.ending_value << ',' << row.trade_cash_flow << ',' << row.market_pnl << ','
        << row.dividend_income << ',' << row.split_adjustment << ',' << row.commission << ',' << row.spread_cost << ','
        << row.slippage_cost << ',' << row.net_contribution << ',' << row.beginning_weight << ',' << row.ending_weight << ','
        << (row.tradable ? 1 : 0) << ',' << (row.stale_mark ? 1 : 0) << ',' << row.stale_age_days << ',' << row.regime << '\n';

    const std::string reconciliation_path = directory + "/attribution_reconciliation.csv";
    auto reconciliation = open_output(reconciliation_path);
    reconciliation << prefix << "start_date,end_date,beginning_value,ending_value,external_cash_flow,market_pnl,dividend_income,corporate_action_effect,cash_return,commission,spread_cost,slippage_cost,residual\n";
    for (const auto& row : result.periods) reconciliation << metadata << ',' << row.start_date << ',' << row.end_date << ','
        << row.beginning_value << ',' << row.ending_value << ',' << row.external_cash_flow << ',' << row.market_pnl << ','
        << row.dividend_income << ',' << row.corporate_action_effect << ',' << row.cash_return << ',' << row.commission << ','
        << row.spread_cost << ',' << row.slippage_cost << ',' << row.residual << '\n';

    const std::string summary_path = directory + "/portfolio_attribution_summary.csv";
    auto summary = open_output(summary_path);
    summary << prefix << "component,contribution,contribution_return,percentage_of_net_profit\n";
    for (const auto& row : result.summary) summary << metadata << ',' << row.component << ',' << row.contribution << ','
        << row.contribution_return << ',' << row.percentage_of_net_profit << '\n';

    const std::string cash_path = directory + "/cash_attribution.csv";
    auto cash = open_output(cash_path);
    cash << prefix << "start_date,end_date,beginning_cash,ending_cash,average_cash,average_cash_allocation,cash_return,uninvested_cash_drag,dividend_cash,costs_paid\n";
    for (const auto& row : result.cash) cash << metadata << ',' << row.start_date << ',' << row.end_date << ',' << row.beginning_cash << ','
        << row.ending_cash << ',' << row.average_cash << ',' << row.average_allocation << ',' << row.cash_return << ','
        << row.benchmark_drag << ',' << row.dividend_cash << ',' << row.costs_paid << '\n';

    const std::string cost_path = directory + "/transaction_cost_attribution.csv";
    auto costs = open_output(cost_path);
    costs << prefix << "date,ticker,commission,fixed_commission,minimum_commission,spread_cost,slippage_cost,total_cost,cumulative_cost,regime\n";
    double cumulative = 0.0;
    for (const auto& row : result.assets) if (row.commission + row.spread_cost + row.slippage_cost > 0.0) {
        const double total = row.commission + row.spread_cost + row.slippage_cost;
        cumulative += total;
        costs << metadata << ',' << row.end_date << ',' << row.ticker << ',' << row.commission << ",0,0," << row.spread_cost << ','
              << row.slippage_cost << ',' << total << ',' << cumulative << ',' << row.regime << '\n';
    }

    const std::string action_path = directory + "/corporate_action_attribution.csv";
    auto actions = open_output(action_path);
    actions << prefix << "date,ticker,action_type,declared_value,economic_contribution,cash_effect,source\n";
    for (const auto& row : result.corporate_actions) actions << metadata << ',' << row.date << ',' << row.ticker << ','
        << row.action_type << ',' << row.declared_value << ',' << row.economic_contribution << ',' << row.cash_effect << ',' << row.source << '\n';

    const std::string rebalance_path = directory + "/rebalance_attribution.csv";
    auto rebalances = open_output(rebalance_path);
    rebalances << prefix << "rebalance_id,scheduled_date,decision_date,execution_date,next_rebalance_date,turnover,gross_trade_value,transaction_costs,cash_after,holding_period_contribution,deferred_assets,skipped_assets,partial_rebalance\n";
    for (const auto& row : result.rebalances) rebalances << metadata << ',' << row.rebalance_id << ',' << row.scheduled_date << ','
        << row.decision_date << ',' << row.execution_date << ',' << row.next_rebalance_date << ',' << row.turnover << ','
        << row.gross_trade_value << ',' << row.costs << ',' << row.cash_after << ',' << row.holding_period_contribution << ','
        << row.deferred_assets << ',' << row.skipped_assets << ',' << (row.partial ? 1 : 0) << '\n';

    const std::string benchmark_path = directory + "/benchmark_relative_attribution.csv";
    auto benchmark = open_output(benchmark_path);
    benchmark << prefix << "component,portfolio_contribution_return,benchmark_contribution_return,active_contribution_return\n";
    for (const auto& row : result.summary) if (row.component == "PORTFOLIO_RETURN" || row.component == "BENCHMARK_RETURN" || row.component == "ACTIVE_RETURN")
        benchmark << metadata << ',' << row.component << ','
                  << (row.component == "PORTFOLIO_RETURN" ? row.contribution_return : 0.0) << ','
                  << (row.component == "BENCHMARK_RETURN" ? -row.contribution_return : 0.0) << ','
                  << (row.component == "ACTIVE_RETURN" ? row.contribution_return : 0.0) << '\n';

    const std::string drawdown_path = directory + "/drawdown_episode_attribution.csv";
    auto drawdowns = open_output(drawdown_path);
    drawdowns << prefix << "episode_id,peak_date,trough_date,recovery_date,drawdown_depth,peak_value,trough_value,duration_observations,recovery_observations,ticker,contribution,stale_mark_observations,unresolved\n";
    for (const auto& row : result.drawdowns) drawdowns << metadata << ',' << row.episode_id << ',' << row.peak_date << ',' << row.trough_date << ','
        << row.recovery_date << ',' << row.depth << ',' << row.peak_value << ',' << row.trough_value << ',' << row.duration_observations << ',' << row.recovery_observations << ','
        << row.ticker << ',' << row.contribution << ',' << row.stale_mark_observations << ',' << (row.unresolved ? 1 : 0) << '\n';

    const std::string risk_path = directory + "/risk_contribution.csv";
    auto risk = open_output(risk_path);
    risk << prefix << "ticker,component_volatility,percentage_contribution,beta_contribution,observations,risk_methodology\n";
    for (const auto& row : result.risk) risk << metadata << ',' << row.ticker << ',' << row.component_volatility << ','
        << row.percentage_contribution << ',' << row.beta_contribution << ',' << row.observations << ",ex_post_covariance_euler\n";

    const std::string year_path = directory + "/calendar_year_attribution.csv";
    auto years = open_output(year_path);
    years << prefix << "calendar_year,ticker,contribution,residual\n";
    for (const auto& row : result.calendar_years) years << metadata << ',' << row.year << ',' << row.ticker << ',' << row.contribution << ',' << row.residual << '\n';

    const std::string regime_path = directory + "/regime_attribution.csv";
    auto regimes = open_output(regime_path);
    regimes << prefix << "regime,ticker,contribution,observations,trades,rebalances,status\n";
    for (const auto& row : result.regimes) regimes << metadata << ',' << row.regime << ',' << row.ticker << ',' << row.contribution << ','
        << row.observations << ',' << row.trades << ',' << row.rebalances << ",existing_causal_regime_method\n";
    const std::string window_path = directory + "/walk_forward_window_attribution.csv";
    auto windows = open_output(window_path);
    windows << prefix << "window_id,ticker,contribution,starting_capital,ending_capital,residual,status\n";
    windows << metadata << ",-1,no_ticker,0,0,0,0,not_applicable_to_portfolio_policy_run\n";

    for (const auto& item : std::vector<std::pair<std::ofstream*, std::string>>{{&daily, daily_path}, {&reconciliation, reconciliation_path},
        {&summary, summary_path}, {&cash, cash_path}, {&costs, cost_path}, {&actions, action_path}, {&rebalances, rebalance_path},
        {&benchmark, benchmark_path}, {&drawdowns, drawdown_path}, {&risk, risk_path}, {&years, year_path},
        {&regimes, regime_path}, {&windows, window_path}}) verify_output(*item.first, item.second);
}

void CsvResultExporter::write_statistics(const quant::analytics::StatisticalResult& r,
    const quant::analytics::MultipleTestingResult& mt, const std::string& directory) {
    std::error_code ec; std::filesystem::create_directories(directory,ec); if(ec)throw std::runtime_error("Could not create statistics directory: "+ec.message());
    std::ostringstream m;m<<r.schema_version<<','<<r.experiment_id<<','<<r.method<<','<<r.rng_engine<<','<<r.rng_mapping<<','<<r.stochastic_methodology_version<<','<<r.seed<<','<<r.simulations<<','<<r.block_length<<','<<r.input_series<<','<<r.benchmark<<','<<r.confidence_level<<','<<r.candidate_count<<','<<r.observation_count<<','<<r.annualization_method;
    const std::string meta=m.str(),p="schema_version,experiment_id,method,rng_engine,rng_mapping,stochastic_methodology_version,seed,simulation_count,block_length,input_series,benchmark,confidence_level,candidate_count,observation_count,annualization_method,";
    auto summary=open_output(directory+"/bootstrap_summary.csv");summary<<p<<"metric,mean,median,standard_deviation,lower_bound,upper_bound,probability\n";
    auto ci=[&](const char*n,const quant::analytics::ConfidenceInterval&c,double pr){summary<<meta<<','<<n<<','<<c.mean<<','<<c.median<<','<<c.standard_deviation<<','<<c.lower<<','<<c.upper<<','<<pr<<'\n';};
    ci("cumulative_return",r.cumulative_return_ci,r.probability_loss);ci("sharpe",r.sharpe_ci,r.probability_sharpe_positive);
    auto input=open_output(directory+"/statistical_input_series.csv");input<<p<<"date,return,benchmark_return,active_return\n";for(std::size_t i=0;i<r.input_returns.size();++i){double b=i<r.benchmark_returns.size()?r.benchmark_returns[i].value:0.0;input<<meta<<','<<r.input_returns[i].date<<','<<r.input_returns[i].value<<','<<b<<','<<r.input_returns[i].value-b<<'\n';}
    auto dist=open_output(directory+"/bootstrap_metric_distributions.csv");dist<<p<<"simulation,cumulative_return,annualized_return,volatility,sharpe,sortino,max_drawdown,calmar,terminal_wealth,active_return,information_ratio\n";
    for(auto&x:r.metrics)dist<<meta<<','<<x.simulation<<','<<x.cumulative_return<<','<<x.annualized_return<<','<<x.volatility<<','<<x.sharpe<<','<<x.sortino<<','<<x.max_drawdown<<','<<x.calmar<<','<<x.terminal_wealth<<','<<x.active_return<<','<<x.information_ratio<<'\n';
    auto paths=open_output(directory+"/bootstrap_paths_sample.csv");paths<<p<<"path,step,return\n";for(std::size_t i=0;i<r.sampled_paths.size();++i)for(std::size_t j=0;j<r.sampled_paths[i].size();++j)paths<<meta<<','<<i<<','<<j<<','<<r.sampled_paths[i][j]<<'\n';
    auto sharpe=open_output(directory+"/sharpe_inference.csv");sharpe<<p<<"sharpe_mean,sharpe_lower,sharpe_upper,probability_sharpe_positive,probability_sharpe_exceeds_benchmark\n"<<meta<<','<<r.sharpe_ci.mean<<','<<r.sharpe_ci.lower<<','<<r.sharpe_ci.upper<<','<<r.probability_sharpe_positive<<','<<r.probability_sharpe_exceeds_benchmark<<'\n';
    auto multi=open_output(directory+"/multiple_testing_summary.csv");multi<<p<<"test_method,eligible_count,observed_best_mean,p_value,null_hypothesis\n"<<meta<<','<<mt.method<<','<<mt.eligible_count<<','<<mt.observed_best_mean<<','<<mt.p_value<<",no_candidate_outperforms_benchmark_in_expected_return\n";
    auto history=open_output(directory+"/parameter_selection_history.csv");history<<p<<"window_id,selected_parameter,status\n"<<meta<<",-1,not_applicable,portfolio_policy_has_no_parameter_grid\n";
    auto stability=open_output(directory+"/parameter_stability.csv");stability<<p<<"parameter,selection_frequency,changes,stability_score,status\n"<<meta<<",not_applicable,0,0,0,portfolio_policy_has_no_parameter_grid\n";
    auto degradation=open_output(directory+"/oos_degradation.csv");degradation<<p<<"in_sample_return,oos_return,return_degradation,in_sample_sharpe,oos_sharpe,sharpe_degradation,status\n"<<meta<<",0,0,0,0,0,0,not_available_for_portfolio_policy_series\n";
    auto strategy=open_output(directory+"/strategy_robustness.csv");strategy<<p<<"strategy,return_lower,return_upper,sharpe_lower,sharpe_upper,probability_positive_active,status\n"<<meta<<",not_applicable,0,0,0,0,0,portfolio_policy_input\n";
    auto policy=open_output(directory+"/portfolio_policy_robustness.csv");policy<<p<<"policy,return_lower,return_upper,sharpe_lower,sharpe_upper,probability_positive_active,probability_loss\n"<<meta<<','<<r.experiment_id<<','<<r.cumulative_return_ci.lower<<','<<r.cumulative_return_ci.upper<<','<<r.sharpe_ci.lower<<','<<r.sharpe_ci.upper<<','<<r.probability_positive_active<<','<<r.probability_loss<<'\n';
    auto attr=open_output(directory+"/attribution_robustness.csv");attr<<p<<"component,estimate,status\n"<<meta<<",joint_asset_concentration,0,requires_joint_contribution_series_bootstrap\n";
    auto warnings=open_output(directory+"/statistical_warnings.csv");warnings<<p<<"warning\n";if(r.warnings.empty())warnings<<meta<<",none\n";else for(auto&w:r.warnings)warnings<<meta<<','<<w<<'\n';
    std::ostringstream manifest;manifest<<"{\n  \"schema_version\": 3,\n  \"experiment_id\": \""<<r.experiment_id<<"\",\n  \"method\": \""<<r.method<<"\",\n  \"rng_engine\": \""<<r.rng_engine<<"\",\n  \"rng_mapping\": \""<<r.rng_mapping<<"\",\n  \"stochastic_methodology_version\": "<<r.stochastic_methodology_version<<",\n  \"seed\": "<<r.seed<<",\n  \"simulation_count\": "<<r.simulations<<",\n  \"block_length\": "<<r.block_length<<",\n  \"input_series\": \""<<r.input_series<<"\",\n  \"observation_count\": "<<r.observation_count<<",\n  \"assumptions\": \""<<r.assumptions<<"\"\n}\n";
    JsonManifestExporter::write_text(directory+"/statistical_manifest.json",manifest.str());
}

void JsonManifestExporter::write_text(const std::string& filepath, const std::string& json) {
    const std::filesystem::path path(filepath);
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    if (error) {
        throw std::runtime_error("Could not create manifest directory for " + filepath + ": " + error.message());
    }
    auto output = open_output(filepath);
    output << json;
    verify_output(output, filepath);
}

void JsonManifestExporter::write_resolved_config(
    const std::string& filepath,
    const quant::config::ExperimentConfig& config) {
    std::ostringstream json;
    json << "{\n"
         << "  \"experiment_name\": \"" << config.name << "\",\n"
         << "  \"strategy\": \"" << config.strategy << "\",\n"
         << "  \"starting_capital\": " << config.execution.starting_capital << ",\n"
         << "  \"commission_bps\": " << config.execution.commission_bps << ",\n"
         << "  \"slippage_bps\": " << config.execution.slippage_bps << ",\n"
         << "  \"train_window_days\": " << config.walk_forward.train_days << ",\n"
         << "  \"test_window_days\": " << config.walk_forward.test_days << ",\n"
         << "  \"step_days\": " << config.walk_forward.step_days << ",\n"
         << "  \"window_mode\": \"" << config.walk_forward.window_mode << "\",\n"
         << "  \"train_years\": " << config.walk_forward.train_years << ",\n"
         << "  \"test_months\": " << config.walk_forward.test_months << ",\n"
         << "  \"step_months\": " << config.walk_forward.step_months << ",\n"
         << "  \"oos_continuity_policy\": \"" << config.walk_forward.continuity_policy << "\",\n"
         << "  \"boundary_position_policy\": \"" << config.walk_forward.boundary_position_policy << "\",\n"
         << "  \"benchmark\": \"" << config.benchmark.ticker << "\",\n"
         << "  \"benchmark_execution_policy\": \"first_close_decision_next_open_integer_shares_5pct_cash_reserve\",\n"
         << "  \"benchmark_cost_policy\": \"strategy_costs_for_net_zero_costs_for_gross\",\n"
         << "  \"excess_return_basis\": \"net_strategy_minus_net_benchmark\",\n"
         << "  \"regime_information_cutoff\": \"close_t_minus_1_for_open_t_and_start_of_return_interval\",\n"
         << "  \"volatility_threshold_method\": \"expanding_median_strictly_prior_volatility\",\n"
         << "  \"parameter_selection_objective\": \"" << config.parameter_selection.objective << "\",\n"
         << "  \"minimum_trade_requirement\": " << config.parameter_selection.minimum_trades << ",\n"
         << "  \"random_seed\": " << config.bootstrap.random_seed << ",\n"
         << "  \"rng_engine\": \"mt19937\",\n"
         << "  \"rng_mapping\": \"portable_bounded_v1\",\n"
         << "  \"stochastic_methodology_version\": 2,\n"
         << "  \"calendar_mode\": \"" << config.calendar.valuation_mode << "\",\n"
         << "  \"stale_mark_policy\": \"" << config.calendar.stale_mark_policy << "\",\n"
         << "  \"max_stale_calendar_days\": " << config.calendar.max_stale_calendar_days << ",\n"
         << "  \"missing_bar_policy\": \"" << config.calendar.missing_bar_policy << "\",\n"
         << "  \"rebalance_closed_asset_policy\": \"" << config.calendar.rebalance_closed_asset_policy << "\",\n"
         << "  \"annualization_method\": \"" << config.calendar.annualization_method << "\",\n"
         << "  \"adjustment_policy\": \"" << config.adjustment.policy << "\",\n"
         << "  \"result_schema_version\": " << config.result_schema_version << "\n"
         << "}\n";
    write_text(filepath, json.str());
}

}  // namespace quant::io
