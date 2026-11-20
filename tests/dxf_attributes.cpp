// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>
namespace {
AttributeDefinition definition(bool multiline) {
    AttributeDefinition a;
    a.tag = "标签";
    a.prompt = "输入";
    a.text.text = "默认";
    a.text.insertion = {1, 2, 3};
    a.text.alignment_point = Point3{4, 5, 6};
    a.text.height = 3;
    a.text.thickness = 2;
    a.text.rotation = .5;
    a.text.width_factor = 1.5;
    a.text.oblique_angle = -.25;
    a.text.generation_flags = 6;
    a.text.horizontal_alignment = 2;
    a.text.vertical_alignment = 3;
    a.flags = 12;
    a.field_length = 12;
    a.lock_position = true;
    if (multiline) {
        a.multiline = true;
        MTextDefinition m;
        m.text = "多行\\P默认";
        m.insertion = {7, 8, 9};
        m.height = 4;
        m.rectangle_width = 20;
        m.rotation = .25;
        m.line_spacing_factor = 1.5;
        a.embedded_mtext = m;
    }
    return a;
}
void check_attribute(const DxfAttribute &e, bool multi) {
    auto a = e.attribute_definition();
    check(a.tag == "标签" && a.flags == 12 && a.field_length == 12 && a.lock_position, "attribute flags");
    check(a.text.insertion == Point3{1, 2, 3} && a.text.alignment_point == Point3{4, 5, 6} &&
              a.text.height == 3 && a.text.thickness == 2 && std::abs(a.text.rotation - .5) < 1e-12 &&
              a.text.width_factor == 1.5 && std::abs(a.text.oblique_angle + .25) < 1e-12,
          "attribute geometry");
    check(a.text.generation_flags == 6 && a.text.horizontal_alignment == 2 &&
              a.text.vertical_alignment == 3 && a.multiline == multi,
          "attribute subclass scopes");
    check(a.text.text == (multi ? "多行\\P默认" : "默认"), "attribute text");
    if (multi) {
        const auto &m = *a.embedded_mtext;
        check(m.insertion == Point3{7, 8, 9} && m.height == 4 && m.rectangle_width == 20 &&
                  std::abs(m.rotation - .25) < 1e-12 && m.line_spacing_factor == 1.5,
              "embedded MTEXT");
    }
}
void verify(const DxfDocument &d) {
    int defs = 0, inserts = 0;
    d.for_each<DxfAttribute>([&](const auto &e) {
        check_attribute(e, defs++ == 1);
        check(e.attribute_definition().prompt == "输入", "attribute prompt");
    });
    d.for_each<DxfInsert>([&](const auto &e) {
        ++inserts;
        check(e.attribute_count() == 2 && e.seqend(), "attribute sequence");
        for (std::size_t i = 0; i < 2; ++i) {
            auto a = e.attribute(i);
            check_attribute(a, i == 1);
            check(a.owner_handle() == e.handle(), "attribute parent");
        }
    });
    check(defs == 2 && inserts == 2, "attribute inventory");
}
template <class F> void edit(DxfDocument &d, const std::string &h, F f) {
    int count = 0;
    d.for_each<DxfInsert>([&](auto &e) {
        if (e.handle() == h) {
            f(e);
            ++count;
        }
    });
    check(count == 1, "insert lookup");
}
template <class F> void failure(DxfDocument &d, F f) {
    auto before = save(d);
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed && save(d) == before, "atomic attribute failure");
}
void unchanged(const Records &before, Records after, const std::set<std::string> &excluded = {}) {
    for (const auto &r : before) {
        auto h = identity(r);
        if (!h.empty() && !excluded.count(h))
            check(at(after, h) == r, "neighbor attribute record changed");
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        made.add_symbol(SymbolTable::BlockRecord, "Attributes");
        EntityOptions o;
        o.block_name = "Attributes";
        for (bool multi : {false, true})
            made.add_attribute_definition(definition(multi), o);
        InsertDefinition i;
        i.block_name = "Attributes";
        i.insertion = {10, 20, 30};
        made.add_insert(i, {definition(false), definition(true)});
        o.block_name.clear();
        o.space = EntitySpace::Paper;
        made.add_insert(i, {definition(false), definition(true)}, o);
        verify(made);
        write(made, prefix + "-created.dxf");
        std::ifstream input(argv[2], std::ios::binary);
        auto d = DxfDocument::read(input);
        verify(d);
        auto model = d.model_space_entity_handles().front();
        std::vector<std::string> children;
        std::string seqend;
        edit(d, model, [&](auto &e) {
            for (std::size_t i = 0; i < e.attribute_count(); ++i)
                children.push_back(e.attribute(i).handle());
            seqend = e.seqend()->handle();
        });
        std::set<std::string> removed{model, seqend};
        for (const auto &h : children) {
            auto dict = hex(d.add_extension_dictionary(h));
            auto xr = hex(d.add_xrecord(dict, "Data", {{340, h}, {341, model}}));
            removed.insert(h);
            removed.insert(dict);
            removed.insert(xr);
        }
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        failure(d, [&] {
            edit(d, model, [](auto &e) {
                auto a = e.attribute(0);
                auto v = a.attribute_definition();
                v.text.height = std::numeric_limits<double>::infinity();
                a.set_attribute_definition(v);
                e.set_attribute(0, a);
            });
        });
        edit(d, model, [](auto &e) {
            auto a = e.attribute(0);
            auto v = a.attribute_definition();
            v.text.text = "修改";
            v.text.insertion = {101, 102, 103};
            v.text.generation_flags = 2;
            v.text.horizontal_alignment = 1;
            v.lock_position = false;
            a.set_attribute_definition(v);
            e.set_attribute(0, a);
            a = e.attribute(1);
            v = a.attribute_definition();
            v.text.text = "修改\\P多行";
            v.embedded_mtext->text = v.text.text;
            v.embedded_mtext->rectangle_width = 25;
            a.set_attribute_definition(v);
            e.set_attribute(1, a);
        });
        auto after = records(save(d), binary);
        unchanged(before, after, {children.begin(), children.end()});
        write(d, prefix + "-edited.dxf");
        auto copy = hex(d.copy_entity(model, EntitySpace::Paper));
        unchanged(after, records(save(d), binary));
        write(d, prefix + "-copied.dxf");
        check(d.remove_entity(copy) && d.remove_entity(model), "attribute group deletion");
        unchanged(before, records(save(d), binary), removed);
        for (auto &h : removed)
            check(!d.get_record(h), "attribute cascade index");
        check(d.reference_issues().empty(), "attribute graph");
        write(d, prefix + "-deleted.dxf");
        auto damaged = before;
        drop(damaged, {seqend});
        auto incomplete = read(encode(damaged, binary));
        failure(incomplete, [&] { incomplete.copy_entity(model); });
        auto wrong = before;
        for (auto &p : at(wrong, children[0]))
            if (p.first == 330)
                p.second = "FFFF";
        auto foreign = read(encode(wrong, binary));
        failure(foreign, [&] { foreign.remove_entity(model); });
        // A flat single-line attribute's 71 is generation flags, never an MTEXT flag.
        auto flat = before;
        auto &child = at(flat, children[0]);
        child.erase(std::remove_if(child.begin(), child.end(), [](auto &p) { return p.first == 100; }),
                    child.end());
        auto legacy = read(encode(flat, binary));
        edit(legacy, model, [](auto &e) {
            auto a = e.attribute(0);
            auto v = a.attribute_definition();
            check(!v.multiline && v.text.generation_flags == 6, "flat attribute generation flags");
            v.text.text = "flat";
            a.set_attribute_definition(v);
            e.set_attribute(0, a);
            check(e.attribute(0).attribute_definition().text.generation_flags == 6,
                  "flat attribute edit preserves generation flags");
        });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
