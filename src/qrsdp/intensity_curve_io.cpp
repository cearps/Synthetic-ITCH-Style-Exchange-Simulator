#include "qrsdp/intensity_curve_io.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

namespace qrsdp {

namespace {

bool writeJsonFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f) return false;
    f << content;
    return f.good();
}

}  // namespace

bool saveCurveToJson(const std::string& path, const IntensityCurve& curve) {
    if (curve.empty()) return false;
    std::ostringstream out;
    out << "{\"values\":[";
    for (size_t n = 0; n <= curve.nMax(); ++n) {
        if (n > 0) out << ",";
        out << curve.value(n);
    }
    out << "],\"tail\":\"";
    out << (curve.nMax() == 0 ? "FLAT" : "FLAT");  // TailRule not exposed; assume FLAT for now
    out << "\"}";
    return writeJsonFile(path, out.str());
}

bool loadCurveFromJson(const std::string& path, IntensityCurve& curve) {
    std::ifstream f(path);
    if (!f) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    std::vector<double> values;
    IntensityCurve::TailRule tail = IntensityCurve::TailRule::FLAT;
    std::vector<char> buf(content.begin(), content.end());
    buf.push_back('\0');
    char* p = buf.data();
    while (*p && *p != '[') ++p;
    if (*p != '[') return false;
    ++p;
    while (*p) {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
        if (*p == ']') break;
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) return false;
        values.push_back(v);
        p = end;
        while (*p == ' ' || *p == ',') ++p;
    }
    if (content.find("\"tail\":\"ZERO\"") != std::string::npos)
        tail = IntensityCurve::TailRule::ZERO;
    curve.setTable(std::move(values), tail);
    return true;
}

}  // namespace qrsdp
