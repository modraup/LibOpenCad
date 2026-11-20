// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
int main(int argc, char**argv) try {
    check(argc == 2, "output prefix");
    for(bool binary : {false,true}) {
        auto d = DxfDocument::create(binary);
        d.add_wipeout(RasterImageDefinition{});
        auto wipeout_record = [&] { for (auto r : records(save(d), binary)) if (!r.empty() && r[0] == Pair{0,"WIPEOUT"}) return r; throw std::runtime_error("Missing wipeout"); };
        auto original = wipeout_record();
        check(d.wipeout_frame() == 1, "default frame");
        d.set_wipeout_frame(0);
        auto vars = d.dictionary_entry(d.root_dictionary_handle(), "ACAD_WIPEOUT_VARS");
        check(d.wipeout_frame() == 0 && !vars.empty(), "create hidden frame");
        d.set_wipeout_frame(2);
        check(d.dictionary_entry(d.root_dictionary_handle(), "ACAD_WIPEOUT_VARS") == vars, "reuse variables");
        check(wipeout_record() == original, "frame setting changed wipeout entity");
        auto loaded = read(save(d));
        check(loaded.wipeout_frame() == 2, "frame roundtrip");
        write(d,std::string(argv[1])+(binary ? "-binary.dxf" : "-ascii.dxf"));
    }
} catch(const std::exception&e) {std::cerr<<e.what()<<'\n';return 1;}
