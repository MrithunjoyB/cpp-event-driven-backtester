#pragma once

#include "quant/domain/Errors.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace quant::performance {

class DeterministicExecutor {
public:
    DeterministicExecutor(std::string mode, int threads)
        : mode_(std::move(mode)), threads_(effective_threads(mode_, threads)) {}

    int threads() const { return threads_; }
    const std::string& mode() const { return mode_; }

    static int effective_threads(const std::string& mode, int requested) {
        if (mode != "serial" && mode != "parallel") throw ConfigurationError("execution_mode must be serial or parallel");
        if (requested <= 0 || requested > 64) throw ConfigurationError("threads must be in [1, 64]");
        return mode == "serial" ? 1 : requested;
    }

    template <typename Function>
    auto map(std::size_t count, Function function) const -> std::vector<decltype(function(std::size_t{}))> {
        using Result = decltype(function(std::size_t{}));
        std::vector<Result> results(count);
        if (count == 0) return results;
        if (threads_ == 1 || count == 1) {
            for (std::size_t index = 0; index < count; ++index) results[index] = function(index);
            return results;
        }
        std::atomic<std::size_t> next{0};
        std::atomic<bool> failed{false};
        std::exception_ptr error;
        std::mutex error_mutex;
        const int workers = std::min(threads_, static_cast<int>(count));
        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(workers));
        for (int worker = 0; worker < workers; ++worker) {
            pool.emplace_back([&]() {
                while (!failed.load(std::memory_order_acquire)) {
                    const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= count) break;
                    try { results[index] = function(index); }
                    catch (...) {
                        failed.store(true, std::memory_order_release);
                        std::lock_guard<std::mutex> lock(error_mutex);
                        if (!error) error = std::current_exception();
                    }
                }
            });
        }
        for (auto& worker : pool) worker.join();
        if (error) std::rethrow_exception(error);
        return results;
    }

private:
    std::string mode_;
    int threads_{1};
};

}  // namespace quant::performance
