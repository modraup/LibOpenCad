// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/nested_copy.rs destination names and localization.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace libopencad {

enum class NestedCopyMode { Insert, Bind };

struct NestedCopySymbolNames {
    std::unordered_map<std::string, std::string> layers;
    std::unordered_map<std::string, std::string> line_types;
};

std::unordered_map<std::string, std::string> nested_copy_destinations(std::vector<std::string> names,
                                                                      NestedCopyMode mode);

} // namespace libopencad
