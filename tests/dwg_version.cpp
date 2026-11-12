// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_version.hpp>
#include <iostream>
#include <string>

int main() {
    using libopencad::DwgVersion;
    struct Case { const char* suffix; DwgVersion version; };
    constexpr Case cases[] = {
        {"1009", DwgVersion::AC9}, {"1012", DwgVersion::AC12}, {"1014", DwgVersion::AC12},
        {"1015", DwgVersion::AC15}, {"1018", DwgVersion::AC18}, {"1021", DwgVersion::AC21},
        {"1024", DwgVersion::AC24}, {"1027", DwgVersion::AC24}, {"1032", DwgVersion::AC24}
    };
    for (const auto& item : cases) {
        for (const auto prefix : {"AC", "AD"}) {
            const auto code = std::string(prefix) + item.suffix;
            if (libopencad::from_version_string(code) != item.version) {
                std::cerr << "Wrong DWG family for " << code << '\n';
                return 1;
            }
        }
    }
    for (const auto code : {"", "AC103", "AC10320", "AX1032", "AC1033", "ac1014"}) {
        if (libopencad::from_version_string(code)) {
            std::cerr << "Unexpectedly accepted '" << code << "'\n";
            return 1;
        }
    }
}
