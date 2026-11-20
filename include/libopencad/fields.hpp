// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/fields.rs FieldContext and evaluation.
#pragma once

#include "sheet_set.hpp"

#include "vector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libopencad {

class DxfDocument;

struct DateLocale {
    std::array<std::string, 12> months;
    std::array<std::string, 12> months_abbr;
    std::array<std::string, 7> days;
    std::array<std::string, 7> days_abbr;
    std::string am = "AM";
    std::string pm = "PM";
    std::string short_date = "M/d/yyyy";
    std::string long_date = "dddd, MMMM d, yyyy";
    std::string long_time = "h:mm:ss tt";
    static DateLocale make_default();
};

class FieldContext {
public:
    virtual ~FieldContext() = default;
    virtual double now_julian() const = 0;
    virtual std::optional<std::pair<double, double>> file_times() const { return std::nullopt; }
    virtual bool plotting() const { return false; }
    virtual std::optional<std::string> login() const { return std::nullopt; }
    virtual std::optional<std::string> getenv(std::string_view) const { return std::nullopt; }
    virtual std::optional<std::string> getvar(std::string_view) const { return std::nullopt; }
    virtual std::optional<std::uint64_t> file_size() const { return std::nullopt; }
    virtual DateLocale date_locale() const { return DateLocale::make_default(); }
    virtual std::optional<std::string>
    sheet_sets(const std::function<std::optional<std::string>(const SheetSetDatabase &)> &) const {
        return std::nullopt;
    }
};

std::optional<std::string> resolve_field(const DxfDocument &doc, std::uint64_t host, const FieldContext &ctx);
std::optional<std::string> resolve_field_handle(const DxfDocument &doc, std::uint64_t field_handle,
                                                std::uint64_t host, const FieldContext &ctx);
std::optional<std::string> cell_field_text(const DxfDocument &doc, std::uint64_t field, std::uint64_t table,
                                           const FieldContext &ctx);
std::string format_with_picture(const DxfDocument &doc, double value, std::string_view picture);
std::string format_point_with_picture(const DxfDocument &doc, Point3 point, std::string_view picture);
std::optional<std::string> evaluate_code(const DxfDocument &doc, std::string_view code,
                                         const std::vector<std::uint64_t> &objects, std::optional<std::uint64_t> host,
                                         const FieldContext &ctx);
// rust fields::inline_code: expand %<_FldIdx n>% to child codes and renumber %<_ObjIdx k>%.
struct InlinedFieldCode {
    std::string code;
    std::vector<std::uint64_t> objects;
};
std::optional<InlinedFieldCode> inline_code(const DxfDocument &doc, std::uint64_t field);
// rust fields::field_spans: UTF-8 [start, end) ranges of filled children when
// the reconstructed template equals `text`.
std::optional<std::vector<std::pair<std::size_t, std::size_t>>> field_spans(const DxfDocument &doc, std::uint64_t host,
                                                                           std::string_view text);
std::array<std::int64_t, 6> julian_parts(double jd);
std::string format_dt(const std::array<std::int64_t, 6> &dt, std::string_view fmt);
std::string format_dt_in(const std::array<std::int64_t, 6> &dt, std::string_view fmt, const DateLocale &loc);

} // namespace libopencad
