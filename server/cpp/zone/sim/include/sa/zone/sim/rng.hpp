#pragma once

#include <concepts>
#include <cstdint>
#include <random>

namespace sa::zone::sim {

// Deterministic random source owned by one zone. Same seed, same sequence (ADR-0008).
class Rng {
public:
    explicit Rng(std::uint64_t seed) : engine_(seed) {}

    [[nodiscard]] std::uint64_t NextU64() { return engine_(); }

    // Uniform in [0, 1).
    [[nodiscard]] float NextUnit() {
        return std::uniform_real_distribution<float>{0.0F, 1.0F}(engine_);
    }

    // Uniform integer in [lo, hi] inclusive. Precondition: lo <= hi.
    template <std::integral T>
    [[nodiscard]] T Range(T lo, T hi) {
        return std::uniform_int_distribution<T>{lo, hi}(engine_);
    }

private:
    std::mt19937_64 engine_;
};

}  // namespace sa::zone::sim
