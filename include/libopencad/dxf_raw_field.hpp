// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string>
namespace libopencad {
// Decoded UTF-8 strings, decimal numeric values, hexadecimal handles/binary data.
struct DxfRawField {
    int code;
    std::string value;
};
} // namespace libopencad
