#include "PortfolioBacktester.h"

#include "ExecutionHandler.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
    double mx = mean(std::vector<double>(x.begin(), x.begin() + n));
    double my = mean(std::vector<double>(y.begin(), y.begin() + n));
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
    std::vector<double> tail(returns.begin(), returns.begin() + index + 1);
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

PortfolioBacktestResult PortfolioBacktester::run(bool write) {
    std::filesystem::create_directories(config_.results_dir);
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
                fills.push_back({rebalance_id, dates[d], ticker, "SELL", fill.quantity, fill.fill_price, fill.transaction_cost, fill.slippage_cost, cash});
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
                fills.push_back({rebalance_id, dates[d], ticker, "BUY", fill.quantity, fill.fill_price, fill.transaction_cost, fill.slippage_cost, cash});
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
    if (write) {
        write_outputs(policy, equity, positions_out, fills, weights_by_rebalance, rebalance_dates, turnover_by_rebalance, benchmark_returns, summary);
    }
    return {summary, equity, positions_out, fills};
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

void PortfolioBacktester::write_outputs(
    const AllocationPolicy& policy,
    const std::vector<PortfolioEquityPoint>& equity,
    const std::vector<PortfolioPositionPoint>& positions,
    const std::vector<PortfolioFill>& fills,
    const std::vector<std::map<std::string, double>>& weights_by_rebalance,
    const std::vector<std::string>& rebalance_dates,
    const std::vector<double>& turnover_by_rebalance,
    const std::vector<double>& benchmark_returns,
    const PortfolioSummary& base_summary) const {
    std::filesystem::create_directories(config_.results_dir);
    std::ofstream eq(config_.results_dir + "/portfolio_equity_curve.csv");
    std::ofstream pos(config_.results_dir + "/portfolio_positions.csv");
    std::ofstream orders(config_.results_dir + "/portfolio_orders.csv");
    std::ofstream fill_out(config_.results_dir + "/portfolio_fills.csv");
    std::ofstream reb(config_.results_dir + "/portfolio_rebalances.csv");
    std::ofstream weights(config_.results_dir + "/portfolio_allocation_weights.csv");
    std::ofstream costs(config_.results_dir + "/portfolio_costs.csv");
    std::ofstream summary_out(config_.results_dir + "/portfolio_performance_summary.csv");
    eq << std::fixed << std::setprecision(6);
    pos << std::fixed << std::setprecision(6);
    orders << std::fixed << std::setprecision(6);
    fill_out << std::fixed << std::setprecision(6);
    reb << std::fixed << std::setprecision(6);
    weights << std::fixed << std::setprecision(6);
    costs << std::fixed << std::setprecision(6);
    summary_out << std::fixed << std::setprecision(6);

    std::vector<double> returns;
    for (std::size_t i = 1; i < equity.size(); ++i) {
        if (equity[i - 1].portfolio_value > 0.0) {
            returns.push_back((equity[i].portfolio_value / equity[i - 1].portfolio_value) - 1.0);
        }
    }

    PortfolioSummary s = base_summary;
    if (!equity.empty()) {
        s.total_return = equity.back().portfolio_value / config_.starting_capital - 1.0;
        s.excess_return = s.total_return - s.equal_weight_benchmark_return;
        s.annualized_return = annualized_return(s.total_return, equity.size());
        s.volatility = stdev(returns) * std::sqrt(252.0);
        s.sharpe = s.volatility > 0.0 ? mean(returns) * 252.0 / s.volatility : 0.0;
        std::vector<double> downside;
        for (double r : returns) {
            if (r < 0.0) {
                downside.push_back(r);
            }
        }
        double down_dev = stdev(downside) * std::sqrt(252.0);
        s.sortino = down_dev > 0.0 ? mean(returns) * 252.0 / down_dev : 0.0;
        for (const auto& p : equity) {
            s.max_drawdown = std::min(s.max_drawdown, p.drawdown);
        }
        s.calmar = s.max_drawdown < 0.0 ? s.annualized_return / std::abs(s.max_drawdown) : 0.0;
        s.var_95 = value_at_risk_95(returns);
        s.expected_shortfall_95 = expected_shortfall_95(returns);
    }

    if (returns.size() == benchmark_returns.size()) {
        double var_b = variance(benchmark_returns);
        s.beta = var_b > 0.0 ? covariance(returns, benchmark_returns) / var_b : 0.0;
        s.alpha = (mean(returns) - s.beta * mean(benchmark_returns)) * 252.0;
        std::vector<double> active;
        for (std::size_t i = 0; i < returns.size(); ++i) {
            active.push_back(returns[i] - benchmark_returns[i]);
        }
        double active_vol = stdev(active) * std::sqrt(252.0);
        s.information_ratio = active_vol > 0.0 ? mean(active) * 252.0 / active_vol : 0.0;
    }

    eq << "date,portfolio_value,cash,total_holdings_value,total_return,drawdown,gross_exposure\n";
    for (const auto& p : equity) {
        eq << p.date << ',' << p.portfolio_value << ',' << p.cash << ',' << p.total_holdings_value << ','
           << p.total_return << ',' << p.drawdown << ',' << p.gross_exposure << '\n';
    }
    pos << "date,ticker,quantity,price,market_value,target_weight,actual_weight,rebalance_id\n";
    for (const auto& p : positions) {
        pos << p.date << ',' << p.ticker << ',' << p.quantity << ',' << p.price << ',' << p.market_value << ','
            << p.target_weight << ',' << p.actual_weight << ',' << p.rebalance_id << '\n';
    }
    fill_out << "rebalance_id,date,ticker,side,quantity,price,transaction_cost,slippage_cost,cash_after\n";
    orders << "rebalance_id,date,ticker,side,quantity,price,transaction_cost,slippage_cost\n";
    costs << "rebalance_id,date,ticker,transaction_cost,slippage_cost,total_cost\n";
    for (const auto& f : fills) {
        fill_out << f.rebalance_id << ',' << f.date << ',' << f.ticker << ',' << f.side << ',' << f.quantity << ','
                 << f.price << ',' << f.transaction_cost << ',' << f.slippage_cost << ',' << f.cash_after << '\n';
        orders << f.rebalance_id << ',' << f.date << ',' << f.ticker << ',' << f.side << ',' << f.quantity << ','
               << f.price << ',' << f.transaction_cost << ',' << f.slippage_cost << '\n';
        costs << f.rebalance_id << ',' << f.date << ',' << f.ticker << ',' << f.transaction_cost << ','
              << f.slippage_cost << ',' << f.transaction_cost + f.slippage_cost << '\n';
    }
    reb << "rebalance_id,date,policy_name,frequency,turnover,cash_buffer,max_weight,min_trade_value\n";
    weights << "rebalance_id,policy_name,ticker,target_weight\n";
    for (std::size_t i = 0; i < weights_by_rebalance.size(); ++i) {
        std::string date = i < rebalance_dates.size() ? rebalance_dates[i] : "";
        reb << i << ',' << date << ',' << policy.name() << ',' << frequency_name(config_.rebalance_frequency) << ','
            << (i < turnover_by_rebalance.size() ? turnover_by_rebalance[i] : 0.0) << ','
            << config_.allocation.cash_buffer << ',' << config_.allocation.max_weight << ',' << config_.allocation.min_trade_value << '\n';
        for (const auto& kv : weights_by_rebalance[i]) {
            weights << i << ',' << policy.name() << ',' << kv.first << ',' << kv.second << '\n';
        }
    }
    summary_out << "policy_name,total_return,equal_weight_benchmark_return,spy_benchmark_return,excess_return,annualized_return,volatility,sharpe,sortino,max_drawdown,calmar,var_95,expected_shortfall_95,beta,alpha,information_ratio,turnover,total_transaction_costs,number_of_rebalances,number_of_fills,average_cash_allocation,average_gross_exposure\n";
    summary_out << s.policy_name << ',' << s.total_return << ',' << s.equal_weight_benchmark_return << ',' << s.spy_benchmark_return << ','
                << s.excess_return << ',' << s.annualized_return << ',' << s.volatility << ',' << s.sharpe << ','
                << s.sortino << ',' << s.max_drawdown << ',' << s.calmar << ',' << s.var_95 << ','
                << s.expected_shortfall_95 << ',' << s.beta << ',' << s.alpha << ',' << s.information_ratio << ','
                << s.turnover << ',' << s.total_transaction_costs << ',' << s.number_of_rebalances << ','
                << s.number_of_fills << ',' << s.average_cash_allocation << ',' << s.average_gross_exposure << '\n';
}
