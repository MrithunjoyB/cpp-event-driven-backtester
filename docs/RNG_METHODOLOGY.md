# Platform-Stable Stochastic Sampling

## Methodology Identity

- Engine: `std::mt19937`, whose 32-bit output sequence is specified by C++.
- Mapping: `portable_bounded_v1`.
- Stochastic methodology version: `2`.
- Supported bound: every integer in `[1, 2^32 - 1]`; zero is rejected.

Legacy stochastic outputs produced through `std::uniform_int_distribution` are methodology-v1 historical evidence and are not release-canonical. There is no legacy replay mode and migrated and legacy packages may not be assembled as one methodology.

## Mapping

For unsigned 32-bit `bound`, define `threshold = (2^32 - bound) mod bound`. Unsigned negation implements the first term without signed overflow.

```text
portable_bounded_v1(engine, bound):
    require 1 <= bound <= 2^32 - 1
    threshold = uint32(-bound) % bound
    repeat:
        word = uint32(engine())
        product = uint64(word) * uint64(bound)
        low = uint32(product)
    until low >= threshold
    return uint32(product >> 32)
```

Each attempt consumes exactly one engine word. Rejected attempts consume their words permanently. Bound one always returns zero and consumes one word.

## Uniformity

The 64-bit product partitions the `2^32` source words among `bound` high-word outputs. When `bound` does not divide `2^32`, the low interval contains an incomplete residue class. Rejecting the first `2^32 mod bound` low words removes that interval, leaving the same number of accepted source words for every output. Plain `engine() % bound` retains the incomplete residue class and is biased.

All arithmetic is explicit `uint32_t` or `uint64_t`; unsigned wraparound and right shift are defined. The implementation does not use floating point, implementation-selected distributions, global state, standard hashes, thread identity, or scheduling order.

## Evidence

`tests/fixtures/rng/portable_bounded_v1.csv` contains 8,704 outputs across four seeds and 17 bounds, including powers of two, non-powers of two, representative sample lengths, `2^31 + 1`, and `2^32 - 1`. It records per-sample and cumulative engine-word consumption and forces rejection paths. C++ consumes the fixture with `std::mt19937`; Python independently implements MT19937 and the mapping. Linux and macOS CI execute both references.

Golden vectors, retained bootstrap paths, bootstrap distributions, max-statistic distributions, and adjusted p-values are canonical semantic artifacts with zero stochastic portability tolerance.
