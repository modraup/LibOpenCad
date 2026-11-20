// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <libopencad/acis.hpp>
#include <string>
#include <string_view>
namespace libopencad::detail {
// SAT transport is ASCII. Non-ASCII modeler data must use SAB; never emit a
// byte sequence that the DWG substitution cipher cannot decode losslessly.
std::string dwg_sat_transport(std::string_view text, bool crlf);
std::string dwg_sat_cipher(std::string_view text);
std::string dwg_sat_from_document(const acis::SatDocument &document, unsigned dwg_version);
} // namespace libopencad::detail
