#include "quant/analytics/PortfolioAttribution.h"

#include "quant/domain/Errors.h"
#include "quant/market_data/CorporateAction.h"
#include "MarketData.h"
#include "ResearchMethodology.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>

namespace quant::analytics {
namespace {
using MarkMap = std::map<std::string, std::map<std::string, PortfolioValuationMark>>;

double mean(const std::vector<double>& values) {
    return values.empty() ? 0.0 : std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}
double covariance(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    if (lhs.size() != rhs.size() || lhs.size() < 2) return 0.0;
    const double lm = mean(lhs);
    const double rm = mean(rhs);
    double sum = 0.0;
    for (std::size_t i = 0; i < lhs.size(); ++i) sum += (lhs[i] - lm) * (rhs[i] - rm);
    return sum / static_cast<double>(lhs.size() - 1);
}
double stdev(const std::vector<double>& values) { return std::sqrt(std::max(0.0, covariance(values, values))); }

const PortfolioValuationMark& mark_at(const MarkMap& marks, const std::string& date, const std::string& ticker) {
    auto date_it = marks.find(date);
    if (date_it == marks.end() || date_it->second.count(ticker) == 0)
        throw MethodologyError("Attribution mark missing for " + ticker + " on " + date);
    return date_it->second.at(ticker);
}

double summary_value(const std::vector<AttributionSummaryRow>& rows, const std::string& component) {
    for (const auto& row : rows) if (row.component == component) return row.contribution;
    return 0.0;
}
}

PortfolioAttributionResult PortfolioAttributionAnalyzer::analyze(
    const PortfolioBacktestResult& portfolio,
    const PortfolioBacktestConfig& config,
    const std::string& experiment_id,
    double residual_tolerance) {
    if (portfolio.equity_curve.size() < 2) throw MethodologyError("Attribution requires at least two portfolio valuations");
    if (residual_tolerance <= 0.0 || !std::isfinite(residual_tolerance))
        throw ConfigurationError("Attribution residual tolerance must be positive and finite");

    PortfolioAttributionResult result;
    result.experiment_id = experiment_id;
    result.policy = portfolio.summary.policy_name;
    result.benchmark = config.benchmark_ticker;
    result.adjustment_basis = quant::market_data::to_string(config.adjustment_policy);
    result.calendar_mode = quant::market_data::to_string(config.calendar.mode);
    result.residual_tolerance = residual_tolerance;

    MarkMap marks;
    std::set<std::string> tickers;
    for (const auto& mark : portfolio.valuations) {
        if (!marks[mark.date].emplace(mark.ticker, mark).second)
            throw MethodologyError("Duplicate attribution mark for " + mark.ticker + " on " + mark.date);
        tickers.insert(mark.ticker);
    }
    std::map<std::string, std::vector<PortfolioFill>> fills;
    for (const auto& fill : portfolio.fills) fills[fill.date].push_back(fill);
    std::map<std::string, std::vector<PortfolioCorporateActionRecord>> actions;
    for (const auto& action : portfolio.corporate_actions) actions[action.date].push_back(action);

    std::map<std::string, double> component_totals;
    std::map<std::string, std::map<int, double>> year_totals;
    std::map<std::string, std::vector<double>> component_returns;
    std::vector<double> portfolio_returns;
    std::vector<double> benchmark_returns;
    std::vector<RegimePoint> causal_regimes;
    try {
        MarketData benchmark_data;
        if (benchmark_data.load_csv(config.benchmark_ticker, config.data_dir + "/" + config.benchmark_ticker + ".csv"))
            causal_regimes = classify_causal_regimes(benchmark_data.bars(config.benchmark_ticker));
    } catch (const std::exception&) {
        causal_regimes.clear();
    }
    std::map<std::pair<std::string, std::string>, RegimeContribution> regime_totals;

    for (std::size_t i = 1; i < portfolio.equity_curve.size(); ++i) {
        const auto& beginning = portfolio.equity_curve[i - 1];
        const auto& ending = portfolio.equity_curve[i];
        PeriodAttribution period;
        period.start_date = beginning.date;
        period.end_date = ending.date;
        period.beginning_value = beginning.portfolio_value;
        period.ending_value = ending.portfolio_value;
        for (const auto& ticker : tickers) {
            const auto& start = mark_at(marks, beginning.date, ticker);
            const auto& finish = mark_at(marks, ending.date, ticker);
            AssetContribution asset;
            asset.start_date = beginning.date;
            asset.end_date = ending.date;
            asset.ticker = ticker;
            asset.beginning_quantity = start.position_quantity;
            asset.ending_quantity = finish.position_quantity;
            asset.beginning_mark = start.mark_price;
            asset.ending_mark = finish.mark_price;
            asset.beginning_value = start.marked_value;
            asset.ending_value = finish.marked_value;
            asset.beginning_weight = start.actual_weight;
            asset.ending_weight = finish.actual_weight;
            asset.tradable = finish.tradable;
            asset.stale_mark = finish.mark_source == "last_known_close";
            asset.stale_age_days = finish.stale_age_days;
            double reference_investment = 0.0;
            for (const auto& fill : fills[ending.date]) {
                if (fill.ticker != ticker) continue;
                const double reference_price = fill.quantity > 0
                    ? (fill.side == "BUY" ? fill.price - fill.slippage_cost / fill.quantity
                                           : fill.price + fill.slippage_cost / fill.quantity)
                    : fill.price;
                const double signed_flow = fill.side == "BUY" ? -fill.quantity * fill.price : fill.quantity * fill.price;
                asset.trade_cash_flow += signed_flow;
                reference_investment += fill.side == "BUY" ? fill.quantity * reference_price : -fill.quantity * reference_price;
                asset.commission += fill.transaction_cost;
                asset.slippage_cost += fill.slippage_cost;
            }
            for (const auto& action : actions[ending.date]) {
                if (action.ticker != ticker) continue;
                if (action.action_type == "cash_dividend") asset.dividend_income += action.cash_effect;
                if (action.action_type == "stock_split") asset.split_adjustment += action.portfolio_value_after - action.portfolio_value_before;
            }
            asset.market_pnl = asset.ending_value - asset.beginning_value - reference_investment - asset.split_adjustment;
            asset.net_contribution = asset.market_pnl + asset.dividend_income + asset.split_adjustment
                - asset.commission - asset.spread_cost - asset.slippage_cost;
            period.market_pnl += asset.market_pnl;
            period.dividend_income += asset.dividend_income;
            period.corporate_action_effect += asset.split_adjustment;
            period.commission += asset.commission;
            period.spread_cost += asset.spread_cost;
            period.slippage_cost += asset.slippage_cost;
            component_totals[ticker] += asset.net_contribution;
            year_totals[ticker][std::stoi(ending.date.substr(0, 4))] += asset.net_contribution;
            component_returns[ticker].push_back(beginning.portfolio_value > 0.0
                ? asset.net_contribution / beginning.portfolio_value : 0.0);
            const RegimePoint* regime = causal_regimes.empty() ? nullptr
                : regime_for_return_interval(causal_regimes, beginning.date, ending.date);
            const std::string regime_name = regime != nullptr && regime->available ? regime->regime : "unavailable";
            asset.regime = regime_name;
            auto& regime_row = regime_totals[{regime_name, ticker}];
            regime_row.regime = regime_name;
            regime_row.ticker = ticker;
            regime_row.contribution += asset.net_contribution;
            ++regime_row.observations;
            regime_row.trades += static_cast<int>(std::count_if(fills[ending.date].begin(), fills[ending.date].end(),
                [&ticker](const PortfolioFill& fill) { return fill.ticker == ticker; }));
            regime_row.rebalances += static_cast<int>(std::count_if(portfolio.rebalances.begin(), portfolio.rebalances.end(),
                [&ending](const PortfolioRebalanceRecord& rebalance) { return rebalance.execution_date == ending.date; }));
            result.assets.push_back(asset);
        }
        const double explained = period.market_pnl + period.dividend_income + period.corporate_action_effect
            + period.cash_return - period.commission - period.spread_cost - period.slippage_cost
            + period.external_cash_flow;
        period.residual = period.ending_value - period.beginning_value - explained;
        const double allowed = residual_tolerance * std::max(1.0, std::abs(period.beginning_value));
        if (!std::isfinite(period.residual) || std::abs(period.residual) > allowed)
            throw MethodologyError("Attribution residual " + std::to_string(period.residual) +
                " exceeds tolerance on " + period.end_date);
        result.total_residual += period.residual;
        result.periods.push_back(period);
        const double benchmark_period_return = tickers.count(config.benchmark_ticker)
            ? (mark_at(marks, beginning.date, config.benchmark_ticker).mark_price > 0.0
                ? mark_at(marks, ending.date, config.benchmark_ticker).mark_price /
                    mark_at(marks, beginning.date, config.benchmark_ticker).mark_price - 1.0 : 0.0)
            : 0.0;
        result.cash.push_back({beginning.date, ending.date, beginning.cash, ending.cash,
            0.5 * (beginning.cash + ending.cash), beginning.portfolio_value > 0.0
                ? 0.5 * (beginning.cash + ending.cash) / beginning.portfolio_value : 0.0,
            0.0, -0.5 * (beginning.cash + ending.cash) * benchmark_period_return,
            period.dividend_income, period.commission + period.spread_cost + period.slippage_cost});
        portfolio_returns.push_back(ending.portfolio_value / beginning.portfolio_value - 1.0);
        if (tickers.count(config.benchmark_ticker)) {
            const auto& benchmark_start = mark_at(marks, beginning.date, config.benchmark_ticker);
            const auto& benchmark_end = mark_at(marks, ending.date, config.benchmark_ticker);
            benchmark_returns.push_back(benchmark_start.mark_price > 0.0
                ? benchmark_end.mark_price / benchmark_start.mark_price - 1.0 : 0.0);
        }
    }
    for (const auto& action : portfolio.corporate_actions) {
        result.corporate_actions.push_back({action.date, action.ticker, action.action_type, action.value,
            action.action_type == "stock_split" ? action.portfolio_value_after - action.portfolio_value_before : action.cash_effect,
            action.cash_effect, action.policy, action.source});
    }

    const double initial = portfolio.equity_curve.front().portfolio_value;
    const double net_profit = portfolio.equity_curve.back().portfolio_value - initial;
    for (const auto& total : component_totals) {
        result.summary.push_back({total.first, total.second, initial > 0.0 ? total.second / initial : 0.0,
            std::abs(net_profit) > 1e-12 ? total.second / net_profit : 0.0});
    }
    double commission = 0.0, slippage = 0.0, dividends = 0.0, market = 0.0;
    for (const auto& period : result.periods) {
        commission += period.commission;
        slippage += period.slippage_cost;
        dividends += period.dividend_income;
        market += period.market_pnl;
    }
    result.summary.push_back({"MARKET_PNL", market, market / initial, std::abs(net_profit) > 1e-12 ? market / net_profit : 0.0});
    result.summary.push_back({"DIVIDENDS", dividends, dividends / initial, std::abs(net_profit) > 1e-12 ? dividends / net_profit : 0.0});
    result.summary.push_back({"COMMISSION", -commission, -commission / initial, std::abs(net_profit) > 1e-12 ? -commission / net_profit : 0.0});
    result.summary.push_back({"SLIPPAGE", -slippage, -slippage / initial, std::abs(net_profit) > 1e-12 ? -slippage / net_profit : 0.0});
    result.summary.push_back({"CASH_RETURN", 0.0, 0.0, 0.0});
    result.summary.push_back({"PORTFOLIO_RETURN", net_profit, net_profit / initial, 1.0});
    result.summary.push_back({"BENCHMARK_RETURN", -portfolio.summary.spy_benchmark_return * initial,
        -portfolio.summary.spy_benchmark_return, 0.0});
    result.summary.push_back({"ACTIVE_RETURN", net_profit - portfolio.summary.spy_benchmark_return * initial,
        net_profit / initial - portfolio.summary.spy_benchmark_return, 0.0});
    result.summary.push_back({"RESIDUAL", result.total_residual, result.total_residual / initial,
        std::abs(net_profit) > 1e-12 ? result.total_residual / net_profit : 0.0});

    const double periods_per_year = portfolio.summary.observations_per_year;
    const double portfolio_sd = stdev(portfolio_returns);
    double component_sum = 0.0;
    for (const auto& ticker : tickers) {
        const double component = portfolio_sd > 0.0
            ? covariance(component_returns[ticker], portfolio_returns) / portfolio_sd * std::sqrt(periods_per_year) : 0.0;
        component_sum += component;
        double beta_component = 0.0;
        if (benchmark_returns.size() == portfolio_returns.size()) {
            const double benchmark_variance = covariance(benchmark_returns, benchmark_returns);
            beta_component = benchmark_variance > 0.0
                ? covariance(component_returns[ticker], benchmark_returns) / benchmark_variance : 0.0;
        }
        result.risk.push_back({ticker, component, 0.0, beta_component, portfolio_returns.size()});
    }
    const double annualized_volatility = portfolio_sd * std::sqrt(periods_per_year);
    for (auto& row : result.risk) row.percentage_contribution = annualized_volatility > 0.0
        ? row.component_volatility / annualized_volatility : 0.0;
    if (std::abs(component_sum - annualized_volatility) > residual_tolerance * std::max(1.0, annualized_volatility))
        throw MethodologyError("Risk contributions do not reconcile to portfolio volatility");

    for (const auto& ticker_years : year_totals) {
        for (const auto& year : ticker_years.second) result.calendar_years.push_back({year.first, ticker_years.first, year.second, 0.0});
    }
    for (const auto& row : regime_totals) result.regimes.push_back(row.second);

    int episode_id = 0;
    std::size_t peak = 0;
    std::size_t episode_peak = 0;
    std::size_t trough = 0;
    bool in_drawdown = false;
    auto emit_episode = [&](std::size_t recovery, bool unresolved) {
        for (const auto& ticker : tickers) {
            double contribution = 0.0;
            int stale = 0;
            for (const auto& asset : result.assets) {
                if (asset.ticker == ticker && asset.end_date > portfolio.equity_curve[episode_peak].date &&
                    asset.end_date <= portfolio.equity_curve[trough].date) {
                    contribution += asset.net_contribution;
                    stale += asset.stale_mark ? 1 : 0;
                }
            }
            result.drawdowns.push_back({episode_id, portfolio.equity_curve[episode_peak].date,
                portfolio.equity_curve[trough].date, unresolved ? "" : portfolio.equity_curve[recovery].date,
                portfolio.equity_curve[trough].portfolio_value / portfolio.equity_curve[episode_peak].portfolio_value - 1.0,
                portfolio.equity_curve[episode_peak].portfolio_value, portfolio.equity_curve[trough].portfolio_value,
                static_cast<int>(trough - episode_peak), unresolved ? 0 : static_cast<int>(recovery - trough),
                ticker, contribution, stale, unresolved});
        }
        ++episode_id;
    };
    for (std::size_t i = 1; i < portfolio.equity_curve.size(); ++i) {
        if (!in_drawdown && portfolio.equity_curve[i].portfolio_value < portfolio.equity_curve[peak].portfolio_value) {
            in_drawdown = true; episode_peak = peak; trough = i;
        } else if (in_drawdown) {
            if (portfolio.equity_curve[i].portfolio_value < portfolio.equity_curve[trough].portfolio_value) trough = i;
            if (portfolio.equity_curve[i].portfolio_value >= portfolio.equity_curve[episode_peak].portfolio_value) {
                emit_episode(i, false); in_drawdown = false; peak = i;
            }
        } else if (portfolio.equity_curve[i].portfolio_value >= portfolio.equity_curve[peak].portfolio_value) peak = i;
    }
    if (in_drawdown) emit_episode(portfolio.equity_curve.size() - 1, true);

    for (std::size_t i = 0; i < portfolio.rebalances.size(); ++i) {
        const auto& rebalance = portfolio.rebalances[i];
        const std::string next = i + 1 < portfolio.rebalances.size() ? portfolio.rebalances[i + 1].execution_date
                                                                    : portfolio.equity_curve.back().date;
        double gross = 0.0, costs = 0.0, cash_after = 0.0, holding = 0.0;
        for (const auto& fill : portfolio.fills) if (fill.rebalance_id == rebalance.rebalance_id) {
            gross = std::fma(static_cast<double>(fill.quantity), fill.price, gross);
            costs += fill.transaction_cost + fill.slippage_cost;
            cash_after = fill.cash_after;
        }
        for (const auto& asset : result.assets) if (asset.end_date > rebalance.execution_date && asset.end_date <= next)
            holding += asset.net_contribution;
        result.rebalances.push_back({rebalance.rebalance_id, rebalance.scheduled_rebalance_date, rebalance.decision_date,
            rebalance.execution_date, next, rebalance.turnover, gross, costs, cash_after, holding,
            rebalance.deferred_asset_count, rebalance.skipped_asset_count, rebalance.partial_rebalance});
    }
    (void)summary_value;
    return result;
}

}  // namespace quant::analytics
