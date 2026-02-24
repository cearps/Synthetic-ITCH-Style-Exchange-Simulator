#include "model/hlr_params.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace qrsdp {

namespace {

std::vector<double> makeTable(int n_max, double (*f)(int)) {
    std::vector<double> t;
    t.reserve(static_cast<size_t>(n_max) + 1);
    for (int n = 0; n <= n_max; ++n) {
        t.push_back(std::max(0.0, f(n)));
    }
    return t;
}

// Add at best: decreasing with depth so queues equilibrate at modest sizes (~5-6).
// HLR2014 empirical finding: limit-order arrival rate falls with existing depth.
double addBest(int n) {
    return 15.0 / (1.0 + 0.12 * static_cast<double>(n));
}

// Add deeper: slow, further decreasing, to keep total event count reasonable.
double addDeeper(int n) {
    return 5.0 / (1.0 + 0.2 * static_cast<double>(n));
}

// Cancel: concave in n (saturates ~15), matching HLR2014 empirical shape.
double cancelCurve(int n) {
    if (n == 0) return 0.0;
    const double nd = static_cast<double>(n);
    return 0.3 * nd / (1.0 + 0.02 * nd);
}

// Market at best: CONSTANT rate for n > 0. HLR2014 shows market orders arrive
// independent of queue depth. Constant rate ensures queues can actually drain.
double marketCurve(int n) {
    if (n == 0) return 0.0;
    return 8.0;
}

// --- JSON I/O helpers (hand-rolled, no external library) ---

void writeCurveArray(std::ostream& out, const IntensityCurve& curve) {
    out << "[";
    for (size_t n = 0; n <= curve.nMax(); ++n) {
        if (n > 0) out << ",";
        out << curve.value(n);
    }
    out << "]";
}

bool skipWhitespace(const char*& p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
    return *p != '\0';
}

bool expectChar(const char*& p, char c) {
    skipWhitespace(p);
    if (*p != c) return false;
    ++p;
    return true;
}

bool parseDouble(const char*& p, double& v) {
    skipWhitespace(p);
    char* end = nullptr;
    v = std::strtod(p, &end);
    if (end == p) return false;
    p = end;
    return true;
}

bool parseInt(const char*& p, int& v) {
    skipWhitespace(p);
    char* end = nullptr;
    long lv = std::strtol(p, &end, 10);
    if (end == p) return false;
    v = static_cast<int>(lv);
    p = end;
    return true;
}

bool parseNumberArray(const char*& p, std::vector<double>& values) {
    values.clear();
    if (!expectChar(p, '[')) return false;
    skipWhitespace(p);
    if (*p == ']') { ++p; return true; }
    while (true) {
        double v;
        if (!parseDouble(p, v)) return false;
        values.push_back(v);
        skipWhitespace(p);
        if (*p == ',') { ++p; continue; }
        if (*p == ']') { ++p; return true; }
        return false;
    }
}

bool parseArrayOfArrays(const char*& p, std::vector<std::vector<double>>& arrays) {
    arrays.clear();
    if (!expectChar(p, '[')) return false;
    skipWhitespace(p);
    if (*p == ']') { ++p; return true; }
    while (true) {
        std::vector<double> arr;
        if (!parseNumberArray(p, arr)) return false;
        arrays.push_back(std::move(arr));
        skipWhitespace(p);
        if (*p == ',') { ++p; continue; }
        if (*p == ']') { ++p; return true; }
        return false;
    }
}

const char* findKey(const char* json, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    const char* pos = std::strstr(json, needle.c_str());
    if (!pos) return nullptr;
    pos += needle.size();
    while (*pos == ' ' || *pos == ':') ++pos;
    return pos;
}

}  // namespace

HLRParams makeDefaultHLRParams(int K, int n_max) {
    HLRParams p;
    p.K = std::max(1, K);
    p.n_max = std::max(1, n_max);
    p.spread_sensitivity = 0.3;
    p.imbalance_sensitivity = 1.0;

    const size_t k = static_cast<size_t>(p.K);
    p.lambda_L_bid.resize(k);
    p.lambda_L_ask.resize(k);
    p.lambda_C_bid.resize(k);
    p.lambda_C_ask.resize(k);

    for (int i = 0; i < p.K; ++i) {
        const bool is_best = (i == 0);
        auto add_table = makeTable(p.n_max, is_best ? addBest : addDeeper);
        auto cancel_table = makeTable(p.n_max, cancelCurve);
        p.lambda_L_bid[static_cast<size_t>(i)].setTable(add_table, IntensityCurve::TailRule::FLAT);
        p.lambda_L_ask[static_cast<size_t>(i)].setTable(add_table, IntensityCurve::TailRule::FLAT);
        p.lambda_C_bid[static_cast<size_t>(i)].setTable(cancel_table, IntensityCurve::TailRule::FLAT);
        p.lambda_C_ask[static_cast<size_t>(i)].setTable(cancel_table, IntensityCurve::TailRule::FLAT);
    }

    auto market_table = makeTable(p.n_max, marketCurve);
    p.lambda_M_buy.setTable(market_table, IntensityCurve::TailRule::FLAT);
    p.lambda_M_sell.setTable(market_table, IntensityCurve::TailRule::FLAT);

    return p;
}

bool saveHLRParamsToJson(const std::string& path, const HLRParams& params) {
    std::ofstream f(path);
    if (!f) return false;

    f << "{\n";
    f << "  \"K\": " << params.K << ",\n";
    f << "  \"n_max\": " << params.n_max << ",\n";
    f << "  \"spread_sensitivity\": " << params.spread_sensitivity << ",\n";
    f << "  \"imbalance_sensitivity\": " << params.imbalance_sensitivity << ",\n";

    auto writeCurveVec = [&](const char* key, const std::vector<IntensityCurve>& curves) {
        f << "  \"" << key << "\": [\n";
        for (size_t i = 0; i < curves.size(); ++i) {
            f << "    ";
            writeCurveArray(f, curves[i]);
            if (i + 1 < curves.size()) f << ",";
            f << "\n";
        }
        f << "  ],\n";
    };

    writeCurveVec("lambda_L_bid", params.lambda_L_bid);
    writeCurveVec("lambda_L_ask", params.lambda_L_ask);
    writeCurveVec("lambda_C_bid", params.lambda_C_bid);
    writeCurveVec("lambda_C_ask", params.lambda_C_ask);

    f << "  \"lambda_M_buy\": ";
    writeCurveArray(f, params.lambda_M_buy);
    f << ",\n";
    f << "  \"lambda_M_sell\": ";
    writeCurveArray(f, params.lambda_M_sell);
    f << "\n";

    f << "}\n";
    return f.good();
}

bool loadHLRParamsFromJson(const std::string& path, HLRParams& params) {
    std::ifstream f(path);
    if (!f) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    const char* json = content.c_str();

    const char* p = findKey(json, "K");
    if (!p) return false;
    if (!parseInt(p, params.K)) return false;

    p = findKey(json, "n_max");
    if (!p) return false;
    if (!parseInt(p, params.n_max)) return false;

    p = findKey(json, "spread_sensitivity");
    if (p) {
        parseDouble(p, params.spread_sensitivity);
    } else {
        params.spread_sensitivity = 0.3;
    }

    p = findKey(json, "imbalance_sensitivity");
    if (p) {
        parseDouble(p, params.imbalance_sensitivity);
    } else {
        params.imbalance_sensitivity = 1.0;
    }

    auto loadCurveVec = [&](const char* key, std::vector<IntensityCurve>& curves) -> bool {
        const char* pos = findKey(json, key);
        if (!pos) return false;
        std::vector<std::vector<double>> arrays;
        if (!parseArrayOfArrays(pos, arrays)) return false;
        curves.resize(arrays.size());
        for (size_t i = 0; i < arrays.size(); ++i) {
            curves[i].setTable(std::move(arrays[i]), IntensityCurve::TailRule::FLAT);
        }
        return true;
    };

    if (!loadCurveVec("lambda_L_bid", params.lambda_L_bid)) return false;
    if (!loadCurveVec("lambda_L_ask", params.lambda_L_ask)) return false;
    if (!loadCurveVec("lambda_C_bid", params.lambda_C_bid)) return false;
    if (!loadCurveVec("lambda_C_ask", params.lambda_C_ask)) return false;

    auto loadSingleCurve = [&](const char* key, IntensityCurve& curve) -> bool {
        const char* pos = findKey(json, key);
        if (!pos) return false;
        std::vector<double> values;
        if (!parseNumberArray(pos, values)) return false;
        curve.setTable(std::move(values), IntensityCurve::TailRule::FLAT);
        return true;
    };

    if (!loadSingleCurve("lambda_M_buy", params.lambda_M_buy)) return false;
    if (!loadSingleCurve("lambda_M_sell", params.lambda_M_sell)) return false;

    return true;
}

}  // namespace qrsdp
