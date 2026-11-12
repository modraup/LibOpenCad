// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/types/vector.rs.
#pragma once

#include <iosfwd>

namespace libopencad {

struct Vector2 {
    double x = 0, y = 0;
    static const Vector2 ZERO, UNIT_X, UNIT_Y;

    double length() const noexcept;
    double length_squared() const noexcept;
    Vector2 normalize() const noexcept;
    double dot(Vector2 other) const noexcept;
    double cross(Vector2 other) const noexcept; // Signed scalar in 2D.
    double distance(Vector2 other) const noexcept;

    constexpr Vector2 operator+(Vector2 b) const noexcept { return {x + b.x, y + b.y}; }
    constexpr Vector2 operator-(Vector2 b) const noexcept { return {x - b.x, y - b.y}; }
    constexpr Vector2 operator*(double s) const noexcept { return {x * s, y * s}; }
    constexpr Vector2 operator/(double s) const noexcept { return {x / s, y / s}; }
    constexpr Vector2 operator-() const noexcept { return {-x, -y}; }
    constexpr bool operator==(Vector2 b) const noexcept { return x == b.x && y == b.y; }
    constexpr bool operator!=(Vector2 b) const noexcept { return !(*this == b); }
};

// Retain the existing DXF coordinate type and its linkage in public entity methods.
struct Point3;
using Vector3 = Point3;

struct Point3 {
    double x = 0, y = 0, z = 0;
    static const Vector3 ZERO, UNIT_X, UNIT_Y, UNIT_Z;

    double length() const noexcept;
    double length_squared() const noexcept;
    Vector3 normalize() const noexcept;
    double dot(Vector3 other) const noexcept;
    Vector3 cross(Vector3 other) const noexcept;
    double distance(Vector3 other) const noexcept;

    constexpr Vector3 operator+(Vector3 b) const noexcept { return {x + b.x, y + b.y, z + b.z}; }
    constexpr Vector3 operator-(Vector3 b) const noexcept { return {x - b.x, y - b.y, z - b.z}; }
    constexpr Vector3 operator*(double s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vector3 operator/(double s) const noexcept { return {x / s, y / s, z / s}; }
    constexpr Vector3 operator-() const noexcept { return {-x, -y, -z}; }
    constexpr bool operator==(Vector3 b) const noexcept { return x == b.x && y == b.y && z == b.z; }
    constexpr bool operator!=(Vector3 b) const noexcept { return !(*this == b); }
};

inline constexpr Vector2 Vector2::ZERO{}, Vector2::UNIT_X{1, 0}, Vector2::UNIT_Y{0, 1};
inline constexpr Vector3 Vector3::ZERO{}, Vector3::UNIT_X{1, 0, 0}, Vector3::UNIT_Y{0, 1, 0},
                         Vector3::UNIT_Z{0, 0, 1};

std::ostream& operator<<(std::ostream& out, Vector2 v);
std::ostream& operator<<(std::ostream& out, Vector3 v);

} // namespace libopencad
