// SPDX-License-Identifier: MPL-2.0
#include <libopencad/vector.hpp>

#include <cmath>
#include <ostream>

namespace libopencad {

double Vector2::dot(Vector2 b) const noexcept { return x * b.x + y * b.y; }
double Vector2::cross(Vector2 b) const noexcept { return x * b.y - y * b.x; }
double Vector2::length_squared() const noexcept { return dot(*this); }
double Vector2::length() const noexcept { return std::sqrt(length_squared()); }
double Vector2::distance(Vector2 b) const noexcept { return (*this - b).length(); }

Vector2 Vector2::normalize() const noexcept {
    const double len = length();
    return len > 0 ? *this / len : *this;
}

double Vector3::dot(Vector3 b) const noexcept { return x * b.x + y * b.y + z * b.z; }
Vector3 Vector3::cross(Vector3 b) const noexcept {
    return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x};
}
double Vector3::length_squared() const noexcept { return dot(*this); }
double Vector3::length() const noexcept { return std::sqrt(length_squared()); }
double Vector3::distance(Vector3 b) const noexcept { return (*this - b).length(); }

Vector3 Vector3::normalize() const noexcept {
    const double len = length();
    return len > 0 ? *this / len : *this;
}

std::ostream& operator<<(std::ostream& out, Vector2 v) {
    return out << '(' << v.x << ", " << v.y << ')';
}
std::ostream& operator<<(std::ostream& out, Vector3 v) {
    return out << '(' << v.x << ", " << v.y << ", " << v.z << ')';
}

} // namespace libopencad
