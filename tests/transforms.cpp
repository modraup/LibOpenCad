// SPDX-License-Identifier: MPL-2.0
#include <libopencad/transform.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace libopencad;

namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

void check_conventions() {
    const Vector3 p{1, 2, 3}, d{1, -1, 2};
    const auto t = Transform::from_translation({5, -2, 1});
    const auto s = Transform::from_scaling({2, 3, -1});
    check(t.apply_rotation(d) == d && t.apply(p) == Vector3{6, 0, 4}, "translation convention");
    check(t.then(s).apply(p) == Vector3{12, 0, -4}, "then order");
    check(t.compose(s).apply(p) == Vector3{7, 4, -2}, "compose order");
    check(t * s == t.compose(s) && t.then(s).apply_rotation(d) == Vector3{2, -3, -2}, "linear part");
    check(
        (Matrix4::rotation_z(std::acos(-1.0) / 2).transform_direction(Vector3::UNIT_X) - Vector3::UNIT_Y)
                .length() < 1e-12,
        "rotation convention");
    check(!Matrix3::zero().inverse() && !Matrix3::scaling(1, 1, 1e-11).inverse(), "singular inverse");
    check(Matrix3::scaling(1, 1, 1e-10).inverse().has_value(), "inverse threshold");
    check(Transform::from_rotation_with_origin({}, 1, p).is_identity(), "degenerate rotation axis");
    check(Transform::from_mirror_plane(p, {}).is_identity(), "degenerate mirror normal");
    const double huge = std::numeric_limits<double>::max();
    check(Transform::from_mirror_plane({huge, 0, 0}, Vector3::UNIT_X).is_identity(), "mirror overflow");
    auto extreme_line = Transform::from_mirror_line({huge, huge, 0}, {-huge, -huge, 0});
    check((extreme_line.apply({3, 5, 7}) - Vector3{5, 3, 7}).length() < 1e-12, "mirror axis overflow");
}

void calculate(std::ostream& out, const std::string& name, Vector3 p, Vector3 d) {
    const Vector3 origin{2, -1, 3}, axis{1, -2, 3}, scale{2, 3, -1};
    const double angle = 0.73;
    const auto t = Transform::from_translation({5, -2, 1});
    const auto r = Transform::from_rotation(axis, angle);
    const auto s = Transform::from_scaling(scale);
    auto record = [&](const char* op, Vector3 point, Vector3 direction) {
        out << name << ' ' << op << ' ' << point.x << ' ' << point.y << ' ' << point.z << ' '
            << direction.x << ' ' << direction.y << ' ' << direction.z << '\n';
    };
    auto linear = [&](const char* op, const Matrix3& m) { record(op, m * p, m * d); };
    auto matrix = [&](const char* op, const Matrix4& m) {
        record(op, m.transform_point(p), m.transform_direction(d));
    };
    auto transform = [&](const char* op, const Transform& f) {
        record(op, f.apply(p), f.apply_rotation(d));
    };
    const auto linear_part = Matrix3::rotation_z(angle) * Matrix3::scaling(scale.x, scale.y, scale.z);
    linear("matrix3_product", linear_part);
    linear("matrix3_transpose", linear_part.transpose());
    linear("matrix3_inverse", linear_part.inverse().value());
    linear("arbitrary_axis", Matrix3::arbitrary_axis(axis));
    linear("arbitrary_axis_z", Matrix3::arbitrary_axis(Vector3::UNIT_Z));
    matrix("matrix3_to_4", Matrix4::from_matrix3(linear_part));
    linear("matrix4_to_3", (t.matrix * r.matrix * s.matrix).to_matrix3());
    matrix("rotation_x", Matrix4::rotation_x(angle));
    matrix("rotation_y", Matrix4::rotation_y(angle));
    matrix("rotation_z", Matrix4::rotation_z(angle));
    matrix("projective", t.matrix.transpose());
    auto near_zero_w = Matrix4::identity();
    near_zero_w.m[3][3] = 1e-11;
    matrix("near_zero_w", near_zero_w);
    transform("translation", t);
    transform("rotation", r);
    transform("uniform", Transform::from_scale(0.5));
    transform("scaling", s);
    transform("scale_about", Transform::from_scaling_with_origin(scale, origin));
    transform("rotate_about", Transform::from_rotation_with_origin(axis, angle, origin));
    transform("translate_then_scale", t.then(s));
    transform("translate_compose_scale", t.compose(s));
    transform("then", t.then(r).then(s));
    transform("compose", t.compose(r).compose(s));
    transform("product", t * r * s);
    transform("mirror_plane", Transform::from_mirror_plane(origin, axis));
    transform("mirror_line", Transform::from_mirror_line(origin, axis));
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 3, "expected input.txt results.txt");
        check_conventions();
        std::ifstream in(argv[1]);
        check(in.is_open(), "cannot open inputs");
        std::ofstream out(argv[2]);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out << std::setprecision(std::numeric_limits<double>::max_digits10);
        std::string line, name;
        unsigned count = 0;
        while (std::getline(in, line)) {
            if (line.empty() || line.front() == '#')
                continue;
            std::istringstream row(line);
            Vector3 p, d;
            if (!(row >> name >> p.x >> p.y >> p.z >> d.x >> d.y >> d.z) || !(row >> std::ws).eof())
                throw std::runtime_error("invalid transform input: " + line);
            calculate(out, name, p, d);
            ++count;
        }
        check(count > 0 && in.eof() && !in.bad(), "incomplete transform input");
        out.close();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
