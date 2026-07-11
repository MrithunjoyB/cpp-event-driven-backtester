#include "PortfolioBacktester.h"

#include "ExecutionHandler.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
double mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double stdev(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }
    double avg = mean(values);
    double sum = 0.0;
    for (double value : values) {
        double diff = value - avg;
        sum += diff * diff;
    }
    return std::sqrt(sum / (values.size() - 1));
}

double covariance(const std::vector<double>& x, const std::vector<double>& y) {
    std::size_t n = std::min(x.size(), y.size());
    if (n < 2) {
        return 0.0;
    }
    const auto count = static_cast<std::vector<double>::difference_type>(n);
    double mx = mean(std::vector<double>(x.begin(), x.begin() + count));
    double my = mean(std::vector<double>(y.begin(), y.begin() + count));
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += (x[i] - mx) * (y[i] - my);
    }
    return sum / (n - 1);
}

double variance(const std::vector<double>& values) {
    double sd = stdev(values);
    return sd * sd;
}

std::string month_key(const std::string& date) {
    return date.size() >= 7 ? date.substr(0, 7) : date;
}

double annualized_return(double total_return, std::size_t observations) {
    double years = std::max(1.0 / 252.0, static_cast<double>(observations) / 252.0);
    return std::pow(std::max(0.0, 1.0 + total_return), 1.0 / years) - 1.0;
}

double close_return(const std::vector<Bar>& bars, std::size_t previous, std::size_t current) {
    if (previous >= bars.size() || current >= bars.size() || bars[previous].close <= 0.0) {
        return 0.0;
    }
    return (bars[current].close / bars[previous].close) - 1.0;
}
}

PortfolioBacktester::PortfolioBacktester(PortfolioBacktestConfig config) : config_(std::move(config)) {}

RebalanceFrequency PortfolioBacktester::parse_frequency(const std::string& value) {
    if (value == "weekly" || value == "Weekly") {
        return RebalanceFrequency::Weekly;
    }
    if (value == "monthly" || value == "Monthly") {
        return RebalanceFrequency::Monthly;
    }
    throw std::runtime_error("Unknown rebalance frequency: " + value);
}

std::string PortfolioBacktester::frequency_name(RebalanceFrequency frequency) {
    return frequency == RebalanceFrequency::Weekly ? "weekly" : "monthly";
}

std::vector<std::size_t> PortfolioBacktester::rebalance_indices(const std::vector<std::string>& dates, RebalanceFrequency frequency) {
    std::vector<std::size_t> indices;
    if (dates.empty()) {
        return indices;
    }
    indices.push_back(1 < dates.size() ? 1 : 0);
    if (frequency == RebalanceFrequency::Weekly) {
        for (std::size_t i = indices.front() + 5; i < dates.size(); i += 5) {
            indices.push_back(i);
        }
        return indices;
    }
    std::string current_month = month_key(dates[indices.front()]);
    for (std::size_t i = indices.front() + 1; i < dates.size(); ++i) {
        std::string m = month_key(dates[i]);
        if (m != current_month) {
            indices.push_back(i);
            current_month = m;
        }
    }
    return indices;
}

double PortfolioBacktester::value_at_risk_95(std::vector<double> returns) {
    if (returns.empty()) {
        return 0.0;
    }
    std::sort(returns.begin(), returns.end());
    std::size_t index = static_cast<std::size_t>(std::floor(0.05 * (returns.size() - 1)));
    return returns[index];
}

double PortfolioBacktester::expected_shortfall_95(std::vector<double> returns) {
    if (returns.empty()) {
        return 0.0;
    }
    std::sort(returns.begin(), returns.end());
    std::size_t index = static_cast<std::size_t>(std::floor(0.05 * (returns.size() - 1)));
    const auto tail_end = static_cast<std::vector<double>::difference_type>(index + 1);
    std::vector<double> tail(returns.begin(), returns.begin() + tail_end);
    return mean(tail);
}

void PortfolioBacktester::load_data(std::map<std::string, std::vector<Bar>>& history) const {
    for (const auto& ticker : config_.tickers) {
        MarketData data;
        std::string path = config_.data_dir + "/" + ticker + ".csv";
        if (!data.load_csv(ticker, path)) {
            throw std::runtime_error("Could not load portfolio data for " + ticker + " from " + path);
        }
        history[ticker] = data.bars(ticker);
    }
}

std::vector<std::string> PortfolioBacktester::common_dates(const std::map<std::string, std::vector<Bar>>& history) const {
    std::set<std::string> intersection;
    bool first = true;
    for (const auto& kv : history) {
        std::set<std::string> dates;
        for (const auto& bar : kv.second) {
            dates.insert(bar.date);
        }
        if (first) {
            intersection = dates;
            first = false;
        } else {
            std::set<std::string> next;
            std::set_intersection(intersection.begin(), intersection.end(), dates.begin(), dates.end(), std::inserter(next, next.begin()));
            intersection = std::move(next);
        }
    }
    return std::vector<std::string>(intersection.begin(), intersection.end());
}

std::map<std::string, std::size_t> PortfolioBacktester::indices_for_date(
    const std::map<std::string, std::vector<Bar>>& history,
    const std::string& date) const {
    std::map<std::string, std::size_t> out;
    for (const auto& kv : history) {
        for (std::size_t i = 0; i < kv.second.size(); ++i) {
            if (kv.second[i].date == date) {
                out[kv.first] = i;
                break;
            }
        }
    }
    return out;
}

PortfolioBacktestResult PortfolioBacktester::run() {
    if (config_.calendar.mode == quant::market_data::CalendarMode::Union) {
        return run_union();
    }
    std::map<std::string, std::vector<Bar>> history;
    load_data(history);
    std::vector<std::string> dates = common_dates(history);
    if (dates.size() < 2) {
        throw std::runtime_error("Portfolio backtest needs at least two common dates");
    }

    AllocationPolicy policy(config_.allocation);
    ExecutionHandler execution(config_.transaction_cost_rate, config_.slippage_rate);
    std::vector<std::size_t> rebalance_points = rebalance_indices(dates, config_.rebalance_frequency);
    std::set<std::size_t> rebalance_set(rebalance_points.begin(), rebalance_points.end());

    std::map<std::string, double> shares;
    std::map<std::string, double> target_weights;
    for (const auto& ticker : config_.tickers) {
        shares[ticker] = 0.0;
        target_weights[ticker] = 0.0;
    }

    double cash = config_.starting_capital;
    double peak = config_.starting_capital;
    double total_turnover = 0.0;
    double total_costs = 0.0;
    double cash_allocation_sum = 0.0;
    double gross_exposure_sum = 0.0;
    int rebalance_id = 0;

    std::vector<PortfolioEquityPoint> equity;
    std::vector<PortfolioPositionPoint> positions_out;
    std::vector<PortfolioFill> fills;
    std::vector<std::map<std::string, double>> weights_by_rebalance;
    std::vector<std::string> rebalance_dates;
    std::vector<double> turnover_by_rebalance;
    std::vector<double> benchmark_returns;

    for (std::size_t d = 0; d < dates.size(); ++d) {
        auto indices = indices_for_date(history, dates[d]);
        std::map<std::string, double> open_prices;
        std::map<std::string, double> close_prices;
        for (const auto& ticker : config_.tickers) {
            auto idx = indices.find(ticker);
            if (idx == indices.end()) {
                continue;
            }
            open_prices[ticker] = history[ticker][idx->second].open;
            close_prices[ticker] = history[ticker][idx->second].close;
        }

        if (rebalance_set.count(d) && d > 0) {
            std::map<std::string, std::size_t> decision_indices;
            for (const auto& ticker : config_.tickers) {
                auto idx = indices.find(ticker);
                if (idx != indices.end() && idx->second > 0) {
                    decision_indices[ticker] = idx->second - 1;
                }
            }
            target_weights = policy.target_weights(config_.tickers, history, decision_indices);

            double holdings_open = 0.0;
            for (const auto& ticker : config_.tickers) {
                holdings_open += shares[ticker] * open_prices[ticker];
            }
            double portfolio_value_open = cash + holdings_open;
            double rebalance_turnover = 0.0;

            for (const auto& ticker : config_.tickers) {
                double price = open_prices[ticker];
                if (price <= 0.0) {
                    continue;
                }
                double current_value = shares[ticker] * price;
                double target_value = portfolio_value_open * target_weights[ticker];
                double sell_value = current_value - target_value;
                if (sell_value <= config_.allocation.min_trade_value) {
                    continue;
                }
                int qty = static_cast<int>(std::floor(std::min(shares[ticker], sell_value / price)));
                if (qty <= 0) {
                    continue;
                }
                OrderEvent order{EventType::Order, dates[d], ticker, policy.name(), OrderSide::Sell, qty};
                FillEvent fill = execution.execute_order(order, price);
                if (fill.quantity <= 0 || fill.quantity > shares[ticker] + 1e-9) {
                    continue;
                }
                shares[ticker] -= fill.quantity;
                cash += fill.gross_value - fill.transaction_cost;
                total_costs += fill.transaction_cost + fill.slippage_cost;
                total_turnover += fill.gross_value;
                rebalance_turnover += fill.gross_value;
                fills.push_back({rebalance_id, dates[d], ticker, "SELL", fill.quantity, fill.fill_price, fill.transaction_cost, fill.slippage_cost, cash, dates[d], dates[d - 1], dates[d]});
            }

            std::vector<std::pair<std::string, double>> desired_buys;
            double required_cash = 0.0;
            for (const auto& ticker : config_.tickers) {
                double price = open_prices[ticker];
                if (price <= 0.0) {
                    continue;
                }
                double current_value = shares[ticker] * price;
                double target_value = portfolio_value_open * target_weights[ticker];
                double buy_value = target_value - current_value;
                if (buy_value <= config_.allocation.min_trade_value) {
                    continue;
                }
                int qty = static_cast<int>(std::floor(buy_value / (price * (1.0 + config_.slippage_rate) * (1.0 + config_.transaction_cost_rate))));
                if (qty <= 0) {
                    continue;
                }
                double fill_price = price * (1.0 + config_.slippage_rate);
                double need = qty * fill_price * (1.0 + config_.transaction_cost_rate);
                desired_buys.push_back({ticker, static_cast<double>(qty)});
                required_cash += need;
            }
            double scale = required_cash > cash && required_cash > 0.0 ? std::max(0.0, cash / required_cash) : 1.0;
            for (const auto& buy : desired_buys) {
                const std::string& ticker = buy.first;
                double price = open_prices[ticker];
                int qty = static_cast<int>(std::floor(buy.second * scale));
                if (qty <= 0) {
                    continue;
                }
                OrderEvent order{EventType::Order, dates[d], ticker, policy.name(), OrderSide::Buy, qty};
                FillEvent fill = execution.execute_order(order, price);
                double need = fill.gross_value + fill.transaction_cost;
                if (need > cash + 1e-8) {
                    qty = static_cast<int>(std::floor(cash / (fill.fill_price * (1.0 + config_.transaction_cost_rate))));
                    if (qty <= 0) {
                        continue;
                    }
                    order.quantity = qty;
                    fill = execution.execute_order(order, price);
                    need = fill.gross_value + fill.transaction_cost;
                }
                if (need > cash + 1e-8) {
                    continue;
                }
                shares[ticker] += fill.quantity;
                cash -= need;
                total_costs += fill.transaction_cost + fill.slippage_cost;
                total_turnover += fill.gross_value;
                rebalance_turnover += fill.gross_value;
                fills.push_back({rebalance_id, dates[d], ticker, "BUY", fill.quantity, fill.fill_price, fill.transaction_cost, fill.slippage_cost, cash, dates[d], dates[d - 1], dates[d]});
            }
            weights_by_rebalance.push_back(target_weights);
            rebalance_dates.push_back(dates[d]);
            turnover_by_rebalance.push_back(portfolio_value_open > 0.0 ? rebalance_turnover / portfolio_value_open : 0.0);
            ++rebalance_id;
        }

        double holdings_close = 0.0;
        for (const auto& ticker : config_.tickers) {
            holdings_close += shares[ticker] * close_prices[ticker];
        }
        double value = cash + holdings_close;
        peak = std::max(peak, value);
        double drawdown = peak > 0.0 ? value / peak - 1.0 : 0.0;
        double gross_exposure = value > 0.0 ? holdings_close / value : 0.0;
        equity.push_back({dates[d], value, cash, holdings_close, value / config_.starting_capital - 1.0, drawdown, gross_exposure});
        cash_allocation_sum += value > 0.0 ? cash / value : 0.0;
        gross_exposure_sum += gross_exposure;

        for (const auto& ticker : config_.tickers) {
            double market_value = shares[ticker] * close_prices[ticker];
            double actual_weight = value > 0.0 ? market_value / value : 0.0;
            positions_out.push_back({dates[d], ticker, shares[ticker], close_prices[ticker], market_value, target_weights[ticker], actual_weight, rebalance_id - 1});
        }

        if (d > 0) {
            double ew = 0.0;
            for (const auto& ticker : config_.tickers) {
                ew += close_return(history[ticker], indices_for_date(history, dates[d - 1])[ticker], indices[ticker]) / config_.tickers.size();
            }
            benchmark_returns.push_back(ew);
        }
    }

    PortfolioSummary summary = summarize(policy, equity, history, dates, total_turnover, total_costs, rebalance_id, static_cast<int>(fills.size()),
                                         cash_allocation_sum, gross_exposure_sum, benchmark_returns);
    PortfolioBacktestResult result;
    result.summary = summary;
    result.equity_curve = std::move(equity);
    result.positions = std::move(positions_out);
    result.fills = std::move(fills);
    result.target_weights = std::move(weights_by_rebalance);
    result.rebalance_dates = std::move(rebalance_dates);
    result.turnover_by_rebalance = std::move(turnover_by_rebalance);
    return result;
}

PortfolioSummary PortfolioBacktester::summarize(
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
    const std::vector<double>& benchmark_returns) const {
    PortfolioSummary summary;
    summary.policy_name = policy.name();
    summary.number_of_rebalances = rebalances;
    summary.number_of_fills = fills;
    summary.total_transaction_costs = total_costs;
    summary.turnover = config_.starting_capital > 0.0 ? total_turnover / config_.starting_capital : 0.0;

    std::map<std::string, std::size_t> first = indices_for_date(history, dates.front());
    std::map<std::string, std::size_t> last = indices_for_date(history, dates.back());
    double ew_end = 0.0;
    for (const auto& ticker : config_.tickers) {
        double start = history.at(ticker)[first[ticker]].close;
        double end = history.at(ticker)[last[ticker]].close;
        ew_end += start > 0.0 ? (end / start) / config_.tickers.size() : 0.0;
    }
    summary.equal_weight_benchmark_return = ew_end - 1.0;
    if (history.count(config_.benchmark_ticker)) {
        double start = history.at(config_.benchmark_ticker)[first[config_.benchmark_ticker]].close;
        double end = history.at(config_.benchmark_ticker)[last[config_.benchmark_ticker]].close;
        summary.spy_benchmark_return = start > 0.0 ? (end / start) - 1.0 : 0.0;
    }
    summary.average_cash_allocation = dates.empty() ? 0.0 : cash_allocation_sum / dates.size();
    summary.average_gross_exposure = dates.empty() ? 0.0 : gross_exposure_sum / dates.size();
    std::vector<double> returns;
    for (std::size_t i = 1; i < equity.size(); ++i) {
        if (equity[i - 1].portfolio_value > 0.0) {
            returns.push_back((equity[i].portfolio_value / equity[i - 1].portfolio_value) - 1.0);
        }
    }
    if (!equity.empty()) {
        summary.total_return = equity.back().portfolio_value / config_.starting_capital - 1.0;
        summary.excess_return = summary.total_return - summary.equal_weight_benchmark_return;
        summary.annualized_return = annualized_return(summary.total_return, equity.size());
        summary.volatility = stdev(returns) * std::sqrt(252.0);
        summary.sharpe = summary.volatility > 0.0 ? mean(returns) * 252.0 / summary.volatility : 0.0;
        std::vector<double> downside;
        for (double r : returns) {
            if (r < 0.0) {
                downside.push_back(r);
            }
        }
        double down_dev = stdev(downside) * std::sqrt(252.0);
        summary.sortino = down_dev > 0.0 ? mean(returns) * 252.0 / down_dev : 0.0;
        for (const auto& p : equity) {
            summary.max_drawdown = std::min(summary.max_drawdown, p.drawdown);
        }
        summary.calmar = summary.max_drawdown < 0.0 ? summary.annualized_return / std::abs(summary.max_drawdown) : 0.0;
        summary.var_95 = value_at_risk_95(returns);
        summary.expected_shortfall_95 = expected_shortfall_95(returns);
        if (returns.size() == benchmark_returns.size()) {
            double var_b = variance(benchmark_returns);
            summary.beta = var_b > 0.0 ? covariance(returns, benchmark_returns) / var_b : 0.0;
            summary.alpha = (mean(returns) - summary.beta * mean(benchmark_returns)) * 252.0;
            std::vector<double> active;
            for (std::size_t i = 0; i < returns.size(); ++i) {
                active.push_back(returns[i] - benchmark_returns[i]);
            }
            double active_vol = stdev(active) * std::sqrt(252.0);
            summary.information_ratio = active_vol > 0.0 ? mean(active) * 252.0 / active_vol : 0.0;
        }
    }
    return summary;
}
