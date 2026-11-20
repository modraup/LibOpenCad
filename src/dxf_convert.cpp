// SPDX-License-Identifier: MPL-2.0
// acadrust 0c3ae80d io/dxf/writer/{mod,section_writer}.rs.
// Scoped rules never interpret application groups, XDATA or XRECORD payloads.
#include "dxf_approximation.hpp"
#include "dxf_binary.hpp"
#include "dxf_code_page.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <libopencad/dxf_document.hpp>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <streambuf>

namespace libopencad {
namespace {
using Kind = DxfConversionIssueKind;
using V = DxfVersion;
bool one_of(std::string_view value, std::initializer_list<std::string_view> names) {
    return std::find(names.begin(), names.end(), value) != names.end();
}
bool code_in(int code, std::initializer_list<int> codes) {
    return std::find(codes.begin(), codes.end(), code) != codes.end();
}
// Bounded staging buffer: an oversized export never reaches the caller's stream.
class ConversionBuffer : public std::streambuf {
  public:
    explicit ConversionBuffer(std::size_t limit) : limit_(limit) {}
    std::string bytes;

  protected:
    std::streamsize xsputn(const char *p, std::streamsize n) override {
        if (n < 0 || static_cast<std::size_t>(n) > limit_ - bytes.size())
            throw std::length_error("DXF conversion output limit exceeded");
        bytes.append(p, static_cast<std::size_t>(n));
        return n;
    }
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            const auto ch = traits_type::to_char_type(c);
            xsputn(&ch, 1);
        }
        return traits_type::not_eof(c);
    }

  private:
    std::size_t limit_;
};
} // namespace
class DxfVersionConverter {
  public:
    DxfVersionConverter(const DxfDocument &source, V target, bool binary, const DxfConversionOptions &options)
        : doc_(source), options_(options) {
        report_.source = source.version();
        report_.requested = target;
        report_.effective = target;
        report_.binary = binary;
    }
    DxfConversionResult run() {
        try {
            if (report_.source < V::AC1009 || report_.requested < V::AC1012)
                throw std::invalid_argument(
                    "This converter requires R12-R2018 source and R13-R2018 target; R12 output uses "
                    "its dedicated export path");
            if (options_.policy != DxfOutputPolicy::PreserveVersion &&
                options_.policy != DxfOutputPolicy::UpstreamCompatible)
                throw std::invalid_argument("Unknown output policy");
            report_.effective =
                dxf_output_plan(report_.source, report_.binary, report_.requested, options_.policy).effective;
            if (report_.effective != report_.requested)
                issue(Kind::Information, "target-promoted", nullptr, 0,
                      "Upstream binary policy promotes the requested target to AC1024");
            if (report_.source != report_.effective) {
                if (!doc_.duplicate_handles().empty())
                    throw std::invalid_argument("Version conversion requires unique record identities");
                if (report_.source == V::AC1009)
                    upgrade_r12();
                convert();
            }
            if (!refused()) {
                ConversionBuffer buffer(options_.max_output_bytes);
                std::ostream output(&buffer);
                output.exceptions(std::ios::badbit | std::ios::failbit);
                doc_.write(output, report_.binary);
                report_.can_write = true;
                return {std::move(buffer.bytes), std::move(report_)};
            }
        } catch (const std::exception &e) {
            issue(Kind::Error, "conversion-failed", nullptr, 0, e.what());
        }
        return {{}, std::move(report_)};
    }

  private:
    DxfDocument doc_;
    DxfConversionOptions options_;
    DxfConversionReport report_;
    bool changed_ = false, r12_defaults_created_ = false;
    std::set<std::string> r12_imported_, r12_header_variables_;
    V target() const { return report_.effective; }
    static DxfCompoundEntity *sequence(DxfDocument::Record &entry) {
        return std::visit(
            [](auto &r) -> DxfCompoundEntity * {
                if constexpr (std::is_base_of_v<DxfCompoundEntity, std::decay_t<decltype(r)>>)
                    return &r;
                else
                    return nullptr;
            },
            entry);
    }
    void issue(Kind kind, std::string code, const DxfRecord *r, int group, std::string message) {
        // Target-version pruning of newly synthesized defaults cannot lose source data.
        if (kind == Kind::Loss && r12_defaults_created_ && r && r->type() != "SECTION" &&
            !r12_imported_.count(r->handle()))
            kind = Kind::Information;
        report_.issues.push_back({kind, std::move(code), r ? r->type() : "HEADER", r ? r->handle() : "",
                                  std::move(message), group});
    }
    bool refused() const {
        return std::any_of(report_.issues.begin(), report_.issues.end(), [&](const auto &i) {
            return i.kind == Kind::Error || (i.kind == Kind::Loss && !options_.allow_loss);
        });
    }
    void normalize(DxfRecord &r, const char *page) {
        auto codec = r;
        codec.binary_ = false;
        codec.code_page_ = page;
        for (auto &p : r.pairs_) {
            if (detail::is_string_value(p.code))
                p.value = codec.encode_string(r.decode_string(p.value));
            else if (r.binary_)
                p.value = detail::text_value_from_binary(p.code, p.value);
        }
        r.binary_ = false;
        r.code_page_ = page;
    }
    // Only direct fields in the named subclass are eligible, including repeated
    // fields. Private 102 groups and all application payload tails are protected.
    void filter(DxfRecord &r, std::string_view scope, const std::function<bool(int)> &remove,
                Kind kind = Kind::Loss,
                const std::string &reason = "Field is not emitted by the target-version writer",
                bool remove_marker = false) {
        std::vector<DxfRecord::Pair> kept;
        std::string current;
        unsigned depth = 0;
        bool tail = false;
        for (const auto &p : r.pairs_) {
            if (!depth && (p.code == 1001 || p.code == 101))
                tail = true;
            if (!tail && p.code == 102) {
                if (!p.value.empty() && p.value.front() == '{')
                    ++depth;
                else if (p.value == "}" && depth)
                    --depth;
                kept.push_back(p);
                continue;
            }
            if (!tail && !depth && p.code == 100)
                current = p.value;
            if (!tail && !depth && (p.code != 100 || remove_marker) && current == scope && remove(p.code)) {
                issue(kind, "field-removed", &r, p.code, reason);
                changed_ = true;
            } else
                kept.push_back(p);
        }
        r.pairs_ = std::move(kept);
    }
    void drop(DxfRecord &r, std::string_view scope, std::initializer_list<int> codes, V minimum,
              Kind kind = Kind::Loss) {
        if (target() < minimum)
            filter(r, scope, [&](int c) { return code_in(c, codes); }, kind);
    }
    void put(DxfRecord &r, std::string_view scope, int code, std::string value) {
        if (r.field(scope, code) != value) {
            r.set_field(scope, code, std::move(value));
            changed_ = true;
        }
    }
    void header(DxfRecord &r) {
        const std::map<std::string, V> minimum{{"$TITLE", V::AC1018},
                                               {"$SUBJECT", V::AC1018},
                                               {"$AUTHOR", V::AC1018},
                                               {"$KEYWORDS", V::AC1018},
                                               {"$COMMENTS", V::AC1018},
                                               {"$LASTSAVEDBY", V::AC1018},
                                               {"$REVISIONNUMBER", V::AC1018},
                                               {"$CUSTOMPROPERTYTAG", V::AC1018},
                                               {"$CUSTOMPROPERTY", V::AC1018},
                                               {"$HYPERLINKBASE", V::AC1015},
                                               {"$DIMTXSTY", V::AC1015},
                                               {"$DIMLTYPE", V::AC1015},
                                               {"$DIMLTEX1", V::AC1015},
                                               {"$DIMLTEX2", V::AC1015},
                                               {"$XCLIPFRAME", V::AC1015},
                                               {"$TDUCREATE", V::AC1015},
                                               {"$TDUUPDATE", V::AC1015},
                                               {"$FINGERPRINTGUID", V::AC1015},
                                               {"$VERSIONGUID", V::AC1015},
                                               {"$DIMFXL", V::AC1021},
                                               {"$DIMFXLON", V::AC1021},
                                               {"$DIMARCSYM", V::AC1021},
                                               {"$DIMJOGANG", V::AC1021},
                                               {"$DIMTFILL", V::AC1021},
                                               {"$DIMTFILLCLR", V::AC1021},
                                               {"$DIMTXTDIRECTION", V::AC1027},
                                               {"$DIMALTMZF", V::AC1032},
                                               {"$DIMALTMZS", V::AC1032},
                                               {"$DIMMZF", V::AC1032},
                                               {"$DIMMZS", V::AC1032},
                                               {"$DWFFRAME", V::AC1021},
                                               {"$DGNFRAME", V::AC1021},
                                               {"$SOLIDHIST", V::AC1021},
                                               {"$SHOWHIST", V::AC1021},
                                               {"$3DDWFPREC", V::AC1021},
                                               {"$PSOLWIDTH", V::AC1021},
                                               {"$PSOLHEIGHT", V::AC1021},
                                               {"$LIGHTGLYPHDISPLAY", V::AC1021},
                                               {"$TILEMODELIGHTSYNCH", V::AC1021},
                                               {"$REALWORLDSCALE", V::AC1021},
                                               {"$INTERFERECOLOR", V::AC1021}};
        std::vector<DxfRecord::Pair> pairs;
        bool version = false;
        for (std::size_t i = 0; i < r.pairs_.size();) {
            if (r.pairs_[i].code != 9) {
                pairs.push_back(r.pairs_[i++]);
                continue;
            }
            const auto begin = i++;
            while (i < r.pairs_.size() && r.pairs_[i].code != 9)
                ++i;
            const auto &name = r.pairs_[begin].value;
            if (name == "$ACADVER") {
                pairs.insert(pairs.end(), {{9, name}, {1, std::string(dxf_version_string(target()))}});
                version = true;
                continue;
            }
            if (name == "$ACADMAINTVER")
                continue;
            auto gate = minimum.find(name);
            // Unknown header variables are preserved, not guessed by number.
            if (gate != minimum.end() && target() < gate->second) {
                issue(report_.source == V::AC1009 && !r12_header_variables_.count(name) ? Kind::Information
                                                                                        : Kind::Loss,
                      "header-removed", &r, 9, name + " is unavailable in the target writer");
                continue;
            }
            pairs.insert(pairs.end(), r.pairs_.begin() + begin, r.pairs_.begin() + i);
        }
        if (!version)
            pairs.insert(pairs.end(), {{9, "$ACADVER"}, {1, std::string(dxf_version_string(target()))}});
        if (target() >= V::AC1015)
            pairs.insert(pairs.end(),
                         {{9, "$ACADMAINTVER"},
                          {target() >= V::AC1032 ? 90 : 70, target() >= V::AC1032 ? "377" : "0"}});
        r.pairs_ = std::move(pairs);
        changed_ = true;
    }
    void block_units(DxfRecord &r) {
        auto units = r.short_field("AcDbBlockTableRecord", 70, 0);
        auto data = r.extended_data();
        auto app = std::find_if(data.begin(), data.end(),
                                [](const auto &a) { return a.application_name == "ACAD"; });
        const bool design = app != data.end() && !app->values.empty() && app->values[0].code == 1000 &&
                            std::get<std::string>(app->values[0].value) == "DesignCenter Data";
        const bool valid = design && app->values.size() >= 5 && app->values[1].code == 1002 &&
                           std::get<std::string>(app->values[1].value) == "{" &&
                           app->values[2].code == 1070 && app->values[3].code == 1070 &&
                           app->values[4].code == 1002 && std::get<std::string>(app->values[4].value) == "}";
        if (design && !valid) {
            issue(Kind::Error, "block-units-xdata-invalid", &r, 1001, "Incomplete DesignCenter units packet");
            return;
        }
        if (valid && r.field("AcDbBlockTableRecord", 70).empty())
            units = std::get<std::int16_t>(app->values[3].value);
        if (target() >= V::AC1021) {
            put(r, "AcDbBlockTableRecord", 70, std::to_string(units));
            return;
        }
        // Merge rather than replacing unrelated ACAD extended data. Zero units
        // also overwrite a stale DesignCenter value when modern group 70 exists.
        if (units != 0 || valid) {
            if (app != data.end() && !valid) {
                issue(Kind::Error, "block-units-xdata-conflict", &r, 1001,
                      "Cannot replace unrelated ACAD XDATA with DesignCenter units");
            } else if (!doc_.get_symbol(SymbolTable::AppId, "ACAD")) {
                issue(Kind::Error, "block-units-appid-missing", &r, 1001,
                      "DesignCenter units require the ACAD application registration");
            } else {
                ExtendedDataRecord x{"ACAD",
                                     {{1000, std::string("DesignCenter Data")},
                                      {1002, std::string("{")},
                                      {1070, std::int16_t(1)},
                                      {1070, std::int16_t(units)},
                                      {1002, std::string("}")}}};
                if (valid) {
                    x = *app;
                    x.values[3].value = std::int16_t(units);
                }
                r.upsert_extended_data(x);
                changed_ = true;
                issue(Kind::Information, "block-units-xdata", &r, 70,
                      "Block units moved to DesignCenter ACAD XDATA");
            }
        }
        drop(r, "AcDbBlockTableRecord", {70}, V::AC1021, Kind::Information);
        drop(r, "AcDbBlockTableRecord", {280, 281}, V::AC1021);
    }
    void dimstyle(DxfRecord &r) {
        const auto scope = "AcDbDimStyleTableRecord";
        for (auto codes : {std::pair<int, int>{342, 5}, {343, 6}, {344, 7}}) {
            if (target() <= V::AC1014) {
                const auto h = r.field(scope, codes.first);
                if (!h.empty() && h != "0") {
                    auto *b = doc_.block_record(h);
                    if (!b)
                        issue(Kind::Error, "dimstyle-arrow-reference", &r, codes.first,
                              "Missing arrow block handle");
                    else
                        put(r, scope, codes.second, r.encode_string(b->name()));
                }
            } else {
                const auto name = r.text_field(scope, codes.second);
                if (!name.empty()) {
                    const auto b = doc_.get_symbol(SymbolTable::BlockRecord, name);
                    if (!b)
                        issue(Kind::Error, "dimstyle-arrow-name", &r, codes.second,
                              "Missing arrow block name");
                    else
                        put(r, scope, codes.first, b->handle);
                }
            }
        }
        if (target() > V::AC1014) {
            filter(
                r, scope, [](int c) { return code_in(c, {5, 6, 7}); }, Kind::Information,
                "Arrow names use block handles");
            filter(r, scope, [](int c) { return code_in(c, {270, 287}); });
        } else {
            if (r.field(scope, 270).empty())
                put(r, scope, 270, "2");
            if (r.field(scope, 287).empty())
                put(r, scope, 287, "0");
            filter(r, scope, [](int c) { return c == 289; });
        }
        drop(r, scope, {69, 70, 290, 345, 346, 347}, V::AC1021);
        drop(r, scope, {295}, V::AC1024);
        drop(r, scope, {341, 371, 372}, V::AC1015);
        drop(r, scope, {342, 343, 344}, V::AC1015, Kind::Information);
    }
    void common(DxfEntity &e) {
        e.version_ = target();
        if (target() < V::AC1018) {
            const auto color = e.color();
            if (color.kind() == Color::Kind::Rgb) {
                e.set_color(Color::from_index(color.approximate_index()));
                changed_ = true;
                issue(Kind::Loss, "true-color-to-aci", &e, 420,
                      "RGB entity color approximated to ACI, matching upstream");
            }
        }
        drop(e, "AcDbEntity", {420, 430, 440}, V::AC1018);
        drop(e, "AcDbEntity", {390}, V::AC1015);
        drop(e, "AcDbEntity", {348}, V::AC1024);
        const auto graphics = e.graphic_data();
        if (graphics) {
            e.set_graphic_data(graphics);
            changed_ = true;
        }
    }
    // Geometry mapping follows Rust's R12Writer::write_lwpolyline; modern
    // subclasses/ownership follow SectionWriter::write_polyline2d. Keep the
    // parent identity and payloads instead of inserting a replacement entity.
    void expand_lightweight(DxfDocument::Record &entry) {
        const auto *source = std::get_if<DxfLwPolyline>(&entry);
        if (!source || target() != V::AC1012)
            return;
        if (source->handle().empty() || !doc_.get_record(source->handle()) ||
            !doc_.block_record(source->owner_handle()))
            throw std::invalid_argument(
                "R13 polyline expansion requires a parent identity and BLOCK_RECORD owner");
        std::vector<DxfRecord::Pair> retained;
        std::string scope;
        unsigned depth = 0;
        bool tail = false, found = false, pending_x = false;
        std::size_t count = 0, declared = 0, count_fields = 0;
        int vertex_fields = 0;
        std::set<int> singleton_fields;
        for (const auto &p : source->pairs_) {
            if (!depth && (p.code == 1001 || p.code == 101))
                tail = true;
            if (!tail && p.code == 102) {
                if (!p.value.empty() && p.value.front() == '{')
                    ++depth;
                else if (p.value == "}" && depth)
                    --depth;
                retained.push_back(p);
                continue;
            }
            if (!tail && !depth && p.code == 100) {
                scope = p.value;
                if (scope != "AcDbPolyline" && scope != "AcDbEntity")
                    throw std::invalid_argument("Unreviewed LWPOLYLINE subclass in R13 conversion");
                if (scope == "AcDbPolyline") {
                    if (found)
                        throw std::invalid_argument("Repeated LWPOLYLINE geometry subclass");
                    found = true;
                    retained.push_back({100, "AcDb2dPolyline"});
                    continue;
                }
            }
            if (!tail && !depth && scope == "AcDbPolyline") {
                if (!code_in(p.code, {90, 70, 38, 39, 43, 210, 220, 230, 10, 20, 40, 41, 42, 91}))
                    throw std::invalid_argument("Unreviewed LWPOLYLINE geometry field in R13 conversion");
                if (code_in(p.code, {90, 70, 38, 39, 43, 210, 220, 230}) &&
                    !singleton_fields.insert(p.code).second)
                    throw std::invalid_argument("Repeated LWPOLYLINE header field");
                const auto first = p.value.find_first_not_of(" \t\r\n");
                const auto last = p.value.find_last_not_of(" \t\r\n");
                if (first == std::string::npos)
                    throw std::invalid_argument("Empty LWPOLYLINE geometry value");
                const auto text = p.value.substr(first, last - first + 1);
                std::size_t used = 0;
                const auto n = code_in(p.code, {90, 70, 91}) ? static_cast<double>(std::stoll(text, &used))
                                                             : std::stod(text, &used);
                if (used != text.size() || !std::isfinite(n))
                    throw std::invalid_argument("Invalid LWPOLYLINE geometry value");
                if ((p.code == 40 || p.code == 41 || p.code == 43) && n < 0)
                    throw std::invalid_argument("Negative LWPOLYLINE width");
                if (p.code == 90) {
                    if (n < 0 || n > INT32_MAX || std::floor(n) != n)
                        throw std::invalid_argument("Invalid LWPOLYLINE vertex count");
                    declared = static_cast<std::size_t>(n);
                    ++count_fields;
                }
                if (p.code == 70 && (n < 0 || n > 32767 || std::floor(n) != n))
                    throw std::invalid_argument("Invalid LWPOLYLINE flags");
                if (p.code == 91 && (n < INT32_MIN || n > INT32_MAX || std::floor(n) != n))
                    throw std::invalid_argument("Invalid LWPOLYLINE vertex ID");
                if (p.code == 10) {
                    if (pending_x)
                        throw std::invalid_argument("LWPOLYLINE vertex missing Y");
                    pending_x = true;
                    vertex_fields = 0;
                } else if (p.code == 20) {
                    if (!pending_x)
                        throw std::invalid_argument("LWPOLYLINE vertex missing X");
                    pending_x = false;
                    ++count;
                } else if (code_in(p.code, {40, 41, 42, 91})) {
                    const auto bit = 1 << (p.code == 91 ? 3 : p.code - 40);
                    if (pending_x || !count || (vertex_fields & bit))
                        throw std::invalid_argument("Ambiguous LWPOLYLINE vertex attributes");
                    vertex_fields |= bit;
                }
                continue;
            }
            retained.push_back(p);
        }
        if (depth || !found || pending_x || count_fields != 1 || count != declared)
            throw std::invalid_argument("LWPOLYLINE declared and actual vertex counts differ");
        const auto vertices = source->vertices();
        if (vertices.size() != count)
            throw std::invalid_argument("Incomplete LWPOLYLINE vertices");
        DxfPolyline2D result;
        static_cast<DxfEntity &>(result) = static_cast<const DxfEntity &>(*source);
        result.pairs_ = std::move(retained);
        result.pairs_.front().value = "POLYLINE";
        result.version_ = target();
        result.set_short_field("AcDb2dPolyline", 66, 1);
        result.set_flags((source->is_closed() ? 1 : 0) | (source->plinegen() ? 128 : 0));
        result.set_elevation(source->elevation());
        result.set_thickness(source->thickness());
        result.set_normal(source->normal());
        result.set_default_widths(source->constant_width(), source->constant_width());
        if (source->short_field("AcDbPolyline", 70, 0) & ~129)
            issue(Kind::Loss, "polyline-flags-removed", source, 70,
                  "Unmodeled lightweight flags have no traditional 2D equivalent");
        auto fresh_handle = [&] {
            std::ostringstream value;
            value << std::uppercase << std::hex << doc_.allocate_handle();
            return value.str();
        };
        auto child = [&](const char *type) {
            DxfEntity r;
            r.code_page_ = source->code_page_;
            r.version_ = target();
            r.pairs_ = {{0, type}, {5, fresh_handle()}, {330, source->handle()}, {100, "AcDbEntity"}};
            r.set_layer(source->layer());
            for (int code : {67, 410}) {
                const auto value = source->field("AcDbEntity", code);
                if (!value.empty())
                    r.set_field("AcDbEntity", code, value);
            }
            return r;
        };
        for (const auto &v : vertices) {
            auto r = child("VERTEX");
            // Modern 2D vertices are OCS XY; elevation is stored on the parent.
            r.set_color(Color::from_index(source->color().approximate_index()));
            r.pairs_.push_back({100, "AcDbVertex"});
            r.pairs_.push_back({100, "AcDb2dVertex"});
            r.set_point("AcDb2dVertex", 10, {v.x, v.y, 0});
            r.set_short_field("AcDb2dVertex", 70, 0);
            // Match the legacy writer: absent zero widths inherit the header.
            // When either width is nonzero, explicitly retain both endpoints.
            if (v.start_width != 0 || v.end_width != 0) {
                r.set_number("AcDb2dVertex", 40, v.start_width);
                r.set_number("AcDb2dVertex", 41, v.end_width);
            }
            r.set_number("AcDb2dVertex", 42, v.bulge);
            result.children_.push_back(std::move(r));
            if (v.vertex_id)
                issue(Kind::Loss, "vertex-id-removed", source, 91,
                      "R13 traditional vertices do not retain lightweight vertex IDs");
        }
        result.seqend_ = child("SEQEND");
        issue(
            Kind::Information, "lwpolyline-to-polyline", source, 0,
            "Expanded into ordered VERTEX/SEQEND records with fresh child handles; parent identity retained");
        entry = std::move(result);
        changed_ = true;
    }
    void rules(DxfRecord &r) {
        const auto type = r.type();
        if (type == "CLASS")
            drop(r, "", {91}, V::AC1018, Kind::Information);
        if (type == "BLOCK_RECORD")
            block_units(r);
        if (type == "DIMSTYLE")
            dimstyle(r);
        if (type == "TABLE" && r.field("", 2) == "DIMSTYLE" && target() < V::AC1015) {
            filter(
                r, "AcDbDimStyleTable", [](int) { return true; }, Kind::Information,
                "Legacy DIMSTYLE table omits its specialized subclass", true);
        }
        if (type == "LAYER") {
            drop(r, "AcDbLayerTableRecord", {430}, V::AC1018);
            drop(r, "AcDbLayerTableRecord", {390}, V::AC1015);
        }
        if (type == "LTYPE" && target() < V::AC1027) {
            auto fields = r.subclass_fields("AcDbLinetypeTableRecord");
            bool changed = false;
            for (auto &p : fields)
                if (p.code == 74) {
                    int n = std::stoi(p.value);
                    if (n & 8) {
                        p.value = std::to_string(n & ~8);
                        changed = true;
                        issue(Kind::Loss, "linetype-upright-removed", &r, 74,
                              "Upright complex-linetype flag requires R2013");
                    }
                }
            if (changed) {
                r.set_subclass_fields("AcDbLinetypeTableRecord", fields);
                changed_ = true;
            }
        }
        if (type == "VPORT" || type == "VIEW") {
            const auto scope = type == "VPORT" ? "AcDbViewportTableRecord" : "AcDbViewTableRecord";
            drop(r, scope, {281, 110, 120, 130, 111, 121, 131, 112, 122, 132, 79, 146, 345, 346}, V::AC1015);
            drop(r, scope, type == "VPORT" ? std::initializer_list<int>{65} : std::initializer_list<int>{72},
                 V::AC1015);
            drop(r, scope, {332, 348, 361, 292, 282, 141, 142, 63, 421}, V::AC1021);
            drop(r, scope,
                 type == "VPORT" ? std::initializer_list<int>{60, 61} : std::initializer_list<int>{73, 334},
                 V::AC1021);
        }
        if (type == "UCS")
            drop(r, "AcDbUCSTableRecord", {71, 79, 146, 346}, V::AC1015);
        if (type == "LWPOLYLINE") {
            drop(r, "AcDbPolyline", {91}, V::AC1024);
            if (target() == V::AC1012)
                issue(Kind::Error, "lwpolyline-r13", &r, 0,
                      "R13 requires traditional POLYLINE conversion; this schema conversion is not "
                      "implemented");
        }
        if (one_of(type, {"DIMENSION", "ARC_DIMENSION", "LARGE_RADIAL_DIMENSION"})) {
            drop(r, "AcDbDimension", {280}, V::AC1024, Kind::Information);
            drop(r, "AcDbDimension", {73, 74, 75}, V::AC1021);
        }
        if (type == "VIEWPORT") {
            drop(r, "AcDbViewport", {170}, V::AC1018);
            drop(r, "AcDbViewport", {332, 333, 348, 292, 282, 141, 142, 63, 421, 361}, V::AC1021);
            if (target() < V::AC1021) {
                const auto n = static_cast<std::uint32_t>(std::stoul(r.field("AcDbViewport", 90, "0")));
                if (n & (15u << 18)) {
                    put(r, "AcDbViewport", 90, std::to_string(n & ~(15u << 18)));
                    issue(Kind::Loss, "viewport-grid-flags", &r, 90, "R2007 grid flags removed");
                }
            }
        }
        if (type == "MLINESTYLE")
            drop(r, "AcDbMlineStyle", {420}, V::AC1018);
        if (type == "MULTILEADER" || type == "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS") {
            const auto scope = type == "MULTILEADER" ? "AcDbMLeader" : "AcDbMLeaderObjectContextData";
            drop(r, scope, {270,271,272,273}, V::AC1024);
            drop(r, scope, {295}, V::AC1027);
        }
        if (type=="ACDBDETAILVIEWSTYLE" || type=="ACDBSECTIONVIEWSTYLE") {
            const auto scope="AcDbModelDocViewStyle";
            drop(r,scope,{300,90},V::AC1032);
            if(target()>=V::AC1032 && report_.source<V::AC1032) {
                put(r,scope,300,r.field(scope,3));
                put(r,scope,90,"0");
            }
        }
        if (type=="RENDERSETTINGS" && (target()>=V::AC1027)!=(report_.source>=V::AC1027)) {
            const auto scope="AcDbRenderSettings";
            auto fields=r.subclass_fields(scope);
            // Rust writes two group 90s and four consecutive group 290s. The
            // fifth boolean follows the display index only in the new layout.
            const std::vector<int> expected{90,1,290,290,290,290,1,1,90};
            if(fields.size()!=expected.size()+(report_.source>=V::AC1027) ||
               !std::equal(expected.begin(),expected.end(),fields.begin(),[](int code,const auto& f){return code==f.code;}))
                throw std::invalid_argument("Unrecognized render settings field order");
            const auto version=std::stoll(fields.front().value)+(target()>=V::AC1027?1:-1);
            if(version<INT32_MIN || version>INT32_MAX) throw std::invalid_argument("Render class version overflow");
            fields.front().value=std::to_string(version);
            if(target()>=V::AC1027) fields.push_back({290,"0"});
            else {
                if(fields.back().code!=290) throw std::invalid_argument("Missing predefined flag");
                if(fields.back().value!="0") issue(Kind::Loss,"render-predefined-removed",&r,290,"Legacy render settings omit the predefined flag");
                fields.pop_back();
            }
            r.set_subclass_fields(scope,fields);
            changed_=true;
        }
        if (type == "MLEADERSTYLE") {
            drop(r, "AcDbMLeaderStyle", {179}, V::AC1024, Kind::Information);
            drop(r, "AcDbMLeaderStyle", {271, 272, 273}, V::AC1024);
            drop(r, "AcDbMLeaderStyle", {298}, V::AC1027);
        }
        if (type == "WIPEOUT")
            drop(r, "AcDbWipeout", {290}, V::AC1024);
        if (type == "ACDBPLACEHOLDER" && target() < V::AC1015 &&
            std::none_of(r.pairs_.begin(), r.pairs_.end(),
                         [](const auto &p) { return p.code == 100 && p.value == "AcDbPlaceHolder"; })) {
            r.pairs_.push_back({100, "AcDbPlaceHolder"});
            changed_ = true;
        }
    }
#include "dxf_convert_visual_style.inc"
#include "dxf_convert_attributes.inc"
#include "dxf_convert_legacy_table.inc"
#include "dxf_convert_mtext.inc"
#include "dxf_upgrade_r12.inc"
    void convert() {
        if (report_.source < V::AC1021 || target() < V::AC1024) collect_legacy_table_styles();
        if (target() == V::AC1021 && report_.source >= V::AC1024) refresh_table_adjuncts();
        split_mtext_columns();
        split_multiline_attributes();
        if (refused())
            return;
        std::string page = "ANSI_1252";
        if (doc_.header_index_) {
            const auto &h = doc_.record_at({*doc_.header_index_, std::nullopt, false});
            for (std::size_t i = 1; i < h.pairs_.size(); ++i)
                if (h.pairs_[i - 1].code == 9 && h.pairs_[i - 1].value == "$DWGCODEPAGE")
                    page = h.pairs_[i].value;
        } else
            throw std::invalid_argument("Missing HEADER section");
        const auto codec = detail::legacy_page(dxf_version_string(target()), page);
        issue(Kind::Information, "encoding-converted", nullptr, 3,
              target() < V::AC1021 ? "Legacy code-page encoding with Unicode escapes" : "UTF-8 encoding");
        for (auto &entry : doc_.records_) {
            std::visit([&](DxfRecord &r) { normalize(r, codec); }, entry);
            if (auto *group = sequence(entry)) {
                for (auto &child : group->children_)
                    normalize(child, codec);
                if (group->seqend_)
                    normalize(*group->seqend_, codec);
            }
        }
        doc_.binary_ = false;
        doc_.compact_codes_ = false;
        doc_.version_ = target();
        // Indexes retain handles but names must use the target codec before lookup.
        doc_.rebuild_indexes();
        merge_multiline_attributes();
        if (refused())
            return;
        const std::set<std::string> supported{"SECTION",
                                              "ENDSEC",
                                              "EOF",
                                              "TABLE",
                                              "ENDTAB",
                                              "CLASS",
                                              "BLOCK",
                                              "ENDBLK",
                                              "VERTEX",
                                              "SEQEND",
                                              "LAYER",
                                              "LTYPE",
                                              "STYLE",
                                              "DIMSTYLE",
                                              "UCS",
                                              "VIEW",
                                              "VPORT",
                                              "APPID",
                                              "BLOCK_RECORD",
                                              "LINE",
                                              "POINT",
                                              "RAY",
                                              "XLINE",
                                              "CIRCLE",
                                              "ARC",
                                              "ELLIPSE",
                                              "TEXT",
                                              "ATTDEF",
                                              "ATTRIB",
                                              "INSERT",
                                              "POLYLINE",
                                              "LWPOLYLINE",
                                              "SOLID",
                                              "TRACE",
                                              "3DFACE",
                                              "SHAPE",
                                              "SPLINE",
                                              "HELIX",
                                              "MTEXT",
                                              "HATCH",
                                              "MPOLYGON",
                                              "DIMENSION",
                                              "ARC_DIMENSION",
                                              "LARGE_RADIAL_DIMENSION",
                                              "LEADER",
                                              "TOLERANCE",
                                              "VIEWPORT",
                                              "MLINE",
                                              "DICTIONARY",
                                              "ACDBDICTIONARYWDFLT",
                                              "XRECORD",
                                              "DICTIONARYVAR",
                                              "ACDBPLACEHOLDER",
                                              "GROUP",
                                              "LAYOUT",
                                              "PLOTSETTINGS",
                                              "MLINESTYLE",
                                              "MLEADERSTYLE",
                                              "TABLESTYLE",
                                              "MATERIAL",
                                              "DBCOLOR",
                                              "SCALE",
                                              "RASTERVARIABLES",
                                              "IMAGEDEF",
                                              "IMAGEDEF_REACTOR",
                                              "IMAGE",
                                              "WIPEOUT",
                                              "WIPEOUTVARIABLES",
                                              "PDFUNDERLAY",
                                              "DWFUNDERLAY",
                                              "DGNUNDERLAY",
                                              "PDFDEFINITION",
                                              "DWFDEFINITION",
                                              "DGNDEFINITION",
                                              "OLE2FRAME",
                                              "OLEFRAME",
                                              "SPATIAL_FILTER",
                                              "SORTENTSTABLE",
                                              "ACAD_PROXY_ENTITY",
                                              "ACAD_PROXY_OBJECT",
                                              "FIELD",
                                              "FIELDLIST",
                                              "ACAD_TABLE",
                                              "TABLECONTENT",
                                              "TABLETEMPLATE",
                                              "TABLEGEOMETRY",
                                              "CELLSTYLEMAP"};
        for (std::size_t i = 0; i < doc_.records_.size(); ++i) {
            auto &entry = doc_.records_[i];
            changed_ = false;
            try {
                expand_lightweight(entry);
            } catch (const std::exception &error) {
                std::visit(
                    [&](const DxfRecord &record) {
                        issue(Kind::Error, "lwpolyline-expansion-failed", &record, 0, error.what());
                    },
                    entry);
                continue;
            }
            std::visit(
                [&](auto &e) {
                    using T = std::decay_t<decltype(e)>;
                    auto &r = static_cast<DxfRecord &>(e);
                    constexpr bool typed_schema = std::is_same_v<T, DxfDataObject> ||
                        std::is_same_v<T, DxfExtendedEntity> || std::is_same_v<T, DxfObjectContext> ||
                        std::is_same_v<T, DxfSemanticEntity> || std::is_same_v<T, DxfSemanticObject> ||
                        std::is_same_v<T, DxfDrawingObject> || std::is_same_v<T, DxfClassObject> ||
                        std::is_same_v<T, DxfMultiLeader>;
                    if (!typed_schema && !supported.count(r.type()) && r.type() != "MESH") {
                        issue(Kind::Error, "unsupported-schema-conversion", &r, 0,
                              "No reviewed cross-version schema rule for this record type");
                        return;
                    }
                    if (r.type()=="MESH" && !std::is_base_of_v<DxfEntity,T>) {
                        DxfEntity mesh;
                        static_cast<DxfRecord&>(mesh) = r;
                        common(mesh);
                        r=static_cast<DxfRecord&>(mesh);
                    }
                    if constexpr (std::is_same_v<T,DxfMultiLeader>) {
                        if(target()>report_.source) e.set_definition(e.definition());
                    }
                    if(r.type()=="DATALINK") {
                        if(target()<V::AC1021) legacy_table_record(r);
                        else if(report_.source<V::AC1021) upgrade_value_frames(r);
                    }
                    if constexpr (std::is_same_v<T,DxfDrawingObject>) {
                        if(r.type()=="VISUALSTYLE") visual_style(e);
                    }
                    if (doc_.header_index_ == i) {
                        header(r);
                        return;
                    }
                    if constexpr (std::is_base_of_v<DxfEntity, T>)
                        common(e);
                    if constexpr (std::is_same_v<T, DxfProxyEntity> || std::is_same_v<T, DxfProxyObject>) {
                        auto value = e.definition();
                        if (target() < V::AC1021 && value.text_payload.bit_count) {
                            issue(Kind::Error, "proxy-string-stream-version", &r, 162,
                                  "A pre-R2007 proxy cannot declare its separate string stream without "
                                  "losing the payload boundary");
                            return;
                        }
                        if (target() < V::AC1032 &&
                            (value.dwg_version < 0 || value.dwg_version > 65535 ||
                             value.maintenance_version < 0 || value.maintenance_version > 65535)) {
                            issue(Kind::Error, "proxy-version-overflow", &r, 95,
                                  "Proxy version fields do not fit the packed legacy header");
                            return;
                        }
                        e.version_ = target();
                        e.set_definition(value);
                        changed_ = true;
                        issue(Kind::Information, "proxy-envelope-converted", &r, 0,
                              "Converted proxy size/version envelope; original payload bytes and their "
                              "declared DWG version remain opaque");
                    }
                    if constexpr (std::is_same_v<T, DxfField> || std::is_same_v<T, DxfFieldList> ||
                                  std::is_same_v<T, DxfTable> || std::is_same_v<T, DxfTableContent> ||
                                  std::is_same_v<T, DxfTableGeometry> || std::is_same_v<T, DxfCellStyleMap>) {
                        if (target() < V::AC1021) {
                            try {
                                legacy_table_record(r);
                            } catch (const std::exception &err) {
                                issue(Kind::Error, "legacy-table-field-refused", &r, 0, err.what());
                            }
                            return;
                        }
                        if (report_.source < V::AC1021 &&
                            (r.type() == "FIELD" || r.type() == "ACAD_TABLE" || r.type() == "TABLECONTENT" ||
                             r.type() == "TABLETEMPLATE")) {
                            if constexpr (std::is_same_v<T, DxfField>) {
                                auto value = e.definition();
                                if (!value.format.empty()) {
                                    value.value.format = value.format;
                                    issue(Kind::Information, "field-format-upgraded", &r, 4,
                                          "Moved legacy field format into its value");
                                }
                                e.set_definition(value);
                                changed_ = true;
                            } else if constexpr (std::is_same_v<T,DxfTable>) {
                                e.set_definition(e.definition());
                                changed_=true;
                            } else upgrade_value_frames(r);
                            return;
                        }
                        // These reviewed R2007-R2018 writers have the same typed payload layout.
                        // Retain wire data to preserve application payloads and table/content links.
                        issue(Kind::Information, "modern-object-schema-retained", &r, 0,
                              "Reviewed R2007-R2018 schema is unchanged; handles, values and table/content "
                              "references retained");
                    }
                    if constexpr (std::is_same_v<T, DxfMText>) {
                        auto value = e.definition();
                        if (value.columns.type && target() < V::AC1021) {
                            issue(Kind::Error, "mtext-column-downgrade", &r, 0,
                                  "Column decomposition left an unsupported pre-R2007 layout");
                        } else if (value.columns.type && target() < V::AC1032 &&
                                   !doc_.get_symbol(SymbolTable::AppId, "ACAD")) {
                            issue(Kind::Error, "mtext-columns-appid-missing", &r, 1001,
                                  "Legacy MTEXT columns require the ACAD application registration");
                        } else {
                            e.set_definition(value);
                            changed_ = true;
                        }
                    }
                    if constexpr (std::is_same_v<T, DxfAttribute>) {
                        auto value = e.attribute_definition();
                        if ((value.multiline || value.embedded_mtext) && target() < V::AC1032)
                            issue(Kind::Error, "multiline-attribute-downgrade", &r, 101,
                                  "Pinned upstream splits multiline attributes only for R2000-R2013");
                        else {
                            if (value.lock_position && target() < V::AC1021)
                                issue(Kind::Loss, "attribute-lock-removed", &r, 280,
                                      "Lock position requires R2007");
                            e.set_attribute_definition(value);
                            changed_ = true;
                        }
                    }
                    if constexpr (std::is_same_v<T, DxfHatch>) {
                        auto source = e;
                        source.version_ = report_.source;
                        auto definition = source.definition();
                        if (target() < V::AC1024)
                            for (auto &path : definition.paths)
                                for (auto &edge : path.edges)
                                    if (auto *spline = std::get_if<HatchSplineEdge>(&edge)) {
                                        if (!spline->fit_points.empty())
                                            issue(Kind::Loss, "hatch-fit-data-removed", &r, 97,
                                                  "Pre-R2010 HATCH retains knots/control points but omits "
                                                  "spline fit data");
                                        spline->fit_points.clear();
                                        spline->start_tangent = {};
                                        spline->end_tangent = {};
                                    }
                        e.set_definition(definition);
                        changed_ = true;
                    }
                    if constexpr (std::is_same_v<T, DxfClassObject>)
                        e.version_ = target();
                    rules(r);
                },
                entry);
            if (auto *group = sequence(entry)) {
                for (auto &child : group->children_) {
                    if (child.type() == "ATTRIB") {
                        DxfAttribute a;
                        static_cast<DxfRecord &>(a) = child;
                        a.version_ = target();
                        auto def = a.attribute_definition();
                        if ((def.multiline || def.embedded_mtext) && target() < V::AC1032)
                            issue(Kind::Error, "multiline-attribute-downgrade", &child, 101,
                                  "Pinned upstream splits multiline attributes only for R2000-R2013");
                        else {
                            if (def.lock_position && target() < V::AC1021)
                                issue(Kind::Loss, "attribute-lock-removed", &child, 280,
                                      "Lock position requires R2007");
                            a.set_attribute_definition(def);
                            common(a);
                            child = static_cast<DxfRecord &>(a);
                            changed_ = true;
                        }
                    } else {
                        DxfEntity e;
                        static_cast<DxfRecord &>(e) = child;
                        common(e);
                        child = static_cast<DxfRecord &>(e);
                    }
                }
            }
            if (auto *group = sequence(entry); group && group->seqend_) {
                DxfEntity e;
                static_cast<DxfRecord &>(e) = *group->seqend_;
                common(e);
                *group->seqend_ = static_cast<DxfRecord &>(e);
            }
            if (changed_)
                ++report_.changed_records;
        }
        if(report_.source<V::AC1021 && target()>=V::AC1021 && !refused()) refresh_table_adjuncts();
        doc_.rebuild_indexes();
    }
};
DxfConversionResult DxfDocument::export_version(V target, bool binary,
                                                const DxfConversionOptions &options) const {
    return DxfVersionConverter(*this, target, binary, options).run();
}
DxfConversionReport DxfDocument::write_version(std::ostream &output, V target, bool binary,
                                               const DxfConversionOptions &options) const {
    auto result = export_version(target, binary, options);
    if (!result.report.can_write)
        throw DxfConversionError(std::move(result.report));
    output.write(result.bytes.data(), static_cast<std::streamsize>(result.bytes.size()));
    if (!output)
        throw std::runtime_error("DXF conversion output failed");
    return std::move(result.report);
}
} // namespace libopencad
