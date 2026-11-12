// SPDX-License-Identifier: MPL-2.0
// Included in dxf_document.cpp; common document operations ported from acadrust
// tables/{mod,textstyle,dimstyle,appid,ucs,view,vport,block_record}.rs and document.rs.
namespace {
const char* symbol_type(SymbolTable table) {
    switch(table) {
        case SymbolTable::Layer:return "LAYER"; case SymbolTable::LineType:return "LTYPE";
        case SymbolTable::TextStyle:return "STYLE"; case SymbolTable::DimStyle:return "DIMSTYLE";
        case SymbolTable::AppId:return "APPID"; case SymbolTable::Ucs:return "UCS";
        case SymbolTable::View:return "VIEW"; case SymbolTable::VPort:return "VPORT";
        case SymbolTable::BlockRecord:return "BLOCK_RECORD";
    } throw std::invalid_argument("Unknown symbol table");
}
const char* symbol_subclass(SymbolTable table) {
    switch(table) {
        case SymbolTable::Layer:return "AcDbLayerTableRecord"; case SymbolTable::LineType:return "AcDbLinetypeTableRecord";
        case SymbolTable::TextStyle:return "AcDbTextStyleTableRecord"; case SymbolTable::DimStyle:return "AcDbDimStyleTableRecord";
        case SymbolTable::AppId:return "AcDbRegAppTableRecord"; case SymbolTable::Ucs:return "AcDbUCSTableRecord";
        case SymbolTable::View:return "AcDbViewTableRecord"; case SymbolTable::VPort:return "AcDbViewportTableRecord";
        case SymbolTable::BlockRecord:return "AcDbBlockTableRecord";
    } throw std::invalid_argument("Unknown symbol table");
}
void valid_symbol_name(std::string_view name) {
    if(name.empty() || name.find_first_of("<>/\\\":;?*|,=`")!=name.npos ||
       std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;}))
        throw std::invalid_argument("Invalid symbol name");
    const auto key=detail::symbol_name_key(name); // Also validate UTF-8.
    if(std::any_of(key.begin(),key.end(),[](char32_t c){return c<32 || (c>=127 && c<=159);}))
        throw std::invalid_argument("Control character in symbol name");
}
bool protected_symbol(SymbolTable table,std::string_view name) {
    const auto key=detail::symbol_name_key(name);
    auto matches=[&](std::string_view other){return key==detail::symbol_name_key(other);};
    return name.empty() || name.front()=='*' || name.find('|')!=name.npos ||
        (table==SymbolTable::Layer && (matches("0") || matches("Defpoints"))) ||
        (table==SymbolTable::LineType && (matches("Continuous") || matches("ByLayer") || matches("ByBlock"))) ||
        ((table==SymbolTable::TextStyle || table==SymbolTable::DimStyle) && matches("Standard")) ||
        (table==SymbolTable::AppId && matches("ACAD"));
}
bool pointer_code(int code) {
    return (code>=330 && code<=369) || (code>=390 && code<=399) || code==480 || code==481 || code==1005;
}
}

std::vector<std::size_t> DxfDocument::symbol_locations(SymbolTable table,std::string_view name) const {
    std::vector<std::size_t> result;
    const auto type=symbol_type(table); const auto key=detail::symbol_name_key(name);
    std::string section,current;
    for(std::size_t i=0;i<records_.size();++i) {
        const auto& r=record_at({i,std::nullopt,false});
        if(r.type()=="SECTION") section=r.field("",2);
        if(r.type()=="ENDSEC") section.clear();
        if(section!="TABLES") continue;
        if(r.type()=="TABLE") current=r.field("",2);
        else if(r.type()=="ENDTAB") current.clear();
        else if(current==type && r.type()==type && detail::symbol_name_key(r.text_field(symbol_subclass(table),2))==key) result.push_back(i);
    } return result;
}
std::vector<SymbolEntry> DxfDocument::symbols(SymbolTable table) const {
    std::vector<SymbolEntry> result;
    const auto type=symbol_type(table); std::string section,current;
    for(std::size_t i=0;i<records_.size();++i) {
        const auto& r=record_at({i,std::nullopt,false});
        if(r.type()=="SECTION") section=r.field("",2);
        if(r.type()=="ENDSEC") section.clear();
        if(section!="TABLES") continue;
        if(r.type()=="TABLE") current=r.field("",2);
        else if(r.type()=="ENDTAB") current.clear();
        else if(current==type && r.type()==type) {
            SymbolEntry entry{r.text_field(symbol_subclass(table),2),r.handle(),r.owner_handle(),{}};
            r.locate_if(symbol_subclass(table),[&](std::size_t at,int code){
                if(code!=0 && code!=2 && code!=5 && code!=105 && code!=330 && code!=100) {
                    const auto& raw=r.pairs_[at].value;
                    entry.properties.push_back({code,detail::is_string_value(code)?r.decode_string(raw):
                        (r.binary_?detail::text_value_from_binary(code,raw):std::string(trim(raw)))});
                }return false;
            });result.push_back(std::move(entry));
        }
    }return result;
}
std::optional<SymbolEntry> DxfDocument::get_symbol(SymbolTable table,std::string_view name) const {
    std::optional<SymbolEntry> result;const auto key=detail::symbol_name_key(name);
    for(auto& entry:symbols(table)) if(detail::symbol_name_key(entry.name)==key) result=std::move(entry);
    return result;
}
void DxfDocument::apply_symbol_properties(DxfRecord& record,SymbolTable table,
    const std::vector<SymbolProperty>& properties) const {
    std::unordered_set<int> codes;
    for(const auto& p:properties) {
        if(p.code<=0 || p.code>=1000 || p.code==2 || p.code==5 || p.code==9 || p.code==100 ||
           p.code==101 || p.code==102 || p.code==105 || p.code==330 || !codes.insert(p.code).second)
            throw std::invalid_argument("Reserved or duplicate symbol property");
        // Validate numeric ranges even for ASCII output.
        const auto encoded=detail::binary_value_from_text(p.code,p.value);
        if(pointer_code(p.code) && (!parse_handle(p.value) || (handle_value(p.value) && records_with_handle(p.value).size()!=1)))
            throw std::invalid_argument("Symbol property refers to a missing/ambiguous handle");
        if(table==SymbolTable::Layer && p.code==6 && !get_linetype(p.value))
            throw std::invalid_argument("Layer linetype does not exist");
        if(table==SymbolTable::Layer && p.code==62 && (integer(p.value)==0 || std::abs(integer(p.value))>255))
            throw std::invalid_argument("Invalid layer color index");
        record.set_field(symbol_subclass(table),p.code,detail::is_string_value(p.code)?record.encode_string(p.value):(binary_?encoded:p.value));
    }
}
std::uint64_t DxfDocument::add_symbol(SymbolTable table,std::string name,
    const std::vector<SymbolProperty>& properties) {
    valid_symbol_name(name);
    if(get_symbol(table,name)) throw std::invalid_argument("Symbol name already exists");
    DxfDocument staged=*this;
    if(table==SymbolTable::Layer || table==SymbolTable::LineType) {
        const auto id=table==SymbolTable::Layer?staged.add_layer(name):staged.add_linetype({name,"",{},0,false});
        auto& entry=staged.records_[staged.handle_index_.at(id).front().record];
        std::visit([&](DxfRecord& r){staged.apply_symbol_properties(r,table,properties);},entry);
        *this=std::move(staged);return id;
    }
    auto entry=staged.make_record(symbol_type(table),symbol_subclass(table));
    if(table==SymbolTable::TextStyle || table==SymbolTable::DimStyle || table==SymbolTable::VPort) {
        // Frozen Rust new() defaults, independent of edited or absent defaults in this document.
        const auto defaults=DxfDocument::create(binary_);
        const auto names=table==SymbolTable::VPort?"*Active":"Standard";
        const auto definition=defaults.get_symbol(table,names);
        if(!definition) throw std::logic_error("Missing built-in symbol defaults");
        auto fields=definition->properties;
        if(table==SymbolTable::DimStyle) {
            const auto standard=get_symbol(SymbolTable::TextStyle,"Standard");
            if(!standard) throw std::invalid_argument("Standard text style does not exist");
            for(auto& f:fields) if(f.code==340) f.value=standard->handle;
        }
        staged.apply_symbol_properties(entry,table,fields);
    }
    entry.set_text_field(symbol_subclass(table),2,std::move(name));
    entry.set_short_field(symbol_subclass(table),70,0);
    if(table==SymbolTable::Ucs) {
        entry.set_point(symbol_subclass(table),10,{0,0,0});
        entry.set_point(symbol_subclass(table),11,{1,0,0});
        entry.set_point(symbol_subclass(table),12,{0,1,0});
    } else if(table==SymbolTable::View) {
        entry.set_point(symbol_subclass(table),10,{0,0,0});
        entry.set_point(symbol_subclass(table),11,{0,0,1});
        entry.set_point(symbol_subclass(table),12,{0,0,0});
        entry.set_number(symbol_subclass(table),40,1); entry.set_number(symbol_subclass(table),41,1);
        entry.set_number(symbol_subclass(table),42,50);
    } else if(table==SymbolTable::BlockRecord) {
        if(version_==DxfVersion::AC1009) throw std::invalid_argument("BLOCK_RECORD requires R13 or later");
        entry.set_short_field(symbol_subclass(table),280,1);entry.set_short_field(symbol_subclass(table),281,0);
    }
    staged.apply_symbol_properties(entry,table,properties);
    Record record=entry;
    if(table==SymbolTable::BlockRecord) {DxfBlockRecord block;static_cast<DxfRecord&>(block)=entry;record=std::move(block);}
    const auto handle=staged.insert_table_entry(symbol_type(table),std::move(record));
    if(table==SymbolTable::BlockRecord) {
        auto start=staged.make_entity("BLOCK","AcDbBlockBegin",{});
        auto end=staged.make_entity("ENDBLK","AcDbBlockEnd",{});
        start.set_field("",5,hex_handle(staged.allocate_handle()));end.set_field("",5,hex_handle(staged.allocate_handle()));
        start.set_field("",330,hex_handle(handle));end.set_field("",330,hex_handle(handle));
        const auto blockname=entry.text_field(symbol_subclass(table),2);
        start.set_text_field("AcDbBlockBegin",2,blockname);start.set_text_field("AcDbBlockBegin",3,blockname);
        start.set_short_field("AcDbBlockBegin",70,0);start.set_point("AcDbBlockBegin",10,{0,0,0});
        std::string section;std::optional<std::size_t> position;
        for(std::size_t i=0;i<staged.records_.size();++i) {
            const auto& r=staged.record_at({i,std::nullopt,false});
            if(r.type()=="SECTION") section=r.field("",2);
            if(r.type()=="ENDSEC" && section=="BLOCKS") {if(position) throw std::invalid_argument("Multiple BLOCKS sections");position=i;section.clear();}
        }
        if(!position) throw std::invalid_argument("Missing BLOCKS section");
        std::vector<Record> definitions{static_cast<DxfRecord>(start),static_cast<DxfRecord>(end)};
        staged.records_.insert(staged.records_.begin()+*position,definitions.begin(),definitions.end());
        staged.rebuild_indexes();
    }
    *this=std::move(staged);return handle;
}

// Return semantic name references only; arbitrary text and XRECORD strings are not names.
std::vector<std::size_t> DxfDocument::symbol_references(const DxfRecord& record,SymbolTable table) const {
    std::vector<std::size_t> result;std::string header;bool xdata=false,payload=false,opaque=false;int depth=0;
    const auto type=record.type();const bool head=type=="SECTION" && record.field("",2)=="HEADER";
    // Flat pre-R13 records have no subclass marker. Only graphical sections
    // give their groups 8/6 entity semantics; OBJECTS may store literal strings.
    const bool graphical=[&] {
        if(table!=SymbolTable::Layer && table!=SymbolTable::LineType)return false;
        const int code=table==SymbolTable::Layer?8:6;
        if(!record.locate("AcDbEntity",code).found)return false;
        if(std::any_of(record.pairs_.begin(),record.pairs_.end(),[](const auto& p){return p.code==100;}))return true;
        std::string section;
        for(std::size_t i=0;i<records_.size();++i) {
            const auto& r=record_at({i,std::nullopt,false});
            if(r.type()=="SECTION")section=trim(r.field("",2));
            if(r.type()=="ENDSEC")section.clear();
            if(section!="ENTITIES" && section!="BLOCKS")continue;
            if(&r==&record)return true;
            if(const auto* children=sequence(records_[i])) {
                for(const auto& v:children->vertices_)if(&v==&record)return true;
                if(children->seqend_ && &*children->seqend_==&record)return true;
            }
        }
        return false;
    }();
    // Identify layer-state header names through the LAYER table's dictionary tree.
    bool layer_state=false;
    if(table==SymbolTable::Layer && type=="XRECORD") {
        const auto* states=get_record(record.owner_handle());
        const auto* extension=states?get_record(states->owner_handle()):nullptr;
        const auto* owner=extension?get_record(extension->owner_handle()):nullptr;
        if(states && extension && owner && states->type()=="DICTIONARY" && extension->type()=="DICTIONARY" &&
           owner->type()=="TABLE" && owner->field("",2)=="LAYER") {
            for(std::size_t j=0;j+1<extension->pairs_.size();++j) {
                const auto& p=extension->pairs_[j];const auto& next=extension->pairs_[j+1];
                if(p.code==3 && (ascii_equal(extension->decode_string(p.value),"ACAD_LAYERSTATES") || ascii_equal(extension->decode_string(p.value),"ACAD_LAYERSTATE")) &&
                    (next.code==350 || next.code==360) && handle_value(next.value)==handle_value(states->handle()))layer_state=true;
            }
        }
    }
    bool state_header=true, state_layer_seen=false;
    const auto common=[&](int code){return record.locate("AcDbEntity",code);};
    for(std::size_t i=0;i<record.pairs_.size();++i) {
        const auto& p=record.pairs_[i];if(head && p.code==9) header=p.value;
        if(p.code==100 && p.value=="AcDbXrecord" && type=="XRECORD") payload=true;
        if(payload) {
            if(p.code==330)state_header=false;
            if(layer_state && state_header && !state_layer_seen && p.code==302){result.push_back(i);state_layer_seen=true;}
            continue;
        }
        if(p.code==101) opaque=true;
        if(p.code==102) {if(!p.value.empty() && p.value.front()=='{')++depth;else if(p.value=="}" && depth)--depth;continue;}
        if(depth || opaque)continue;
        if(p.code==1001)xdata=true;
        bool ref=false;
        switch(table) {
            case SymbolTable::Layer: ref=(xdata && p.code==1003)||(!xdata && p.code==8 && ((graphical && common(8).found && common(8).index==i) || (head && header=="$CLAYER")));break;
            case SymbolTable::LineType: ref=!xdata && p.code==6 && ((graphical && common(6).found && common(6).index==i) || type=="LAYER" || type=="MLINESTYLE" || (head && header=="$CELTYPE"));break;
            case SymbolTable::TextStyle: ref=!xdata && p.code==7 && (type=="TEXT" || type=="MTEXT" || type=="ATTRIB" || type=="ATTDEF" || type=="SHAPE" || type=="TABLESTYLE" || (head && header=="$TEXTSTYLE"));break;
            case SymbolTable::DimStyle: ref=(!xdata && p.code==3 && type=="DIMENSION") || (head && header=="$DIMSTYLE" && p.code==2);break;
            case SymbolTable::AppId: ref=p.code==1001;break;
            case SymbolTable::Ucs: ref=head && p.code==2 && (header=="$UCSNAME" || header=="$PUCSNAME");break;
            case SymbolTable::View:ref=!xdata && p.code==6 && (type=="LAYOUT" || type=="PLOTSETTINGS");break;
            case SymbolTable::BlockRecord:ref=!xdata && ((p.code==2 && (type=="INSERT" || type=="DIMENSION" || type=="BLOCK")) ||
                (p.code==3 && type=="BLOCK") || (type=="DIMSTYLE" && p.code>=5 && p.code<=7) ||
                (head && p.code==1 && (header=="$DIMBLK" || header=="$DIMBLK1" || header=="$DIMBLK2" || header=="$DIMLDRBLK")));break;
            default:break;
        }
        if(ref) result.push_back(i);
    }return result;
}
std::uint64_t DxfDocument::rename_symbol(SymbolTable table,std::string_view old_name,std::string new_name) {
    valid_symbol_name(new_name);
    const auto locations=symbol_locations(table,old_name);
    if(locations.size()!=1) throw std::invalid_argument("Rename requires one matching symbol");
    const auto& source=record_at({locations[0],std::nullopt,false});
    const auto name=source.text_field(symbol_subclass(table),2);
    if(protected_symbol(table,name) || name==new_name) throw std::invalid_argument("Reserved or unchanged symbol name");
    const auto existing=symbol_locations(table,new_name);
    if(!existing.empty() && existing!=locations) throw std::invalid_argument("Symbol name already exists");
    const auto id=handle_value(source.handle());
    if(!id || records_with_handle(source.handle()).size()!=1) throw std::invalid_argument("Ambiguous symbol identity");
    DxfDocument staged=*this;const auto key=detail::symbol_name_key(name);
    auto rewrite=[&](DxfRecord& record){for(auto at:staged.symbol_references(record,table)) {
        auto& p=record.pairs_[at];if(detail::symbol_name_key(record.decode_string(p.value))==key) p.value=record.encode_string(new_name);
    }};
    for(auto& item:staged.records_) {
        std::visit(rewrite,item);
        if(auto* children=sequence(item)) {for(auto& v:children->vertices_) rewrite(v);if(children->seqend_) rewrite(*children->seqend_);}
    }
    std::visit([&](DxfRecord& record){record.set_text_field(symbol_subclass(table),2,new_name);},staged.records_[locations[0]]);
    staged.rebuild_indexes();*this=std::move(staged);return id;
}
bool DxfDocument::remove_symbol(SymbolTable table,std::string_view name) {
    const auto locations=symbol_locations(table,name);if(locations.empty()) return false;
    if(locations.size()!=1) throw std::invalid_argument("Removal requires one matching symbol");
    const auto& source=record_at({locations[0],std::nullopt,false});
    const auto actual=source.text_field(symbol_subclass(table),2);const auto key=detail::symbol_name_key(actual);
    if(protected_symbol(table,actual)) throw std::invalid_argument("Reserved symbol cannot be removed");
    const auto identity=handle_value(source.handle());
    if(!identity || records_with_handle(source.handle()).size()!=1) throw std::invalid_argument("Ambiguous symbol identity");
    std::unordered_set<std::size_t> removed{locations[0]};std::unordered_set<std::uint64_t> identities{identity};
    if(table==SymbolTable::BlockRecord) {
        // Legacy members may omit group 330. Physical BLOCK/ENDBLK membership
        // still makes a block nonempty; never leave those records orphaned.
        std::string section;bool in_target=false;
        for(std::size_t i=0;i<records_.size();++i) {
            const auto& r=record_at({i,std::nullopt,false});
            if(r.type()=="SECTION")section=trim(r.field("",2));
            if(r.type()=="ENDSEC") {
                if(in_target)throw std::invalid_argument("Unterminated block definition");
                section.clear();
            }
            if(section!="BLOCKS")continue;
            if(r.type()=="ENDBLK")in_target=false;
            else if(in_target)throw std::invalid_argument("Block is not empty");
            else if(r.type()=="BLOCK")in_target=handle_value(r.owner_handle())==identity ||
                detail::symbol_name_key(r.text_field("AcDbBlockBegin",2))==key;
        }
        if(in_target)throw std::invalid_argument("Unterminated block definition");
        for(auto index:entity_records_) {
            const auto& r=record_at({index,std::nullopt,false});
            if(handle_value(r.owner_handle())==identity) throw std::invalid_argument("Block is not empty");
        }
        for(std::size_t i=0;i<records_.size();++i) {
            const auto& r=record_at({i,std::nullopt,false});
            if((r.type()=="BLOCK" || r.type()=="ENDBLK") && handle_value(r.owner_handle())==identity) {
                if(!handle_value(r.handle()) || records_with_handle(r.handle()).size()!=1) throw std::invalid_argument("Ambiguous block definition identity");
                removed.insert(i);identities.insert(handle_value(r.handle()));
            }
        }
    }
    auto check_record=[&](const DxfRecord& r) {
        for(auto at:symbol_references(r,table)) if(detail::symbol_name_key(r.decode_string(r.pairs_[at].value))==key)
            throw std::invalid_argument("Symbol is referenced by name");
        for(auto [at,editable]:reference_positions(r)) if(identities.count(handle_value(r.pairs_[at].value)))
            throw std::invalid_argument("Symbol is referenced by handle");
        if(identities.count(handle_value(r.owner_handle()))) throw std::invalid_argument("Symbol owns additional objects");
    };
    for(std::size_t i=0;i<records_.size();++i) {
        if(removed.count(i)) continue;
        check_record(record_at({i,std::nullopt,false}));
        if(const auto* children=sequence(records_[i])) {for(const auto& v:children->vertices_)check_record(v);if(children->seqend_)check_record(*children->seqend_);}
    }
    // An extension dictionary owned by the entry also blocks removal until explicitly detached.
    if(std::any_of(source.pairs_.begin(),source.pairs_.end(),[](const auto& p){return p.code==102 && p.value=="{ACAD_XDICTIONARY";}))
        throw std::invalid_argument("Symbol has an extension dictionary");
    DxfDocument staged=*this;
    for(std::size_t i=staged.records_.size();i-->0;) if(removed.count(i)) staged.records_.erase(staged.records_.begin()+i);
    std::string section,current;std::optional<std::size_t> header;int count=0;
    for(std::size_t i=0;i<staged.records_.size();++i) {
        const auto& r=staged.record_at({i,std::nullopt,false});
        if(r.type()=="SECTION")section=r.field("",2);
        if(section!="TABLES")continue;
        if(r.type()=="TABLE") {current=r.field("",2);if(current==symbol_type(table)){if(header)throw std::invalid_argument("Multiple symbol table headers");header=i;}}
        else if(r.type()=="ENDTAB")current.clear();
        else if(current==symbol_type(table) && r.type()==current)++count;
    }
    if(!header) throw std::invalid_argument("Missing symbol table header");
    std::visit([&](DxfRecord& r){r.set_short_field("AcDbSymbolTable",70,count);},staged.records_[*header]);
    staged.rebuild_indexes();*this=std::move(staged);return true;
}

void DxfDocument::set_symbol_properties(SymbolTable table,std::string_view name,
    const std::vector<SymbolProperty>& properties) {
    const auto locations=symbol_locations(table,name);
    if(locations.size()!=1) throw std::invalid_argument("Update requires one matching symbol");
    DxfDocument staged=*this;
    std::visit([&](DxfRecord& r){staged.apply_symbol_properties(r,table,properties);},staged.records_[locations.front()]);
    staged.rebuild_indexes();*this=std::move(staged);
}
