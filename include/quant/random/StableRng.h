#pragma once

#include <cstdint>
#include <random>
#include <string_view>

namespace quant::random {

inline constexpr std::string_view kRngEngine = "mt19937";
inline constexpr std::string_view kRngMapping = "portable_bounded_v1";
inline constexpr int kStochasticMethodologyVersion = 2;

struct BoundedSample {
    std::uint32_t value{0};
    std::uint64_t engine_words_consumed{0};
};

class StableRng {
public:
    static BoundedSample bounded(std::mt19937& engine, std::uint32_t bound);
    static std::uint32_t bounded_index(std::mt19937& engine, std::uint32_t bound);
};

}  // namespace quant::random
