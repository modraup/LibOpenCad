// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <map>
namespace {
const std::vector<DxfVersion> targets{DxfVersion::AC1015, DxfVersion::AC1018, DxfVersion::AC1021,
                                      DxfVersion::AC1024, DxfVersion::AC1027};
AttributeDefinition multiline(std::string tag, std::string text) {
    AttributeDefinition a;
    a.tag = std::move(tag);
    a.prompt = "输入说明";
    a.multiline = true;
    a.text.text = text;
    a.text.height = 2;
    a.text.insertion = {3, 5, 1};
    a.text.alignment_point = Point3{5, 6, 1};
    a.text.rotation = .5;
    a.text.generation_flags = 6;
    a.lock_position = false;
    MTextDefinition m;
    m.text = std::move(text);
    m.height = 2.5;
    m.rectangle_width = 30;
    m.rectangle_height = 9;
    m.insertion = {7, 8, 1};
    m.rotation = .25;
    m.attachment = 2;
    m.line_spacing_style = 2;
    m.line_spacing_factor = 1.5;
    a.embedded_mtext = m;
    return a;
}
DxfDocument fixture(bool binary) {
    auto d = DxfDocument::create(binary);
    d.add_symbol(SymbolTable::BlockRecord, "属性块");
    EntityOptions o;
    o.block_name = "属性块";
    const auto def = hex(d.add_attribute_definition(multiline("NOTE", "定义甲\\P\\P定义乙"), o));
    AttributeDefinition plain;
    plain.tag = "PLAIN";
    plain.text.text = "相邻单行";
    plain.text.height = 1;
    d.add_attribute_definition(plain, o);
    d.add_line({0, 0, 0}, {1, 2, 0}, o);
    InsertDefinition insert;
    insert.block_name = "属性块";
    const auto model = hex(d.add_insert(insert, {plain, multiline("NOTE", "第一行\\P第二行\\P")}));
    o = {};
    o.space = EntitySpace::Paper;
    d.add_insert(insert, {multiline("PAPER", "纸空间甲\\P纸空间乙"), plain}, o);
    const auto dict = hex(d.add_extension_dictionary(def));
    d.add_xrecord(dict, "Keep", {{1, "保留数据"}, {340, def}});
    d.add_xrecord(dict, "ACAD_XREC_ROUNDTRIP",
                  {{102, "OTHER_APP"},
                   {1, "保留高度之外的数据"},
                   {102, "ACAD_MATT_HEIGHT"},
                   {140, "123"},
                   {90, "42"},
                   {280, "7"}});
    std::string attr;
    d.for_each<DxfInsert>([&](const auto &e) {
        if (e.handle() == model)
            attr = e.attribute(1).handle();
    });
    const auto adict = hex(d.add_extension_dictionary(attr));
    d.add_xrecord(adict, "Keep", {{1, "属性保留"}, {340, attr}});
    d.set_extended_data(attr, {"应用", {{1000, std::string("中文扩展")}}});
    d.add_circle({30, 40, 0}, 2);
    return d;
}
std::vector<DxfAttribute> attributes(const DxfDocument &d) {
    std::vector<DxfAttribute> result;
    d.for_each<DxfAttribute>([&](const auto &a) { result.push_back(a); });
    d.for_each<DxfInsert>([&](const auto &e) {
        for (std::size_t i = 0; i < e.attribute_count(); ++i)
            result.push_back(e.attribute(i));
    });
    return result;
}
void verify(const DxfDocument &source, const DxfDocument &d, DxfVersion version) {
    check(d.version() == version && d.duplicate_handles().empty() && d.reference_issues().empty(),
          "target version and reference graph");
    auto all = attributes(d);
    std::size_t split_count = 0;
    for (const auto &old : attributes(source)) {
        const auto before = old.attribute_definition();
        auto first =
            std::find_if(all.begin(), all.end(), [&](const auto &a) { return a.handle() == old.handle(); });
        check(first != all.end(), "first handle retained");
        if (!before.multiline) {
            check(first->attribute_definition().tag == before.tag &&
                      first->attribute_definition().text.text == before.text.text,
                  "neighbor unchanged");
            continue;
        }
        ++split_count;
        const auto dict = d.extension_dictionary_handle(old.handle());
        const auto ml = d.dictionary_entry(dict, "ACAD_MLATT");
        check(!ml.empty() && d.get_record(ml)->owner_handle() == dict, "MLATT record ownership");
        const auto height = d.dictionary_entry(dict, "ACAD_XREC_ROUNDTRIP");
        check(!height.empty(), "height roundtrip record");
        const auto &m = *before.embedded_mtext;
        std::vector<std::string> lines;
        std::size_t at = 0;
        for (;;) {
            auto end = m.text.find("\\P", at);
            lines.push_back(m.text.substr(at, end == std::string::npos ? end : end - at));
            if (end == std::string::npos)
                break;
            at = end + 2;
        }
        const auto base = std::size_t(first - all.begin());
        const auto pitch = m.height * m.line_spacing_factor * 5 / 3;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto &line = all.at(base + i);
            auto got = line.attribute_definition();
            std::ostringstream tag;
            tag << before.tag << '_' << std::setw(3) << std::setfill('0') << i + 1;
            check(!got.multiline && !got.embedded_mtext && got.tag == tag.str() && got.text.text == lines[i],
                  "ordered flat lines including empty paragraphs");
            check(line.owner_handle() == old.owner_handle(), "line owner retained");
            check(std::abs(got.text.insertion.x -
                           (before.text.insertion.x + std::sin(before.text.rotation) * pitch * i)) < 1e-10 &&
                      std::abs(got.text.insertion.y - (before.text.insertion.y -
                                                       std::cos(before.text.rotation) * pitch * i)) < 1e-10,
                  "rotated line pitch");
            check(got.text.alignment_point && std::abs(got.text.alignment_point->x -
                                                       (before.text.alignment_point->x +
                                                        std::sin(before.text.rotation) * pitch * i)) < 1e-10,
                  "alignment pitch");
            if (i)
                check(source.get_record(line.handle()) == nullptr &&
                          !d.extension_dictionary_handle(line.handle()).empty(),
                      "new line identity and empty dictionary");
            check(got.lock_position == (version >= DxfVersion::AC1021 &&
                                        (before.lock_position || (i && old.type() == "ATTRIB"))),
                  "line lock gate");
        }
        const auto olddict = source.extension_dictionary_handle(old.handle());
        if (!olddict.empty())
            check(dict == olddict &&
                      d.dictionary_entry(dict, "Keep") == source.dictionary_entry(olddict, "Keep"),
                  "existing application dictionary preserved");
    }
    check(split_count == 3, "definition and model/paper attributes split");
    source.for_each<DxfInsert>([&](const auto &before) {
        d.for_each<DxfInsert>([&](const auto &after) {
            if (before.handle() == after.handle())
                check(before.seqend()->handle() == after.seqend()->handle() &&
                          before.seqend()->owner_handle() == after.seqend()->owner_handle(),
                      "SEQEND retained");
        });
    });
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 3, "format prefix");
        const bool binary = std::string(argv[1]) == "binary";
        const std::string prefix = argv[2];
        auto source = fixture(binary);
        const auto original = save(source);
        write(source, prefix + "-source.dxf");
        DxfConversionOptions allow;
        allow.allow_loss = true;
        for (auto version : targets) {
            auto result = source.export_version(version, binary, allow);
            if (!result.report.can_write)
                for (auto &i : result.report.issues)
                    std::cerr << i.code << ": " << i.message << '\n';
            check(result.report.can_write, "multiline downsave");
            auto got = read(result.bytes);
            verify(source, got, version);
            write(got, prefix + "-" + std::string(dxf_version_string(version)) + ".dxf");
            check(save(source) == original, "source unchanged");
            auto upgraded = got.export_version(DxfVersion::AC1032, binary, allow);
            if (!upgraded.report.can_write)
                for (auto &i : upgraded.report.issues)
                    std::cerr << i.code << ": " << i.message << '\n';
            check(upgraded.report.can_write, "merge legacy MLATT to R2018");
            auto modern = read(upgraded.bytes);
            check(modern.reference_issues().empty() && attributes(modern).size() == attributes(source).size(),
                  "merged graph and attribute count");
            for (const auto &old : attributes(source)) {
                auto all = attributes(modern);
                auto found = std::find_if(all.begin(), all.end(),
                                          [&](const auto &a) { return a.handle() == old.handle(); });
                check(found != all.end(), "merged original identity");
                const auto a = old.attribute_definition(), b = found->attribute_definition();
                check(a.tag == b.tag && a.text.text == b.text.text && a.multiline == b.multiline &&
                          a.text.insertion == b.text.insertion,
                      "merged tag text multiline and insertion");
                if (a.multiline)
                    check(b.embedded_mtext && b.embedded_mtext->text == a.embedded_mtext->text &&
                              b.embedded_mtext->height == a.embedded_mtext->height,
                          "merged MTEXT payload");
            }
            check(modern.next_handle() == got.next_handle(), "merge allocates no handles");
            write(modern, prefix + "-" + std::string(dxf_version_string(version)) + "-up.dxf");
        }
        check(!source.export_version(DxfVersion::AC1012, binary, allow).report.can_write &&
                  !source.export_version(DxfVersion::AC1014, binary, allow).report.can_write,
              "R13/R14 unsupported split rejected");
        // Same-version R2018 keeps embedded MTEXT and allocates nothing.
        auto same = source.export_version(DxfVersion::AC1032, binary);
        check(same.report.can_write && read(same.bytes).next_handle() == source.next_handle(),
              "R2018 preservation");
        for (int fault = 0; fault < 3; ++fault) {
            auto bad = source;
            std::string owner;
            bad.for_each<DxfAttribute>([&](auto &a) {
                if (a.attribute_definition().multiline)
                    owner = a.handle();
            });
            if (fault == 0)
                bad.add_xrecord(bad.extension_dictionary_handle(owner), "ACAD_MLATT", {{1, "existing"}});
            if (fault == 1)
                bad.for_each<DxfAttribute>([](auto &a) {
                    auto v = a.attribute_definition();
                    if (!v.multiline) {
                        v.tag = "NOTE_001";
                        a.set_attribute_definition(v);
                    }
                });
            if (fault == 2) {
                auto data = records(save(bad), binary);
                for (auto &r : data)
                    for (std::size_t i = 1; i < r.size(); ++i)
                        if (r[i - 1] == Pair{9, "$HANDSEED"})
                            r[i].second = "FFFFFFFFFFFFFFFF";
                bad = read(encode(data, binary));
            }
            const auto before = save(bad);
            std::ostringstream out;
            out << "sentinel";
            bool refused = false;
            try {
                bad.write_version(out, DxfVersion::AC1027, binary, allow);
            } catch (const DxfConversionError &e) {
                refused = std::any_of(e.report().issues.begin(), e.report().issues.end(), [](const auto &i) {
                    return i.code == "multiline-attribute-split-failed" && !i.source_handle.empty();
                });
            }
            check(refused && out.str() == "sentinel" && save(bad) == before,
                  "conflict/exhaustion refusal is atomic and identified");
        }
        // Upgrades must never delete foreign lines or application data.
        const auto legacy_result = source.export_version(DxfVersion::AC1027, binary, allow);
        const auto legacy = read(legacy_result.bytes);
        std::string root;
        legacy.for_each<DxfAttribute>([&](const auto &a) {
            if (a.attribute_definition().tag == "NOTE_001")
                root = a.handle();
        });
        const auto dict = legacy.extension_dictionary_handle(root),
                   ml = legacy.dictionary_entry(dict, "ACAD_MLATT");
        for (int fault = 0; fault < 4; ++fault) {
            auto data = records(save(legacy), binary);
            auto &r = at(data, ml);
            auto ref = std::find_if(r.begin(), r.end(), [](const auto &p) { return p.first == 340; });
            check(ref != r.end(), "MLATT other line reference");
            const auto other = ref->second;
            if (fault == 0)
                ref->second = root;
            if (fault == 1)
                legacy.for_each<DxfLine>([&](const auto &e) { ref->second = e.handle(); });
            if (fault == 2) {
                int n = 0;
                for (auto &p : r)
                    if (p.first == 70 && ++n == 3)
                        p.second = binary ? detail::binary_short(99) : "99";
            }
            auto bad = read(encode(data, binary));
            if (fault == 3)
                bad.add_xrecord(bad.extension_dictionary_handle(other), "KeepOther", {{1, "do not delete"}});
            const auto before = save(bad);
            auto result = bad.export_version(DxfVersion::AC1032, binary, allow);
            check(!result.report.can_write && result.bytes.empty() && save(bad) == before,
                  "malformed MLATT/foreign data refuses atomically");
        }
        // Empty stored MTEXT text reconstructs the ordered line values, as upstream.
        auto empty = records(save(legacy), binary);
        auto &payload = at(empty, ml);
        bool embedded = false;
        payload.erase(std::remove_if(payload.begin(), payload.end(),
                                     [&](const auto &p) {
                                         if (p.first == 1 && p.second == "Embedded Object") {
                                             embedded = true;
                                             return false;
                                         }
                                         return embedded && (p.first == 1 || p.first == 3);
                                     }),
                      payload.end());
        auto restored = read(encode(empty, binary)).export_version(DxfVersion::AC1032, binary, allow);
        check(restored.report.can_write, "missing MLATT text falls back to lines");
        auto restored_doc = read(restored.bytes);
        bool found = false;
        restored_doc.for_each<DxfAttribute>([&](const auto &a) {
            if (a.handle() == root) {
                check(a.attribute_definition().text.text == "定义甲\\P\\P定义乙",
                      "fallback preserves empty paragraphs");
                found = true;
            }
        });
        check(found, "fallback identity retained");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
