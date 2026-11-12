// SPDX-License-Identifier: MPL-2.0
#include <libopencad/vector.hpp>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace libopencad;

namespace {
void check(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}

void check_known_results() {
    const Vector2 a{3, 4}, b{-2, 1};
    check(a.length() == 5 && a.length_squared() == 25 && a.dot(b) == -2 && a.cross(b) == 11,
          "2D geometry");
    check(a.normalize() == Vector2{0.6, 0.8} && Vector2{}.normalize() == Vector2::ZERO,
          "2D normalization");
    const Vector3 p{3, 4, 12}, q{-2, 1, 2};
    check(p.length() == 13 && p.length_squared() == 169 && p.dot(q) == 22 &&
          p.cross(q) == Vector3{-4, -30, 11}, "3D geometry");
    check(p.normalize() == p / 13 && Vector3{}.normalize() == Vector3::ZERO, "3D normalization");
    check(a.distance(Vector2::ZERO) == 5 && p.distance(Vector3::ZERO) == 13, "distance");
    check(a + b - b == a && -(q * 2) == Vector3{4, -2, -4}, "vector arithmetic");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "expected input.txt cpp-results.txt");
        check_known_results();
        std::ifstream input(argv[1]);
        check(input.is_open(), "cannot open vector inputs");
        std::ofstream out(argv[2]);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(std::numeric_limits<double>::max_digits10);
        std::string line, name;
        unsigned count = 0;
        while (std::getline(input, line)) {
            if (line.empty() || line.front() == '#') continue;
            std::istringstream row(line);
            Vector3 a, b;
            if (!(row >> name >> a.x >> a.y >> a.z >> b.x >> b.y >> b.z) || !(row >> std::ws).eof())
                throw std::runtime_error("invalid vector input: " + line);
            const Vector2 a2{a.x, a.y}, b2{b.x, b.y};
            const auto n2 = a2.normalize();
            const auto cross = a.cross(b), n3 = a.normalize();
            out << name << ' ' << a2.length() << ' ' << a2.dot(b2) << ' ' << a2.cross(b2)
                << ' ' << n2.x << ' ' << n2.y << ' ' << a.length() << ' ' << a.dot(b)
                << ' ' << cross.x << ' ' << cross.y << ' ' << cross.z
                << ' ' << n3.x << ' ' << n3.y << ' ' << n3.z << '\n';
            ++count;
        }
        check(input.eof() && !input.bad() && count > 0, "incomplete vector inputs");
        out.close();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
