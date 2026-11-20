// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/nested_copy.rs destination-name assignment.
#include <libopencad/nested_copy.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace libopencad {
namespace {

std::string ascii_upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

} // namespace

std::unordered_map<std::string, std::string> nested_copy_destinations(std::vector<std::string> names,
                                                                      NestedCopyMode mode) {
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        return ascii_upper(a) < ascii_upper(b);
    });
    std::unordered_set<std::string> used;
    for (const auto &name : names)
        used.insert(ascii_upper(name));
    std::unordered_map<std::string, std::string> result;
    for (const auto &source : names) {
        auto bar = source.rfind('|');
        if (bar == std::string::npos)
            continue;
        auto prefix = source.substr(0, bar);
        auto local = source.substr(bar + 1);
        if (prefix.find('|') != std::string::npos)
            continue;
        std::string destination;
        if (mode == NestedCopyMode::Insert)
            destination = local;
        else {
            std::size_t index = 0;
            for (;;) {
                destination = prefix + "$" + std::to_string(index) + "$" + local;
                if (!used.count(ascii_upper(destination)))
                    break;
                ++index;
            }
        }
        used.insert(ascii_upper(destination));
        result[ascii_upper(source)] = destination;
    }
    return result;
}

} // namespace libopencad
