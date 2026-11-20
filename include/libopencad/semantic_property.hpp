// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
namespace libopencad {
// Registered application classes have no published fixed schema. This matches
// acadrust's SemanticProperty rather than claiming private application behavior.
using SemanticValue = std::variant<std::string, bool, std::uint8_t, std::int16_t, std::int32_t, std::int64_t,
                                   double, std::uint64_t, std::vector<std::uint8_t>>;
struct SemanticProperty {
    std::string subclass;
    int code;
    SemanticValue value;
};
} // namespace libopencad
