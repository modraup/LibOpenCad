// SPDX-License-Identifier: MPL-2.0
DwgDocument DwgDocument::create_wall(Vector3 start, Vector3 end, double width, double height,
                                     std::optional<std::string> layer) {
    if (!std::isfinite(width) || width <= 0 || !std::isfinite(height) || height <= 0)
        throw std::invalid_argument("Arch dimensions must be finite and positive");
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(start.z) ||
        !std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z) || start.z != 0 ||
        end.z != 0)
        throw std::invalid_argument("wall baseline must be finite and in the world XY plane");
    auto length = (end - start).length();
    if (!std::isfinite(length) || length <= 0)
        throw std::invalid_argument("wall baseline must have positive finite length");
    auto profile = centered_wall_profile(start, end, width);
    if (!profile)
        throw std::invalid_argument("wall dimensions are too small for a supported profile");
    auto direction = (end - start) / length;
    return DwgDocument(detail::create_dwg_architecture(ArchKind::Wall, start, direction, width, height, length,
                                                       *profile, layer));
}
DwgDocument DwgDocument::create_door(double width, double height, std::optional<std::string> layer) {
    if (!std::isfinite(width) || width <= 0 || !std::isfinite(height) || height <= 0)
        throw std::invalid_argument("Arch dimensions must be finite and positive");
    return DwgDocument(detail::create_dwg_architecture(
        ArchKind::Door, {}, Vector3::UNIT_X, width, height, 0, {{}, {width + 100, 0, 0}, {width + 100, width + 120, 0},
                                                               {0, width + 120, 0}, {}},
        layer));
}
DwgDocument DwgDocument::create_window(double width, double height, std::optional<std::string> layer) {
    if (!std::isfinite(width) || width <= 0 || !std::isfinite(height) || height <= 0)
        throw std::invalid_argument("Arch dimensions must be finite and positive");
    return DwgDocument(detail::create_dwg_architecture(
        ArchKind::Window, {}, Vector3::UNIT_X, width, height, 0,
        {{}, {width, 0, 0}, {width, 50, 0}, {0, 50, 0}, {}}, layer));
}
std::optional<ArchEntityData> DwgDocument::architecture_entity(std::uint64_t handle) const {
    auto i = objects_.arch_entities.find(handle);
    if (i == objects_.arch_entities.end())
        return std::nullopt;
    return i->second;
}
std::optional<ArchObject> DwgDocument::architecture_object(std::uint64_t handle) const {
    auto i = objects_.arch_objects.find(handle);
    if (i == objects_.arch_objects.end())
        return std::nullopt;
    return i->second;
}
void DwgDocument::set_wall(std::uint64_t handle, Vector3 start, Vector3 end, double width, double height) {
    *this = DwgDocument(detail::set_dwg_wall(bytes_, objects_, handle, start, end, width, height));
}
void DwgDocument::set_door(std::uint64_t handle, double width, double height) {
    *this = DwgDocument(detail::set_dwg_door(bytes_, objects_, handle, width, height));
}
void DwgDocument::set_window(std::uint64_t handle, double width, double height) {
    *this = DwgDocument(detail::set_dwg_window(bytes_, objects_, handle, width, height));
}
void DwgDocument::set_opening(std::uint64_t handle, double width, double height) {
    *this = DwgDocument(detail::set_dwg_opening(bytes_, objects_, handle, width, height));
}
void DwgDocument::set_stair(std::uint64_t handle, double run, double width, double height) {
    *this = DwgDocument(detail::set_dwg_stair(bytes_, objects_, handle, run, width, height));
}
void DwgDocument::set_member(std::uint64_t handle, double length, double breadth, double depth) {
    *this = DwgDocument(detail::set_dwg_member(bytes_, objects_, handle, length, breadth, depth));
}
void DwgDocument::set_window_assembly(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::WindowAssembly, min, max));
}
void DwgDocument::set_railing(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::Railing, min, max));
}
void DwgDocument::set_slab(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::Slab, min, max));
}
void DwgDocument::set_roof(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::Roof, min, max));
}
void DwgDocument::set_roof_slab(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::RoofSlab, min, max));
}
void DwgDocument::set_space(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::Space, min, max));
}
void DwgDocument::set_curtain_wall(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::CurtainWall, min, max));
}
void DwgDocument::set_column_grid(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::ColumnGrid, min, max));
}
void DwgDocument::set_mass_element(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::MassElement, min, max));
}
void DwgDocument::set_display_theme(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::DisplayTheme, min, max));
}
void DwgDocument::set_ceiling_grid(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_arch_bounds(bytes_, objects_, handle, ArchKind::CeilingGrid, min, max));
}
std::vector<std::uint64_t> DwgDocument::entities_anchored_to(std::uint64_t host) const {
    std::vector<std::uint64_t> anchored;
    auto owning_entity = [&](std::uint64_t h) -> std::uint64_t {
        for (int i = 0; i < 24; ++i) {
            if (objects_.arch_entities.count(h))
                return h;
            auto owner = owner_of(objects_, h);
            if (!owner || owner == h)
                return 0;
            h = owner;
        }
        return 0;
    };
    auto push = [&](std::uint64_t entity) {
        if (!entity || entity == host || !objects_.arch_entities.count(entity))
            return;
        if (std::find(anchored.begin(), anchored.end(), entity) == anchored.end())
            anchored.push_back(entity);
    };
    for (auto &[h, rec] : objects_.associative_records) {
        std::uint64_t dependent = 0;
        if (auto *d = std::get_if<AssocDependency>(&rec.data))
            dependent = d->dependent_on;
        else if (auto *d = std::get_if<AssocGeomDependency>(&rec.data))
            dependent = d->dependency.dependent_on;
        if (dependent != host)
            continue;
        push(owning_entity(rec.owner));
    }
    for (auto &[h, object] : objects_.arch_objects) {
        if (object.dxf_name.find("ANCHOR") == std::string::npos ||
            object.dxf_name.find("DISP_REP") != std::string::npos)
            continue;
        if (!object.raw_dwg_data || object.raw_dwg_handle_bits <= 8)
            continue;
        bool points = false;
        try {
            DwgBitReader refs(*object.raw_dwg_data);
            auto total = object.raw_dwg_data->size() * 8;
            auto start = total - static_cast<std::size_t>(object.raw_dwg_handle_bits);
            if (start >= total)
                continue;
            refs.seek(start);
            for (int i = 0; i < 8 && refs.remaining() >= 8; ++i) {
                auto ref = refs.handle(object.handle);
                if (ref.value == host) {
                    points = true;
                    break;
                }
                if (!ref.value)
                    break;
            }
        } catch (const std::exception &) {
        }
        if (points)
            push(owning_entity(object.handle));
    }
    return anchored;
}
std::uint64_t DwgDocument::add_wall(Vector3 start, Vector3 end, double width, double height,
                                    std::optional<std::string> layer) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_wall(bytes_, objects_, start, end, width, height, layer, id));
    return id;
}
void DwgDocument::translate_architecture(std::uint64_t handle, Vector3 delta) {
    *this = DwgDocument(detail::translate_dwg_architecture(bytes_, objects_, handle, delta));
}
void DwgDocument::set_profile_bounds(std::uint64_t handle, Vector3 min, Vector3 max) {
    *this = DwgDocument(detail::set_dwg_profile_bounds(bytes_, objects_, handle, min, max));
}
void DwgDocument::reshape_rectangle(std::uint64_t handle, Vector3 start, Vector3 end, double width) {
    *this = DwgDocument(detail::reshape_dwg_rectangle(bytes_, objects_, handle, start, end, width));
}
void DwgDocument::set_continuous_wall(const ContinuousWall &chain, const std::vector<Vector3> &points, double width,
                                      double height) {
    *this = DwgDocument(detail::set_dwg_continuous_wall(bytes_, objects_, chain, points, width, height));
}
namespace {
bool continuous_near(double a, double b) { return std::abs(a - b) <= 1e-6; }
bool continuous_near_point(Vector3 a, Vector3 b) { return (a - b).length() <= 1e-6; }
std::optional<bool> join_continuous(std::vector<Vector3> &points, const StraightWallGeometry &segment) {
    auto end = points.back();
    auto start = points.front();
    if (continuous_near_point(end, segment.start)) {
        points.push_back(segment.end);
        return true;
    }
    if (continuous_near_point(end, segment.end)) {
        points.push_back(segment.start);
        return true;
    }
    if (continuous_near_point(start, segment.end)) {
        points.insert(points.begin(), segment.start);
        return false;
    }
    if (continuous_near_point(start, segment.start)) {
        points.insert(points.begin(), segment.end);
        return false;
    }
    return std::nullopt;
}
} // namespace
std::vector<ContinuousWall> DwgDocument::continuous_walls() const {
    std::vector<std::pair<std::uint64_t, StraightWallGeometry>> segments;
    for (auto &[handle, data] : objects_.arch_entities) {
        if (data.kind != ArchKind::Wall)
            continue;
        auto geometry = data.straight_wall_geometry();
        if (!geometry)
            continue;
        segments.emplace_back(handle, *geometry);
    }
    std::vector<char> unused(segments.size(), 1);
    std::vector<ContinuousWall> chains;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (!unused[index])
            continue;
        unused[index] = 0;
        ContinuousWall chain;
        chain.handles = {segments[index].first};
        chain.points = {segments[index].second.start, segments[index].second.end};
        chain.width = segments[index].second.width;
        chain.height = segments[index].second.height;
        for (;;) {
            bool extended = false;
            for (std::size_t candidate = 0; candidate < segments.size(); ++candidate) {
                if (!unused[candidate])
                    continue;
                auto &geometry = segments[candidate].second;
                if (!continuous_near(geometry.width, chain.width) ||
                    !continuous_near(geometry.height, chain.height))
                    continue;
                if (auto joined = join_continuous(chain.points, geometry)) {
                    unused[candidate] = 0;
                    if (*joined)
                        chain.handles.push_back(segments[candidate].first);
                    else
                        chain.handles.insert(chain.handles.begin(), segments[candidate].first);
                    extended = true;
                    break;
                }
            }
            if (!extended)
                break;
        }
        chains.push_back(std::move(chain));
    }
    return chains;
}
