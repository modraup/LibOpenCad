// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
namespace {
ProxyDefinition proxy(const DxfDocument &d, std::string_view h) {
    ProxyDefinition p;
    d.for_each<DxfProxyEntity>([&](const auto &e) {
        if (e.handle() == h)
            p = e.definition();
    });
    return p;
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3];
        auto h = d.model_space_entity_handles()[0];
        auto p = proxy(d, h);
        check(p.payload.bit_count == 10 && p.payload.data == std::vector<std::uint8_t>({0xab, 0xc0}) &&
                  p.text_payload.data == std::vector<std::uint8_t>({0xe8}),
              "split proxy bits");
        auto graphics = ProxyGraphics::decode(p.graphics);
        check(graphics.records.size() == 27 && graphics.records[1].unicode_text().text == "代理中文😀",
              "proxy graphics decode");
        d.for_each<DxfProxyEntity>([](auto &e) { e.set_definition(e.definition()); });
        d.for_each<DxfProxyObject>([](auto &o) { o.set_definition(o.definition()); });
        write(d, prefix + "-roundtrip.dxf");
        for (std::size_t n = 3; n < graphics.records.size(); ++n) {
            const auto &r = graphics.records[n];
            auto command = r.command();
            check(command.has_value(), "typed proxy command");
            auto encoded = ProxyGraphicRecord::from_command(*command);
            check(encoded.type == r.type && encoded.data == r.data, "proxy command byte parity");
        }
        auto text = graphics.records[1].unicode_text();
        text.text = "修改代理😀";
        text.height = 4;
        graphics.records[1] = ProxyGraphicRecord::text(text);
        p.graphics = graphics.encode();
        p.payload = {9, {0x55, 0xff}};
        d.for_each<DxfProxyEntity>([&](auto &e) {
            if (e.handle() == h)
                e.set_definition(p);
        });
        write(d, prefix + "-edited.dxf");
        auto copy = hex(d.copy_entity(h, EntitySpace::Paper));
        write(d, prefix + "-copied.dxf");
        check(d.remove_entity(copy), "proxy cascade");
        write(d, prefix + "-deleted.dxf");
        auto fresh = DxfDocument::create(binary);
        ClassDefinition c;
        c.name = "CUSTOM_CPP";
        c.cpp_name = "CppProxy";
        c.is_entity = true;
        c.proxy_flags = 127;
        fresh.set_class(c);
        fresh.add_proxy_entity(p);
        auto root = fresh.root_dictionary_handle();
        p.proxy_id = 499;
        p.graphics.clear();
        fresh.add_proxy_object(root, "对象", p);
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "proxy graph");
        auto saved = save(fresh);
        bool rejected = false;
        try {
            p.payload.bit_count = 1000000;
            fresh.add_proxy_object(root, "坏对象", p);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && save(fresh) == saved, "bounded proxy payload validation");
        auto bad = graphics.encode();
        bad[8] = 7;
        rejected = false;
        try {
            ProxyGraphics::decode(bad);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "invalid record length");
        bad = graphics.encode();
        bad[4] = 255;
        rejected = false;
        try {
            ProxyGraphics::decode(bad);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "untrusted record count");
        check(ProxyGraphics::decode(graphics.encode()).records[2].data ==
                  std::vector<std::uint8_t>({1, 2, 3, 4}),
              "unknown packet preserved");
        auto xdoc = d;
        std::vector<std::string> objects;
        xdoc.for_each<DxfProxyObject>([&](const auto &o) { objects.push_back(o.handle()); });
        for (auto id : objects)
            xdoc.set_extended_data(id, {"ProxyData", {{1000, std::string("保留扩展")}}});
        xdoc.for_each<DxfProxyObject>([](auto &o) {
            auto before = o.extended_data();
            o.set_definition(o.definition());
            check(o.extended_data().size() == before.size(), "proxy setter keeps XDATA");
        });
        auto again = read(save(xdoc));
        for (auto id : objects)
            check(again.get_record(id)->extended_data().size() == 1, "proxy XDATA readback");
        ProxyGraphicRecord invalid;
        invalid.type = 6;
        invalid.data = {255, 255, 255, 255};
        rejected = false;
        try {
            invalid.command();
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "bounded typed proxy point count");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
