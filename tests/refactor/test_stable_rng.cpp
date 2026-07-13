#include "quant/domain/Errors.h"
#include "quant/random/StableRng.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <string>

int main() {
    int cases = 0;
    const auto check = [&cases](const bool condition, const char* name) {
        ++cases;
        if (!condition) throw std::runtime_error(name);
    };

    try {
        std::mt19937 zero_engine(1U);
        bool rejected = false;
        try {
            (void)quant::random::StableRng::bounded_index(zero_engine, 0U);
        } catch (const quant::ConfigurationError&) {
            rejected = true;
        }
        check(rejected, "zero bound rejection");

        std::mt19937 one_engine(7U);
        for (int i = 0; i < 128; ++i) check(quant::random::StableRng::bounded_index(one_engine, 1U) == 0U, "bound one");

        const std::array<std::uint32_t, 7> bounds{2U, 3U, 32U, 33U, 257U, 2190U,
                                                  std::numeric_limits<std::uint32_t>::max()};
        for (const auto bound : bounds) {
            std::mt19937 engine(42U);
            for (int i = 0; i < 1000; ++i) check(quant::random::StableRng::bounded_index(engine, bound) < bound, "range");
        }

        std::mt19937 first(123456U);
        std::mt19937 second(123456U);
        std::mt19937 different(654321U);
        std::vector<std::uint32_t> a;
        std::vector<std::uint32_t> b;
        std::vector<std::uint32_t> c;
        for (int i = 0; i < 2048; ++i) {
            a.push_back(quant::random::StableRng::bounded_index(first, 2190U));
            b.push_back(quant::random::StableRng::bounded_index(second, 2190U));
            c.push_back(quant::random::StableRng::bounded_index(different, 2190U));
        }
        check(a == b, "repeated seed equality");
        check(a != c, "different seed inequality");
        check(quant::random::kRngEngine == "mt19937", "engine identity");
        check(quant::random::kRngMapping == "portable_bounded_v1", "mapping identity");
        check(quant::random::kStochasticMethodologyVersion == 2, "methodology version");

        std::ifstream fixture("tests/fixtures/rng/portable_bounded_v1.csv");
        check(fixture.good(), "golden fixture available");
        std::string line;
        std::getline(fixture, line);
        std::uint32_t active_seed = 0;
        std::uint32_t active_bound = 0;
        std::uint64_t cumulative_words = 0;
        std::mt19937 fixture_engine;
        std::size_t fixture_rows = 0;
        bool rejection_seen = false;
        while (std::getline(fixture, line)) {
            std::stringstream stream(line);
            std::array<std::string, 7> fields{};
            for (std::size_t index = 0; index < fields.size(); ++index) std::getline(stream, fields[index], ',');
            const auto seed = static_cast<std::uint32_t>(std::stoul(fields[1]));
            const auto bound = static_cast<std::uint32_t>(std::stoul(fields[2]));
            if (fixture_rows == 0 || seed != active_seed || bound != active_bound) {
                fixture_engine.seed(seed);
                active_seed = seed;
                active_bound = bound;
                cumulative_words = 0;
            }
            const auto sample = quant::random::StableRng::bounded(fixture_engine, bound);
            cumulative_words += sample.engine_words_consumed;
            check(sample.value == static_cast<std::uint32_t>(std::stoul(fields[4])), "golden value");
            check(sample.engine_words_consumed == std::stoull(fields[5]), "golden draw count");
            check(cumulative_words == std::stoull(fields[6]), "golden engine state position");
            rejection_seen = rejection_seen || sample.engine_words_consumed > 1;
            ++fixture_rows;
        }
        check(fixture_rows == 8704, "golden vector count");
        check(rejection_seen, "rejection path fixture");

        std::cout << cases << " stable RNG cases passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
