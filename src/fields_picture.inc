struct PictureZeros {
    bool feet = false, inches = false, leading = false, trailing = false;
};

struct Picture {
    std::string lit, pre, suf;
    std::optional<std::size_t> slot;
    std::optional<std::int64_t> lu, au, pt, bl, lw;
    std::optional<std::size_t> pr, printf_prec;
    std::int64_t zs = 0, qf = 0, nn = 0;
    std::optional<char> ds, th, ls;
    std::optional<std::pair<std::int64_t, std::optional<double>>> conv;

    static Picture parse(std::string_view pic);
    PictureZeros zeros() const;
    double convert(double v) const;
    std::string wrap(std::string_view value) const;
    std::int64_t linear_mode(const DxfDocument &doc) const;
};

std::string format_fixed_prec(double x, std::size_t prec) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.setf(std::ios::fixed);
    o.precision(static_cast<std::streamsize>(prec));
    o << x;
    return o.str();
}

std::optional<char> char_from_code(std::int64_t n) {
    if (n < 0 || n > 127)
        return std::nullopt;
    return static_cast<char>(n);
}

Picture Picture::parse(std::string_view pic) {
    Picture p;
    std::string_view rest = pic;
    while (true) {
        auto at = rest.find('%');
        if (at == std::string_view::npos) {
            p.lit.append(rest.begin(), rest.end());
            break;
        }
        p.lit.append(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(at));
        auto tail = rest.substr(at + 1);
        if (!tail.empty() && tail.front() == '.') {
            auto d = tail.substr(1);
            std::size_t nd = 0;
            while (nd < d.size() && std::isdigit(static_cast<unsigned char>(d[nd])))
                ++nd;
            if (nd > 0 && nd < d.size() && d[nd] == 'f') {
                try {
                    p.printf_prec = static_cast<std::size_t>(std::stoul(std::string(d.substr(0, nd))));
                } catch (...) {
                    p.printf_prec.reset();
                }
                if (!p.slot)
                    p.slot = p.lit.size();
                rest = d.substr(nd + 1);
                continue;
            }
        }
        std::string key;
        for (char c : tail) {
            if (!std::isalpha(static_cast<unsigned char>(c)) || key.size() == 2)
                break;
            key.push_back(c);
        }
        if (key.size() < 2) {
            p.lit.push_back('%');
            rest = tail;
            continue;
        }
        auto after_key = tail.substr(key.size());
        std::size_t nd = 0;
        while (nd < after_key.size() && std::isdigit(static_cast<unsigned char>(after_key[nd])))
            ++nd;
        std::size_t end = key.size() + nd;
        std::optional<std::string_view> arg;
        if (nd < after_key.size() && after_key[nd] == '[') {
            auto close = after_key.find(']', nd + 1);
            if (close != std::string_view::npos) {
                arg = after_key.substr(nd + 1, close - (nd + 1));
                end += arg->size() + 2;
            }
        }
        std::int64_t n = 0;
        if (nd > 0) {
            try {
                n = std::stoll(std::string(after_key.substr(0, nd)));
            } catch (...) {
                n = 0;
            }
        }
        auto ch = [&] { return char_from_code(n); };
        if (key == "lu")
            p.lu = n;
        else if (key == "au")
            p.au = n;
        else if (key == "pr")
            p.pr = n < 0 ? 0 : static_cast<std::size_t>(n);
        else if (key == "zs")
            p.zs = n;
        else if (key == "qf")
            p.qf = n;
        else if (key == "ds")
            p.ds = ch();
        else if (key == "th")
            p.th = ch();
        else if (key == "ls") {
            if (arg && !arg->empty())
                p.ls = (*arg)[0];
            else
                p.ls = ch();
        } else if (key == "pt")
            p.pt = n;
        else if (key == "bl")
            p.bl = n;
        else if (key == "lw")
            p.lw = n;
        else if (key == "nn")
            p.nn = n;
        else if (key == "ct") {
            std::optional<double> f;
            if (arg) {
                auto t = *arg;
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
                    t.remove_prefix(1);
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
                    t.remove_suffix(1);
                try {
                    f = std::stod(std::string(t));
                } catch (...) {
                }
            }
            p.conv = std::make_pair(n, f);
        } else if (key == "ps") {
            auto a = arg.value_or(std::string_view{});
            auto comma = a.find(',');
            if (comma == std::string_view::npos) {
                p.pre = std::string(a);
                p.suf.clear();
            } else {
                p.pre = std::string(a.substr(0, comma));
                p.suf = std::string(a.substr(comma + 1));
            }
        }
        if (!p.slot)
            p.slot = p.lit.size();
        rest = tail.substr(std::min(end, tail.size()));
    }
    return p;
}

PictureZeros Picture::zeros() const {
    auto bit = [&](std::int64_t zs_bit, std::int64_t qf_bit) {
        return (zs & zs_bit) != 0 || (qf & qf_bit) != 0;
    };
    return {bit(1, 256), bit(2, 512), bit(4, 1024), bit(8, 2048)};
}

double Picture::convert(double v) const {
    if (!conv)
        return v;
    auto n = conv->first;
    auto f = conv->second;
    auto r = (n & 1) != 0 ? 1.0 / v : v;
    if ((n & 8) != 0)
        return r * f.value_or(1.0);
    if (n == 2)
        return v * 12.0;
    if (n == 3)
        return 1.0 / (12.0 * v);
    if (n == 4)
        return v / 144.0;
    return r;
}

std::string Picture::wrap(std::string_view value) const {
    std::string num = pre + std::string(value) + suf;
    if (!slot)
        return lit + num;
    auto at = std::min(*slot, lit.size());
    return lit.substr(0, at) + num + lit.substr(at);
}

std::int64_t Picture::linear_mode(const DxfDocument &doc) const {
    auto m = lu.value_or(2);
    if (m != 6)
        return m;
    auto header = static_cast<std::int64_t>(doc.linear_unit_format());
    if ((header == 3 || header == 4) && (qf & 1) != 0)
        return 2;
    return header;
}

double rem_euclid_tau(double a, double m) {
    auto r = std::fmod(a, m);
    if (r < 0.0)
        r += m;
    return r;
}

std::string unit_text(double v, std::int64_t mode, std::size_t prec, PictureZeros z, char ds,
                      std::optional<char> th);
std::string angle_text(double rad, std::int64_t mode, std::size_t prec, PictureZeros z, bool normalize);

std::string number_text(const DxfDocument &doc, const Picture &p, double v) {
    auto z = p.zeros();
    if (p.au) {
        auto mode = *p.au == 5 ? static_cast<std::int64_t>(doc.angular_unit_format()) : *p.au;
        auto header_prec = static_cast<std::size_t>(std::max<std::int16_t>(doc.angular_unit_precision(), 0));
        auto prec = std::min(p.pr.value_or(header_prec), std::size_t{16});
        return angle_text(v, mode, prec, z, true);
    }
    auto header_prec = static_cast<std::size_t>(std::max<std::int16_t>(doc.linear_unit_precision(), 0));
    auto prec = std::min(p.pr.value_or(header_prec), std::size_t{16});
    if ((p.qf & 6) != 0)
        return angle_text(v, p.lu.value_or(2), prec, z, (p.qf & 2) != 0);
    return unit_text(v, p.linear_mode(doc), prec, z, p.ds.value_or('.'), p.th);
}

std::string format_number(const DxfDocument &doc, double value, std::string_view pic) {
    auto p = Picture::parse(pic);
    if (p.bl) {
        const char *names[2] = {nullptr, nullptr};
        switch (*p.bl) {
        case 1:
            names[0] = "True";
            names[1] = "False";
            break;
        case 2:
            names[0] = "Yes";
            names[1] = "No";
            break;
        case 3:
            names[0] = "On";
            names[1] = "Off";
            break;
        case 4:
            names[0] = "Enabled";
            names[1] = "Disabled";
            break;
        default:
            return p.wrap(num_str(value));
        }
        return p.wrap(names[value == 0.0 ? 1 : 0]);
    }
    if (p.lw) {
        auto named = static_cast<std::int64_t>(value);
        if (named == -1)
            return "ByLayer";
        if (named == -2)
            return "ByBlock";
        if (named == -3)
            return "Default";
        auto mm = value / 100.0;
        auto v = *p.lw == 2 ? mm / 25.4 : mm;
        return p.wrap(format_fixed_prec(v, p.printf_prec.value_or(6)));
    }
    auto v = p.convert(value);
    if (!std::isfinite(v))
        return "####";
    if (p.nn == 1 && v < 0.0)
        return "(" + p.wrap(number_text(doc, p, -v)) + ")";
    return p.wrap(number_text(doc, p, v));
}

std::string format_point(const DxfDocument &doc, Vector3 pt, std::string_view pic) {
    auto p = Picture::parse(pic);
    auto bits = p.pt.value_or(7);
    std::string sep(1, p.ls.value_or(','));
    sep.push_back(' ');
    double coords[3] = {pt.x, pt.y, pt.z};
    std::string joined;
    for (int i = 0; i < 3; ++i) {
        if ((bits & (1LL << i)) == 0)
            continue;
        if (!joined.empty())
            joined += sep;
        joined += number_text(doc, p, p.convert(coords[i]));
    }
    return p.wrap(joined);
}

std::string unit_text(double v, std::int64_t mode, std::size_t prec, PictureZeros z, char ds,
                      std::optional<char> th) {
    std::string sign = v < 0.0 ? "-" : "";
    double a = std::fabs(v);
    auto decimal = [&](double x, std::size_t p) {
        auto m = std::pow(10.0, static_cast<int>(p));
        auto s = format_fixed_prec(std::round(x * m) / m, p);
        if (z.trailing && s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0')
                s.pop_back();
            if (!s.empty() && s.back() == '.')
                s.pop_back();
        }
        if (z.leading && s.size() >= 2 && s[0] == '0' && s[1] == '.')
            s.erase(s.begin());
        auto dot = s.find('.');
        std::string intp = dot == std::string::npos ? s : s.substr(0, dot);
        std::string frac = dot == std::string::npos ? std::string{} : s.substr(dot);
        if (!frac.empty() && frac.front() == '.')
            frac.front() = ds;
        if (th) {
            std::string grouped;
            for (std::size_t i = 0; i < intp.size(); ++i) {
                if (i > 0 && (intp.size() - i) % 3 == 0)
                    grouped.push_back(*th);
                grouped.push_back(intp[i]);
            }
            intp = std::move(grouped);
        }
        return intp + frac;
    };
    auto fraction = [&](double x) {
        auto shift = static_cast<int>(std::min(prec, std::size_t{8}));
        std::int64_t den = 1LL << shift;
        auto n = static_cast<std::int64_t>(std::round(x * static_cast<double>(den)));
        auto whole = n / den;
        auto num = n % den;
        while (num != 0 && num % 2 == 0) {
            num /= 2;
            den /= 2;
        }
        return std::make_tuple(whole, num, den);
    };
    if (mode == 1) {
        int e = a > 0.0 ? static_cast<int>(std::floor(std::log10(a))) : 0;
        auto scaled = [&](int exp) {
            int k = static_cast<int>(prec) - exp;
            if (k >= 0)
                return std::round(a * std::pow(10.0, k));
            return std::round(a / std::pow(10.0, -k));
        };
        auto n = scaled(e);
        if (n >= std::pow(10.0, static_cast<int>(prec) + 1)) {
            ++e;
            n = scaled(e);
        }
        char digits[64];
        std::snprintf(digits, sizeof(digits), "%0*.0f", static_cast<int>(prec + 1), n);
        std::string m = digits;
        if (prec != 0 && m.size() >= 1)
            m = m.substr(0, 1) + ds + m.substr(1);
        char expbuf[8];
        std::snprintf(expbuf, sizeof(expbuf), "%02d", std::abs(e));
        return sign + m + "E" + (e < 0 ? "-" : "+") + expbuf;
    }
    if (mode == 3) {
        auto m = std::pow(10.0, static_cast<int>(prec));
        auto total = std::round(a * m) / m;
        auto feet = std::trunc(total / 12.0);
        auto inches = total - feet * 12.0;
        if (z.feet && feet == 0.0)
            return sign + decimal(inches, prec) + "\"";
        std::ostringstream feet_o;
        feet_o.imbue(std::locale::classic());
        feet_o << feet;
        if (z.inches && inches == 0.0)
            return sign + feet_o.str() + "'";
        return sign + feet_o.str() + "'-" + decimal(inches, prec) + "\"";
    }
    if (mode == 4) {
        auto parts = fraction(a);
        auto inches = std::get<0>(parts);
        auto num = std::get<1>(parts);
        auto den = std::get<2>(parts);
        auto feet = inches / 12;
        auto inch = inches % 12;
        std::string frac = num != 0 ? std::to_string(num) + "/" + std::to_string(den) : std::string{};
        auto inch_text = [&](bool drop_zero) {
            if (inch == 0 && !frac.empty() && drop_zero)
                return frac;
            if (!frac.empty())
                return std::to_string(inch) + " " + frac;
            return std::to_string(inch);
        };
        if (z.feet && feet == 0)
            return sign + inch_text(true) + "\"";
        if (z.inches && inch == 0 && num == 0)
            return sign + std::to_string(feet) + "'";
        return sign + std::to_string(feet) + "'-" + inch_text(false) + "\"";
    }
    if (mode == 5) {
        auto parts = fraction(a);
        auto whole = std::get<0>(parts);
        auto num = std::get<1>(parts);
        auto den = std::get<2>(parts);
        if (num == 0)
            return sign + std::to_string(whole);
        if (whole == 0)
            return sign + std::to_string(num) + "/" + std::to_string(den);
        return sign + std::to_string(whole) + " " + std::to_string(num) + "/" + std::to_string(den);
    }
    return sign + decimal(a, prec);
}

std::string angle_text(double rad, std::int64_t mode, std::size_t prec, PictureZeros z, bool normalize) {
    const double tau = 2.0 * std::acos(-1.0);
    const double pi = std::acos(-1.0);
    auto a = normalize ? rem_euclid_tau(rad, tau) : rad;
    auto fixed = [&](double x) {
        auto s = format_fixed_prec(x, prec);
        if (z.trailing && s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0')
                s.pop_back();
            if (!s.empty() && s.back() == '.')
                s.pop_back();
        }
        if (z.leading) {
            auto i = s.find("0.");
            if (i != std::string::npos && s.find_first_not_of('-') == i)
                s.erase(i, 1);
        }
        return s;
    };
    auto dms = [&](double deg) {
        if (prec == 0)
            return std::to_string(static_cast<long long>(std::round(deg))) + "d";
        if (prec == 1 || prec == 2) {
            auto m = static_cast<std::int64_t>(std::round(deg * 60.0));
            return std::to_string(m / 60) + "d" + std::to_string(m % 60) + "'";
        }
        auto k = prec > 4 ? prec - 4 : 0;
        auto scale = std::pow(10.0, static_cast<int>(k));
        auto s = std::round(deg * 3600.0 * scale) / scale;
        auto whole = static_cast<std::int64_t>(std::trunc(s));
        auto sec = s - static_cast<double>(whole - whole % 60);
        return std::to_string(whole / 3600) + "d" + std::to_string((whole % 3600) / 60) + "'" +
               format_fixed_prec(sec, k) + "\"";
    };
    auto to_degrees = [&](double r) { return r * 180.0 / pi; };
    if (mode == 1)
        return dms(to_degrees(a));
    if (mode == 2)
        return fixed(a * 200.0 / pi) + "g";
    if (mode == 3)
        return fixed(a) + "r";
    if (mode == 4) {
        auto deg = to_degrees(a);
        char ns = 'N', ew = 'E';
        double bearing = 90.0 - deg;
        if (deg <= 90.0) {
            ns = 'N';
            bearing = 90.0 - deg;
            ew = 'E';
        } else if (deg <= 180.0) {
            ns = 'N';
            bearing = deg - 90.0;
            ew = 'W';
        } else if (deg <= 270.0) {
            ns = 'S';
            bearing = 270.0 - deg;
            ew = 'W';
        } else {
            ns = 'S';
            bearing = deg - 270.0;
            ew = 'E';
        }
        if (prec > 0 && std::fabs(bearing - 90.0) < 1e-9)
            return std::string(1, ew);
        if (prec > 0 && std::fabs(bearing) < 1e-9)
            return std::string(1, ns);
        std::string out;
        out.push_back(ns);
        out.push_back(' ');
        out += dms(bearing);
        out.push_back(' ');
        out.push_back(ew);
        return out;
    }
    return fixed(to_degrees(a));
}
