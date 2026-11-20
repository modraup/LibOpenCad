// SPDX-License-Identifier: MPL-2.0
#include <libopencad/plot_style.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace libopencad;

static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}

static void write_bytes(const std::string &path, const std::vector<std::uint8_t> &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

int main(int argc, char **argv) {
    try {
        check(argc == 2, "output prefix");
        std::string prefix = argv[1];

        auto scaled = PlotStyleTable::identity("scaled.ctb");
        scaled.apply_factor = true;
        scaled.scale_factor = 0.25f;
        scaled.aci_entries[1].lineweight = 13;
        auto reloaded = PlotStyleTable::from_bytes(scaled.name, scaled.to_bytes());
        check(reloaded.scale_factor == 0.25f, "scale factor");
        auto lw = reloaded.resolve_lineweight(1);
        check(lw && std::abs(*lw - 0.125f) < 1e-6f, "scaled lineweight");
        check(scaled.to_text().find("\nscale_factor=1.0\n") == std::string::npos, "scaled not 1.0");
        check(PlotStyleTable::identity("plain.ctb").to_text().find("\nscale_factor=1.0\n") != std::string::npos,
              "whole scale factor");

        auto pens = PlotStyleTable::identity("pens.ctb");
        pens.lineweights[1] = 0.035f;
        pens.aci_entries[1].lineweight = 1;
        auto pens_again = PlotStyleTable::from_bytes(pens.name, pens.to_bytes());
        check(pens_again.lineweights[1] == 0.035f, "custom lineweight");

        PlotStyleTable stb;
        stb.name = "named.stb";
        stb.is_stb = true;
        stb.lineweights.assign(kPlotStyleLineweights.begin(), kPlotStyleLineweights.end());
        PlotStyleEntry normal;
        normal.name = "Normal";
        PlotStyleEntry black;
        black.name = "Black";
        black.color = std::array<std::uint8_t, 3>{0, 0, 0};
        black.lineweight = 8;
        black.color_policy = 5;
        stb.named_entries["Normal"] = normal;
        stb.named_entries["Black"] = black;
        auto stb_bytes = stb.to_bytes();
        check(stb_bytes.size() >= 26 && std::string(stb_bytes.begin() + 19, stb_bytes.begin() + 26) == "STBVER1",
              "STBVER1 header");
        auto stb_loaded = PlotStyleTable::from_bytes("named.stb", stb_bytes);
        check(stb_loaded.is_stb, "stb name");
        auto black_it = stb_loaded.named_entries.find("Black");
        check(black_it != stb_loaded.named_entries.end(), "Black style");
        check(black_it->second.color.has_value() && *black_it->second.color == (std::array<std::uint8_t, 3>{0, 0, 0}),
              "Black RGB");

        auto ttb = stb_bytes;
        ttb[19] = 'T';
        ttb[20] = 'T';
        ttb[21] = 'B';
        auto ttb_loaded = PlotStyleTable::from_bytes("named.stb", ttb);
        check(ttb_loaded.is_stb && ttb_loaded.named_entries.count("Black"), "TTBVER1");

        auto damaged = scaled.to_bytes();
        damaged[48] ^= 0xFF;
        auto warned = PlotStyleTable::from_bytes("foreign.ctb", damaged);
        check(!warned.load_warnings.empty(), "checksum warning");

        std::string greek = "Ελληνικά";
        auto decoded = decode_plot_style_text(std::vector<std::uint8_t>(greek.begin(), greek.end()));
        check(decoded.first == greek && !decoded.second, "utf-8 description");

        write_bytes(prefix + "-scaled.ctb", scaled.to_bytes());
        write_bytes(prefix + "-named.stb", stb_bytes);
        write_bytes(prefix + "-ttb.stb", ttb);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
