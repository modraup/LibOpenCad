// SPDX-License-Identifier: MPL-2.0
// Decompose pre-R2007 columns using acadrust's explode/text.rs layout.
void split_mtext_columns() {
    if (target() >= V::AC1021)
        return;
    std::vector<DxfMText> sources;
    doc_.for_each<DxfMText>([&](const auto &e) {
        if (e.definition().columns.type)
            sources.push_back(e);
    });
    for (auto source : sources) {
        try {
            if (source.handle().empty() || !doc_.get_record(source.handle()) ||
                !doc_.block_record(source.owner_handle()))
                throw std::invalid_argument("MTEXT columns require unique identity and a BLOCK_RECORD owner");
            const auto value = source.definition();
            auto pieces = detail::approximate_mtext(value, options_.max_output_bytes / 32);
            // Keep identity (and external references) even for wholly blank text.
            if (pieces.empty()) {
                detail::ExplodedText blank;
                blank.text.insertion = value.insertion;
                blank.text.normal = value.normal;
                blank.text.height = value.height;
                blank.text.style = value.style;
                pieces.push_back(blank);
            }
            issue(Kind::Loss, "mtext-columns-decomposed", &source, 0,
                  "Columns replaced by TEXT using upstream approximate font metrics and balanced line "
                  "distribution; wrapping, inline decoration, column heights and background are not exact");
            auto plain = value;
            plain.columns = {};
            source.set_definition(plain);
            DxfText base;
            static_cast<DxfEntity &>(base) = source;
            base.pairs_.clear();
            bool body = false, tail = false;
            unsigned depth = 0;
            for (const auto &p : source.pairs_) {
                if (p.code == 100 && p.value == "AcDbMText" && !depth) {
                    body = true;
                    base.pairs_.push_back({100, "AcDbText"});
                    continue;
                }
                if (body && p.code >= 1000 && !depth)
                    tail = true;
                if (!body || tail)
                    base.pairs_.push_back(p);
                else {
                    if (p.code == 102 || p.code == 101 || p.code == 100 ||
                        !code_in(p.code,
                                 {1,  3,  7,  10, 20, 30, 11, 21, 31, 40, 41, 42,  43,  44,  45,  46, 48,
                                  49, 50, 63, 71, 72, 73, 75, 76, 78, 79, 90, 210, 220, 230, 421, 441}))
                        throw std::invalid_argument("Unreviewed MTEXT body extension cannot be decomposed");
                }
                if (p.code == 102) {
                    if (!p.value.empty() && p.value.front() == '{')
                        ++depth;
                    else if (p.value == "}" && depth)
                        --depth;
                }
            }
            if (!body)
                throw std::invalid_argument("Missing AcDbMText subclass");
            base.pairs_.front().value = "TEXT";
            std::vector<DxfDocument::Record> replacement;
            for (std::size_t i = 0; i < pieces.size(); ++i) {
                auto text = base;
                if (i) {
                    // Only the first piece owns the original dictionary and reactors.
                    std::vector<DxfRecord::Pair> clean;
                    unsigned nesting = 0;
                    for (const auto &p : text.pairs_) {
                        if (p.code == 102) {
                            if (!p.value.empty() && p.value.front() == '{')
                                ++nesting;
                            else if (p.value == "}" && nesting)
                                --nesting;
                            continue;
                        }
                        if (!nesting)
                            clean.push_back(p);
                    }
                    text.pairs_ = std::move(clean);
                    text.set_field("", 5, attribute_hex(doc_.allocate_handle()));
                }
                auto definition = pieces[i].text;
                if (!doc_.get_symbol(SymbolTable::TextStyle, definition.style)) {
                    issue(Kind::Loss, "mtext-font-fallback", &source, 7,
                          "Unregistered inline font replaced by the source text style");
                    definition.style = value.style;
                }
                text.set_definition(definition);
                if (pieces[i].color)
                    text.set_color(*pieces[i].color);
                replacement.emplace_back(std::move(text));
            }
            const auto pos = doc_.handle_index_.at(std::stoull(source.handle(), nullptr, 16)).front().record;
            doc_.records_.erase(doc_.records_.begin() + pos);
            doc_.records_.insert(doc_.records_.begin() + pos, replacement.begin(), replacement.end());
            doc_.rebuild_indexes();
            report_.changed_records += replacement.size();
        } catch (const std::exception &e) {
            issue(Kind::Error, "mtext-column-decomposition-failed", &source, 0, e.what());
        }
    }
}
