// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace libopencad;
namespace {
constexpr const char *kSource = R"(A\H2;B\H2x;C\H2X;D\\H2;E\H 2.5 ;F\H2e1;G\P中\H3;)";
constexpr const char *kScaled = R"(A\H4;B\H2x;C\H2X;D\\H2;E\H5;F\H40;G\P中\H6;)";
void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
void write_file(const std::string &path, const DxfDocument &d) {
    std::ofstream out(path, std::ios::binary);
    d.write(out);
    out.close();
    check(bool(out), "write failed");
}
DxfDocument read_bytes(const std::string &s) {
    std::istringstream in(s);
    return DxfDocument::read(in);
}
void scale(DxfDocument &d) {
    const auto t = Transform::from_scale(2);
    d.for_each<DxfMText>([&](auto &e) { e.apply_transform(t); });
    d.for_each<DxfMultiLeader>([&](auto &e) { e.apply_transform(t); });
}
void expect_texts(const DxfDocument &d, const char *text, bool leaders) {
    int mtext = 0, mleader = 0;
    d.for_each<DxfMText>([&](const auto &e) {
        check(e.definition().text == text, "MTEXT inline heights");
        check(e.definition().height == 2, "MTEXT entity height");
        ++mtext;
    });
    d.for_each<DxfMultiLeader>([&](const auto &e) {
        check(e.definition().context.text_string == text, "MULTILEADER inline heights");
        check(e.definition().context.text_height == 0.36, "MULTILEADER text height");
        ++mleader;
    });
    check(mtext == 1, "MTEXT count");
    check(mleader == (leaders ? 1 : 0), "MULTILEADER count");
}
DxfDocument with_mtext(bool binary) {
    auto d = DxfDocument::create(binary);
    MTextDefinition t;
    t.text = kSource;
    t.height = 1;
    t.rectangle_width = 20;
    t.rectangle_height = 4;
    d.add_mtext(t);
    return d;
}
DxfDocument with_mtext_and_leader(bool binary) {
    auto d = with_mtext(binary);
    MultiLeaderDefinition v;
    v.context.has_text_contents = true;
    v.context.text_string = kSource;
    v.context.text_height = 0.18;
    d.add_multileader(v);
    return d;
}
void run_drawing(DxfDocument d, const std::string &prefix, bool leaders) {
    write_file(prefix + "-source.dxf", d);
    {
        auto identity = d;
        identity.for_each<DxfMText>([](auto &e) { e.apply_transform(Transform::identity()); });
        identity.for_each<DxfMultiLeader>([](auto &e) { e.apply_transform(Transform::identity()); });
        identity.for_each<DxfMText>([](const auto &e) { check(e.definition().text == kSource, "identity MTEXT"); });
        if (leaders)
            identity.for_each<DxfMultiLeader>(
                [](const auto &e) { check(e.definition().context.text_string == kSource, "identity MULTILEADER"); });
    }
    {
        auto bad = d;
        const auto before = bytes(bad);
        Transform t;
        t.matrix.m[0][0] = std::numeric_limits<double>::quiet_NaN();
        bool refused = false;
        try {
            bad.for_each<DxfMText>([&](auto &e) { e.apply_transform(t); });
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused && bytes(bad) == before, "nonfinite MTEXT transform rolled back");
        if (leaders) {
            refused = false;
            try {
                bad.for_each<DxfMultiLeader>([&](auto &e) { e.apply_transform(t); });
            } catch (const std::invalid_argument &) {
                refused = true;
            }
            check(refused && bytes(bad) == before, "nonfinite MULTILEADER transform rolled back");
        }
    }
    scale(d);
    expect_texts(d, kScaled, leaders);
    write_file(prefix + "-scaled.dxf", d);
    expect_texts(read_bytes(bytes(d)), kScaled, leaders);
}
} // namespace
int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    const std::string prefix = argv[1];
    run_drawing(with_mtext_and_leader(false), prefix + "-ascii", true);
    run_drawing(with_mtext_and_leader(true), prefix + "-binary", true);
    auto modern = with_mtext(false);
    auto exported = modern.export_version(DxfVersion::AC1018, false, {true});
    check(exported.report.can_write && !exported.bytes.empty(), "AC1018 MTEXT export");
    auto old = read_bytes(exported.bytes);
    check(old.version() == DxfVersion::AC1018, "AC1018 version");
    run_drawing(std::move(old), prefix + "-ascii-ac1018", false);
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
