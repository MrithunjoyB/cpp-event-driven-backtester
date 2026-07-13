#include "quant/random/StableRng.h"

#include "quant/domain/Errors.h"

#include <cstdint>

namespace quant::random {

BoundedSample StableRng::bounded(std::mt19937& engine, const std::uint32_t bound) {
    if (bound == 0U) throw ConfigurationError("Stable bounded RNG requires a positive bound");

    // Unsigned wraparound defines 2^32 - bound; the remainder is 2^32 mod bound.
    const std::uint32_t rejection_threshold = static_cast<std::uint32_t>(-bound) % bound;
    std::uint64_t consumed = 0;
    for (;;) {
        const std::uint32_t word = engine();
        ++consumed;
        const std::uint64_t product = static_cast<std::uint64_t>(word) * static_cast<std::uint64_t>(bound);
        const std::uint32_t low = static_cast<std::uint32_t>(product);
        if (low >= rejection_threshold) {
            return {static_cast<std::uint32_t>(product >> 32U), consumed};
        }
    }
}

std::uint32_t StableRng::bounded_index(std::mt19937& engine, const std::uint32_t bound) {
    return bounded(engine, bound).value;
}

}  // namespace quant::random
