# Configuration

Run `quant_cli validate-config --config <file>` before an experiment and `print-resolved-config` to inspect explicit defaults. Parsing is strict: unknown/duplicate fields, wrong types, malformed JSON, non-finite numbers, negative capital or costs, invalid windows, unknown strategies/policies, invalid benchmark definitions, and invalid thresholds fail before simulation.

Major sections in `ExperimentConfig` are:

- `execution`: starting capital, commission basis points, and slippage basis points.
- `walk_forward`: observation or calendar windows, continuity policy, and boundary liquidation policy.
- `benchmark`: `same_asset` or an available benchmark ticker.
- `regime`: the causal trend/return/volatility classification method.
- `bootstrap`: deterministic random seed.
- `portfolio`: allocation policy, rebalance frequency, constraints, and lookbacks.
- `output`: research and portfolio result directories.
- `parameter_selection`: documented objective and minimum trade count.

Existing examples are in `configs/`. The canonical commands are:

```bash
./build/quant_cli validate-config --config configs/ma_walk_forward.json
./build/quant_cli print-resolved-config --config configs/ma_walk_forward.json
./build/quant_cli run --config configs/ma_walk_forward.json --dry-run
./build/quant_cli run --config configs/ma_walk_forward.json
```

Legacy `--mode` invocation remains available for reproducible single-purpose runs, but new automation should use typed config files.
