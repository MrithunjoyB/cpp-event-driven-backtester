# Stable RNG Migration

The migration uses `std::mt19937` with repository-owned Lemire multiply-high rejection sampling. Legacy stochastic values remain historical evidence; migrated values are canonical. See `baseline_comparison.csv` for every decision-sensitive change.
