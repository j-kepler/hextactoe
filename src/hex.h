// Axial hex coordinates for an unbounded, pointy-top hexagonal grid.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

struct Hex {
    int q = 0;
    int r = 0;

    constexpr Hex() = default;
    constexpr Hex(int q_, int r_) : q(q_), r(r_) {}

    constexpr Hex operator+(Hex o) const { return {q + o.q, r + o.r}; }
    constexpr Hex operator-(Hex o) const { return {q - o.q, r - o.r}; }
    constexpr Hex operator*(int k) const { return {q * k, r * k}; }
    constexpr bool operator==(Hex o) const { return q == o.q && r == o.r; }
    constexpr bool operator!=(Hex o) const { return !(*this == o); }
};

// The three line directions of a hex grid. Each axis together with its
// opposite direction forms one of the three kinds of straight line.
inline constexpr Hex kAxes[3] = {{1, 0}, {0, 1}, {1, -1}};

inline int hexDistance(Hex a, Hex b) {
    const int dq = a.q - b.q;
    const int dr = a.r - b.r;
    return (std::abs(dq) + std::abs(dr) + std::abs(dq + dr)) / 2;
}

struct HexHash {
    std::size_t operator()(Hex h) const noexcept {
        std::uint64_t k = (std::uint64_t(std::uint32_t(h.q)) << 32) | std::uint32_t(h.r);
        // splitmix64 finaliser: spreads neighbouring coordinates across buckets.
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return std::size_t(k);
    }
};

struct Vec2 {
    double x = 0;
    double y = 0;
};

inline constexpr double kSqrt3 = 1.7320508075688772;

// Centre of a hex in world units, where 1 unit is the hex's corner radius.
inline Vec2 hexToWorld(Hex h) {
    return {kSqrt3 * (h.q + h.r * 0.5), 1.5 * h.r};
}

// The hex containing a world-space point.
inline Hex worldToHex(Vec2 p) {
    const double q = kSqrt3 / 3.0 * p.x - p.y / 3.0;
    const double r = 2.0 / 3.0 * p.y;
    const double s = -q - r;
    double rq = std::round(q);
    double rr = std::round(r);
    const double rs = std::round(s);
    const double dq = std::fabs(rq - q);
    const double dr = std::fabs(rr - r);
    const double ds = std::fabs(rs - s);
    if (dq > dr && dq > ds) {
        rq = -rr - rs;
    } else if (dr > ds) {
        rr = -rq - rs;
    }
    return {int(rq), int(rr)};
}
