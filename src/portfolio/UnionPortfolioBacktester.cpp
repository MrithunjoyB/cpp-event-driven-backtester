#include "PortfolioBacktester.h"

#include "ExecutionHandler.h"
#include "quant/domain/Date.h"
#include "quant/domain/Errors.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace {
double average(const std::vector<double>& values) {
    return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}
double deviation(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double mean = average(values);
    double sum = 0.0;
    for (double value : values) sum += (value - mean) * (value - mean);
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}
double covariance(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    if (lhs.size() != rhs.size() || lhs.size() < 2) return 0.0;
    const double lhs_mean = average(lhs);
    const double rhs_mean = average(rhs);
    double sum = 0.0;
    for (std::size_t i = 0; i < lhs.size(); ++i) sum += (lhs[i] - lhs_mean) * (rhs[i] - rhs_mean);
    return sum / static_cast<double>(lhs.size() - 1);
}
double inferred_periods(const std::vector<quant::market_data::TradingSession>& sessions) {
    if (sessions.size() < 2) return 365.25;
    const int days = quant::Date::parse(sessions.front().date).days_until(quant::Date::parse(sessions.back().date));
    return days > 0 ? static_cast<double>(sessions.size() - 1) * 365.25 / static_cast<double>(days) : 365.25;
}
}

PortfolioBacktestResult PortfolioBacktester::run_union() {
    using namespace quant::market_data;
    std::map<std::string, std::vector<Bar>> history;
    load_data(history);
    if (config_.adjustment_policy != AdjustmentPolicy::RawPrice) {
        for (auto& asset : history) {
            for (auto& bar : asset.second) {
                if (!bar.has_adjusted_close) {
                    throw quant::DataError("Adjustment policy " + to_string(config_.adjustment_policy) +
                        " requires AdjustedClose for " + asset.first + " on " + bar.date);
                }
                const double factor = bar.adjusted_close / bar.close;
                bar.open *= factor;
                bar.high *= factor;
                bar.low *= factor;
                bar.close = bar.adjusted_close;
            }
        }
    }
    const auto sessions = TradingCalendar::build(history, config_.calendar);
    if (sessions.size() < 2) throw quant::SimulationError("Union portfolio requires at least two valuation dates");
    const auto schedule = config_.rebalance_frequency == RebalanceFrequency::Weekly
        ? TradingCalendar::weekly_first_on_or_after_monday(sessions)
        : TradingCalendar::monthly_first_valuation(sessions);
    std::map<std::size_t, RebalanceSchedulePoint> scheduled;
    for (const auto& point : schedule) scheduled[point.valuation_index] = point;

    AllocationPolicy policy(config_.allocation);
    ExecutionHandler execution(config_.transaction_cost_rate, config_.slippage_rate);
    std::map<std::string, double> shares;
    std::map<std::string, double> target_weights;
    for (const auto& ticker : config_.tickers) { shares[ticker] = 0.0; target_weights[ticker] = 0.0; }
    double cash = config_.starting_capital;
    double peak = cash;
    double total_turnover = 0.0;
    double total_costs = 0.0;
    int rebalance_id = 0;
    int weekend_count = 0;
    int stale_count = 0;
    double cash_sum = 0.0;
    double exposure_sum = 0.0;
    PortfolioBacktestResult result;
    std::vector<double> returns;
    std::vector<double> benchmark_returns;
    std::map<std::string, double> first_asset_marks;
    std::map<std::string, double> last_asset_marks;
    std::string deferred_schedule_date;
    std::string deferred_decision_date;
    std::map<std::string, double> deferred_target_weights;

    for (std::size_t d = 0; d < sessions.size(); ++d) {
        const auto& session = sessions[d];
        const int weekday = quant::Date::parse(session.date).iso_weekday();
        if (weekday >= 6) ++weekend_count;
        for (const auto& ticker : config_.tickers) {
            const auto& state = session.assets.at(ticker);
            if (!state.tradability.has_bar) continue;
            const auto& bar = *state.bar;
            if (bar.stock_splits > 0.0) {
                const double before_quantity = shares[ticker];
                const double before_value = cash + before_quantity * bar.close * bar.stock_splits;
                if (config_.adjustment_policy == AdjustmentPolicy::RawPrice) shares[ticker] *= bar.stock_splits;
                const double after_value = cash + shares[ticker] * bar.close;
                result.corporate_actions.push_back({session.date, ticker, "stock_split", bar.stock_splits,
                    before_quantity, shares[ticker], 0.0, before_value, after_value,
                    to_string(config_.adjustment_policy), "csv:StockSplits"});
            }
            if (bar.dividends > 0.0) {
                const double before_value = std::fma(shares[ticker], bar.close, cash);
                const double cash_effect = config_.adjustment_policy == AdjustmentPolicy::RawPrice
                    ? shares[ticker] * bar.dividends : 0.0;
                cash += cash_effect;
                result.corporate_actions.push_back({session.date, ticker, "cash_dividend", bar.dividends,
                    shares[ticker], shares[ticker], cash_effect, before_value, before_value + cash_effect,
                    to_string(config_.adjustment_policy), "csv:Dividends"});
            }
        }
        auto schedule_it = scheduled.find(d);
        const bool has_rebalance = !deferred_schedule_date.empty() || schedule_it != scheduled.end();
        const std::string scheduled_date = !deferred_schedule_date.empty()
            ? deferred_schedule_date : (schedule_it != scheduled.end() ? schedule_it->second.scheduled_date : "");
        if (has_rebalance) {
            std::map<std::string, std::size_t> decision_indices;
            int closed = 0;
            for (const auto& ticker : config_.tickers) {
                const auto& state = session.assets.at(ticker);
                if (!state.tradability.execution_allowed) { ++closed; continue; }
                if (state.bar_index > 0) decision_indices[ticker] = state.bar_index - 1;
            }
            const std::string decision_date = deferred_decision_date.empty() ? sessions[d - 1].date : deferred_decision_date;
            const auto candidate_weights = deferred_target_weights.empty()
                ? policy.target_weights(config_.tickers, history, decision_indices) : deferred_target_weights;
            const bool defer_all = closed > 0 && config_.calendar.closed_asset_policy == ClosedAssetPolicy::Defer;
            if (defer_all) {
                deferred_schedule_date = scheduled_date;
                if (deferred_decision_date.empty()) deferred_decision_date = decision_date;
                if (deferred_target_weights.empty()) deferred_target_weights = candidate_weights;
            } else {
                target_weights = candidate_weights;
                double holdings_open = 0.0;
                for (const auto& ticker : config_.tickers) {
                    const auto& state = session.assets.at(ticker);
                    const double price = state.tradability.execution_allowed ? state.bar->open : state.mark.price;
                    if (!state.mark.available && shares[ticker] != 0.0)
                        throw quant::SimulationError("No valuation mark for held asset " + ticker + " on " + session.date);
                    holdings_open += shares[ticker] * price;
                }
                const double value_open = cash + holdings_open;
                double rebalance_turnover = 0.0;
                for (const auto& ticker : config_.tickers) {
                    const auto& state = session.assets.at(ticker);
                    if (!state.tradability.execution_allowed) continue;
                    const double price = state.bar->open;
                    const double current = shares[ticker] * price;
                    const double target = value_open * target_weights[ticker];
                    const int quantity = static_cast<int>(std::floor(std::min(shares[ticker], std::max(0.0, current - target) / price)));
                    if (quantity <= 0 || current - target <= config_.allocation.min_trade_value) continue;
                    const auto fill = execution.execute_order({EventType::Order, session.date, ticker, policy.name(), OrderSide::Sell, quantity}, price);
                    shares[ticker] -= fill.quantity;
                    cash += fill.gross_value - fill.transaction_cost;
                    total_costs += fill.transaction_cost + fill.slippage_cost;
                    total_turnover += fill.gross_value;
                    rebalance_turnover += fill.gross_value;
                    result.fills.push_back({rebalance_id, session.date, ticker, "SELL", fill.quantity, fill.fill_price,
                        fill.transaction_cost, fill.slippage_cost, cash, scheduled_date,
                        decision_date, session.date});
                }
                for (const auto& ticker : config_.tickers) {
                    const auto& state = session.assets.at(ticker);
                    if (!state.tradability.execution_allowed) continue;
                    const double price = state.bar->open;
                    const double current = shares[ticker] * price;
                    const double desired = value_open * target_weights[ticker] - current;
                    if (desired <= config_.allocation.min_trade_value) continue;
                    int quantity = static_cast<int>(std::floor(desired / (price * (1.0 + config_.slippage_rate) * (1.0 + config_.transaction_cost_rate))));
                    if (quantity <= 0) continue;
                    auto fill = execution.execute_order({EventType::Order, session.date, ticker, policy.name(), OrderSide::Buy, quantity}, price);
                    double required = fill.gross_value + fill.transaction_cost;
                    if (required > cash) {
                        quantity = static_cast<int>(std::floor(cash / (fill.fill_price * (1.0 + config_.transaction_cost_rate))));
                        if (quantity <= 0) continue;
                        fill = execution.execute_order({EventType::Order, session.date, ticker, policy.name(), OrderSide::Buy, quantity}, price);
                        required = fill.gross_value + fill.transaction_cost;
                    }
                    if (required > cash + 1e-8) continue;
                    shares[ticker] += fill.quantity;
                    cash -= required;
                    total_costs += fill.transaction_cost + fill.slippage_cost;
                    total_turnover += fill.gross_value;
                    rebalance_turnover += fill.gross_value;
                    result.fills.push_back({rebalance_id, session.date, ticker, "BUY", fill.quantity, fill.fill_price,
                        fill.transaction_cost, fill.slippage_cost, cash, scheduled_date,
                        decision_date, session.date});
                }
                result.target_weights.push_back(target_weights);
                result.rebalance_dates.push_back(session.date);
                result.turnover_by_rebalance.push_back(value_open > 0.0 ? rebalance_turnover / value_open : 0.0);
                result.rebalances.push_back({rebalance_id, scheduled_date, decision_date,
                    session.date, config_.calendar.closed_asset_policy == ClosedAssetPolicy::Defer ? closed : 0,
                    config_.calendar.closed_asset_policy == ClosedAssetPolicy::SkipAsset ? closed : 0,
                    closed > 0, to_string(config_.calendar.closed_asset_policy),
                    value_open > 0.0 ? rebalance_turnover / value_open : 0.0});
                ++rebalance_id;
                deferred_schedule_date.clear();
                deferred_decision_date.clear();
                deferred_target_weights.clear();
            }
        }

        double holdings = 0.0;
        for (const auto& ticker : config_.tickers) {
            const auto& state = session.assets.at(ticker);
            if (!state.mark.available && shares[ticker] != 0.0)
                throw quant::SimulationError("Valuation mark unavailable for held asset " + ticker + " on " + session.date);
            holdings = std::fma(shares[ticker], state.mark.price, holdings);
            if (!state.mark.current) ++stale_count;
        }
        const double value = cash + holdings;
        peak = std::max(peak, value);
        const double drawdown = peak > 0.0 ? value / peak - 1.0 : 0.0;
        const double exposure = value > 0.0 ? holdings / value : 0.0;
        result.equity_curve.push_back({session.date, value, cash, holdings, value / config_.starting_capital - 1.0, drawdown, exposure});
        if (d > 0 && result.equity_curve[d - 1].portfolio_value > 0.0)
            returns.push_back(value / result.equity_curve[d - 1].portfolio_value - 1.0);
        cash_sum += value > 0.0 ? cash / value : 0.0;
        exposure_sum += exposure;
        for (const auto& ticker : config_.tickers) {
            const auto& state = session.assets.at(ticker);
            const double marked = shares[ticker] * state.mark.price;
            const double weight = value > 0.0 ? marked / value : 0.0;
            result.positions.push_back({session.date, ticker, shares[ticker], state.mark.price, marked, target_weights[ticker], weight, rebalance_id - 1});
            result.valuations.push_back({session.date, ticker, state.tradability.tradable, state.tradability.has_bar,
                state.mark.price, state.mark.source, state.mark.stale_age_days, shares[ticker], marked, weight});
            if (state.mark.available) {
                if (first_asset_marks.count(ticker) == 0) first_asset_marks[ticker] = state.mark.price;
                last_asset_marks[ticker] = state.mark.price;
            }
        }
        if (d > 0) {
            const auto current = session.assets.find(config_.benchmark_ticker);
            const auto previous = sessions[d - 1].assets.find(config_.benchmark_ticker);
            if (current != session.assets.end() && previous != sessions[d - 1].assets.end() &&
                current->second.mark.available && previous->second.mark.available && previous->second.mark.price > 0.0) {
                benchmark_returns.push_back(current->second.mark.price / previous->second.mark.price - 1.0);
            } else {
                benchmark_returns.push_back(0.0);
            }
        }
    }

    const double periods = config_.annualization_method == "configured" ? config_.configured_periods_per_year : inferred_periods(sessions);
    auto& summary = result.summary;
    summary.schema_version = 3;
    summary.policy_name = policy.name();
    summary.calendar_mode = "union";
    summary.valuation_frequency = "union_calendar_observations";
    summary.observations_per_year = periods;
    summary.annualization_method = config_.annualization_method;
    summary.total_valuation_observations = static_cast<int>(sessions.size());
    summary.weekend_observations = weekend_count;
    summary.stale_mark_observations = stale_count;
    summary.total_return = result.equity_curve.back().portfolio_value / config_.starting_capital - 1.0;
    const double years = static_cast<double>(returns.size()) / periods;
    summary.annualized_return = years > 0.0 ? std::pow(1.0 + summary.total_return, 1.0 / years) - 1.0 : 0.0;
    summary.volatility = deviation(returns) * std::sqrt(periods);
    summary.sharpe = summary.volatility > 0.0 ? average(returns) * periods / summary.volatility : 0.0;
    std::vector<double> downside;
    for (double value : returns) if (value < 0.0) downside.push_back(value);
    const double downside_deviation = deviation(downside) * std::sqrt(periods);
    summary.sortino = downside_deviation > 0.0 ? average(returns) * periods / downside_deviation : 0.0;
    for (const auto& point : result.equity_curve) summary.max_drawdown = std::min(summary.max_drawdown, point.drawdown);
    summary.calmar = summary.max_drawdown < 0.0 ? summary.annualized_return / std::abs(summary.max_drawdown) : 0.0;
    summary.var_95 = value_at_risk_95(returns);
    summary.expected_shortfall_95 = expected_shortfall_95(returns);
    summary.turnover = total_turnover / config_.starting_capital;
    summary.total_transaction_costs = total_costs;
    summary.number_of_rebalances = rebalance_id;
    summary.number_of_fills = static_cast<int>(result.fills.size());
    summary.average_cash_allocation = cash_sum / static_cast<double>(sessions.size());
    summary.average_gross_exposure = exposure_sum / static_cast<double>(sessions.size());
    if (benchmark_returns.size() == returns.size()) {
        double benchmark_wealth = 1.0;
        for (double value : benchmark_returns) benchmark_wealth *= 1.0 + value;
        summary.spy_benchmark_return = benchmark_wealth - 1.0;
        summary.excess_return = summary.total_return - summary.spy_benchmark_return;
        const double benchmark_variance = deviation(benchmark_returns) * deviation(benchmark_returns);
        summary.beta = benchmark_variance > 0.0 ? covariance(returns, benchmark_returns) / benchmark_variance : 0.0;
        summary.alpha = (average(returns) - summary.beta * average(benchmark_returns)) * periods;
        std::vector<double> active;
        active.reserve(returns.size());
        for (std::size_t i = 0; i < returns.size(); ++i) active.push_back(returns[i] - benchmark_returns[i]);
        const double tracking_error = deviation(active) * std::sqrt(periods);
        summary.information_ratio = tracking_error > 0.0 ? average(active) * periods / tracking_error : 0.0;
    }
    double equal_weight_wealth = 0.0;
    int benchmark_assets = 0;
    for (const auto& ticker : config_.tickers) {
        if (first_asset_marks.count(ticker) && first_asset_marks[ticker] > 0.0 && last_asset_marks.count(ticker)) {
            equal_weight_wealth += last_asset_marks[ticker] / first_asset_marks[ticker];
            ++benchmark_assets;
        }
    }
    summary.equal_weight_benchmark_return = benchmark_assets > 0
        ? equal_weight_wealth / static_cast<double>(benchmark_assets) - 1.0 : 0.0;
    return result;
}
