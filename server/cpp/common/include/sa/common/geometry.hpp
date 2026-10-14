#pragma once

#include <cmath>

// 2D geometry shared by the simulation, the bots and the viewer. Units are metres; the Y axis
// points "up" on the map. Everything is constexpr-friendly and allocation-free.
namespace sa::common {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;

    constexpr Vec2() noexcept = default;
    constexpr Vec2(float x_value, float y_value) noexcept : x(x_value), y(y_value) {}

    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Vec2 operator*(Vec2 v, float s) noexcept { return {v.x * s, v.y * s}; }
    friend constexpr bool operator==(Vec2, Vec2) noexcept = default;

    constexpr Vec2& operator+=(Vec2 o) noexcept {
        x += o.x;
        y += o.y;
        return *this;
    }

    [[nodiscard]] constexpr float LengthSq() const noexcept { return (x * x) + (y * y); }
    [[nodiscard]] float Length() const noexcept { return std::sqrt(LengthSq()); }
    [[nodiscard]] constexpr bool IsZero() const noexcept { return x == 0.0F && y == 0.0F; }

    // Unit vector in the same direction; the zero vector stays zero (no NaN).
    [[nodiscard]] Vec2 Normalized() const noexcept {
        const float len = Length();
        return len > 0.0F ? Vec2{x / len, y / len} : Vec2{};
    }
};

[[nodiscard]] constexpr float DistanceSq(Vec2 a, Vec2 b) noexcept {
    return (a - b).LengthSq();
}
[[nodiscard]] inline float Distance(Vec2 a, Vec2 b) noexcept {
    return (a - b).Length();
}

// Axis-aligned box, min inclusive / max exclusive.
struct Aabb {
    Vec2 min;
    Vec2 max;

    [[nodiscard]] constexpr bool Contains(Vec2 p) const noexcept {
        return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return min.x < max.x && min.y < max.y; }
};

}  // namespace sa::common
