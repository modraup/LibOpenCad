// SPDX-License-Identifier: MPL-2.0
#include <libopencad/compound_file.hpp>
#include <libopencad/count.hpp>
#include <libopencad/dxf_document.hpp>
#include <libopencad/fields.hpp>
#include <libopencad/nested_copy.hpp>
#include <libopencad/notification.hpp>
#include <libopencad/vba.hpp>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

static std::string hex(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << value;
    return out.str();
}

using namespace libopencad;

static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}

struct TestContext : FieldContext {
    double now_julian() const override { return 2460000.5; }
    std::optional<std::string> login() const override { return std::string("builder"); }
};

int main(int argc, char **argv) {
    try {
        check(argc == 2, "output prefix");
        std::string prefix = argv[1];

        auto insert = nested_copy_destinations({"X|Detail", "x$0$detail"}, NestedCopyMode::Insert);
        auto bind = nested_copy_destinations({"X|Detail", "x$0$detail"}, NestedCopyMode::Bind);
        check(insert["X|DETAIL"] == "Detail", "insert destination");
        check(bind["X|DETAIL"] == "X$1$Detail", "bind collision");

        auto drawing = DxfDocument::create();
        LineTypeDefinition dash;
        dash.name = "X|Dash";
        dash.elements = {{0.5, std::nullopt}, {-0.25, std::nullopt}};
        dash.pattern_length = 0.75;
        dash.xref_dependent = true;
        drawing.add_linetype(dash);
        drawing.add_layer("X|Detail", 7, "X|Dash");
        EntityOptions line_opt;
        line_opt.layer = "X|Detail";
        line_opt.linetype = "X|Dash";
        auto line = drawing.add_line({0, 0, 0}, {1, 0, 0}, line_opt);
        auto names = drawing.nested_copy_symbol_names(NestedCopyMode::Insert);
        check(drawing.localize_nested_copy_symbols({line}, names) == 0, "localized");
        bool localized = false;
        drawing.for_each<DxfLine>([&](const DxfLine &entity) {
            if (entity.handle() == hex(line)) {
                check(entity.layer() == "Detail", "localized layer");
                check(entity.linetype() == "Dash", "localized linetype");
                localized = true;
            }
        });
        check(localized && drawing.get_layer("Detail") && drawing.get_linetype("Dash"), "copied symbols");

        drawing.add_symbol(SymbolTable::BlockRecord, "Bolt");
        InsertDefinition ins;
        ins.block_name = "Bolt";
        ins.insertion = {1, 2, 0};
        auto a = drawing.add_insert(ins);
        auto b = drawing.add_insert(ins);
        ins.insertion = {4, 5, 0};
        auto c = drawing.add_insert(ins);
        (void)a;
        (void)b;
        (void)c;
        auto instances = block_instances(drawing);
        check(instances.size() == 3, "three inserts");
        check(count_of(instances, "Bolt", CountKey{}) == 2, "unique count");
        check(evaluate_count(drawing, "\\AcCount {\"name\":\"Bolt\",\"type\":\"block\"}") == "2", "count json");

        NotificationCollection notes;
        notes.notify(NotificationType::Warning, "w1");
        notes.notify(NotificationType::Error, "e1");
        check(notes.size() == 2 && notes.has_type(NotificationType::Error), "notifications");
        check(notes.items()[0].to_string() == "[Warning] w1", "display");

        auto field_doc = DxfDocument::create();
        MTextDefinition mt;
        mt.text = "DIESEL";
        mt.insertion = {0, 0, 0};
        mt.height = 2.5;
        auto host = field_doc.add_mtext(mt);
        auto dict = field_doc.add_extension_dictionary(hex(host));
        FieldDefinition diesel;
        diesel.evaluator_id = "AcDiesel";
        diesel.code = "\\AcDiesel $(+,1,2)";
        diesel.value.type = 4;
        diesel.value.text = "3";
        FieldDefinition container;
        container.evaluator_id = "_text";
        container.code = "%<\\_FldIdx 0>%";
        auto parent = field_doc.add_field(hex(dict), "_text", container);
        field_doc.add_field_child(hex(parent), diesel);
        TestContext ctx;
        auto shown = resolve_field(field_doc, host, ctx);
        check(shown == std::optional<std::string>{"3"}, "diesel field");
        FieldDefinition login;
        login.evaluator_id = "AcVar";
        login.code = "\\AcVar Login";
        auto login_handle = field_doc.add_field_child(hex(parent), login);
        check(resolve_field_handle(field_doc, login_handle, host, ctx) == std::optional<std::string>{"builder"},
              "login field");

        CompoundFile empty;
        auto cfb = empty.encode();
        auto decoded = CompoundFile::decode(cfb.data(), cfb.size());
        check(decoded && decoded->first.root.name == "Root Entry", "empty CFB");
        auto payload = StructuredStoragePayload::decode(cfb.data(), cfb.size());
        check(payload.compound_file.has_value(), "structured storage");
        std::vector<std::uint8_t> vba_src{'H', 'i'};
        auto compressed = vba_compress_container(vba_src.data(), vba_src.size());
        auto plain = vba_decompress_container(compressed.data(), compressed.size());
        check(plain && *plain == vba_src, "vba container");

        std::ofstream dxf(prefix + "-optional.dxf", std::ios::binary);
        drawing.write(dxf);
        std::ofstream fields_out(prefix + "-fields.dxf", std::ios::binary);
        field_doc.write(fields_out);
        std::ofstream cfb_out(prefix + "-empty.cfb", std::ios::binary);
        cfb_out.write(reinterpret_cast<const char *>(cfb.data()), static_cast<std::streamsize>(cfb.size()));
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
