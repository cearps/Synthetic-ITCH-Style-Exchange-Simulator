#include <gtest/gtest.h>
#include "qrsdp/intensity_curve.h"
#include "qrsdp/hlr_params.h"
#include "qrsdp/curve_intensity_model.h"
#include "qrsdp/records.h"
#include "qrsdp/event_types.h"
#include <cmath>
#include <vector>

namespace qrsdp {
namespace test {

// --- IntensityCurve ---
TEST(IntensityCurve, ValueInRange) {
    IntensityCurve c;
    c.setTable({0.0, 1.0, 2.0, 3.0}, IntensityCurve::TailRule::FLAT);
    EXPECT_DOUBLE_EQ(c.value(0), 0.0);
    EXPECT_DOUBLE_EQ(c.value(1), 1.0);
    EXPECT_DOUBLE_EQ(c.value(2), 2.0);
    EXPECT_DOUBLE_EQ(c.value(3), 3.0);
    EXPECT_DOUBLE_EQ(c.value(100), 3.0);  // flat tail
}

TEST(IntensityCurve, TailZero) {
    IntensityCurve c;
    c.setTable({1.0, 2.0}, IntensityCurve::TailRule::ZERO);
    EXPECT_DOUBLE_EQ(c.value(0), 1.0);
    EXPECT_DOUBLE_EQ(c.value(1), 2.0);
    EXPECT_DOUBLE_EQ(c.value(2), 0.0);
}

TEST(IntensityCurve, EmptyReturnsZero) {
    IntensityCurve c;
    EXPECT_DOUBLE_EQ(c.value(0), 0.0);
}

TEST(IntensityCurve, NonNegativeClamp) {
    IntensityCurve c;
    c.setTable({-1.0, 0.5}, IntensityCurve::TailRule::FLAT);
    EXPECT_GE(c.value(0), 0.0);
    EXPECT_GE(c.value(1), 0.0);
}

// --- HLRParams / makeDefaultHLRParams ---
TEST(HLRParams, DefaultStarterCurves) {
    HLRParams p = makeDefaultHLRParams(3, 20);
    EXPECT_EQ(p.K, 3);
    EXPECT_EQ(p.n_max, 20);
    EXPECT_EQ(static_cast<int>(p.lambda_L_bid.size()), 3);
    EXPECT_EQ(static_cast<int>(p.lambda_L_ask.size()), 3);
    EXPECT_FALSE(p.lambda_M_buy.empty());
    EXPECT_FALSE(p.lambda_M_sell.empty());
}

// --- CurveIntensityModel ---
TEST(CurveIntensityModel, ComputeWithDepthsReturnsPositiveTotal) {
    HLRParams p = makeDefaultHLRParams(2, 10);
    CurveIntensityModel model(p);
    BookState state;
    state.features = BookFeatures{9999, 10001, 50, 50, 2, 0.0};
    state.bid_depths = {50, 30};
    state.ask_depths = {50, 25};
    Intensities i = model.compute(state);
    EXPECT_GT(i.total(), 0.0);
    EXPECT_GE(i.add_bid, 0.0);
    EXPECT_GE(i.exec_buy, 0.0);
}

TEST(CurveIntensityModel, GetPerLevelIntensitiesAfterCompute) {
    HLRParams p = makeDefaultHLRParams(2, 10);
    CurveIntensityModel model(p);
    BookState state;
    state.bid_depths = {5, 3};
    state.ask_depths = {5, 2};
    state.features = BookFeatures{9999, 10001, 5, 5, 2, 0.0};
    model.compute(state);
    std::vector<double> w;
    EXPECT_TRUE(model.getPerLevelIntensities(w));
    EXPECT_EQ(w.size(), 4u * 2u + 2u);  // 4*K+2
}

TEST(CurveIntensityModel, DecodePerLevelIndex) {
    EventType t;
    size_t lev;
    CurveIntensityModel::decodePerLevelIndex(0, 3, t, lev);
    EXPECT_EQ(t, EventType::ADD_BID);
    EXPECT_EQ(lev, 0u);
    CurveIntensityModel::decodePerLevelIndex(2, 3, t, lev);
    EXPECT_EQ(t, EventType::ADD_BID);
    EXPECT_EQ(lev, 2u);
    CurveIntensityModel::decodePerLevelIndex(3, 3, t, lev);
    EXPECT_EQ(t, EventType::ADD_ASK);
    EXPECT_EQ(lev, 0u);
    CurveIntensityModel::decodePerLevelIndex(12, 3, t, lev);  // 4*3 = 12 -> EXECUTE_BUY
    EXPECT_EQ(t, EventType::EXECUTE_BUY);
    EXPECT_EQ(lev, 0u);
    CurveIntensityModel::decodePerLevelIndex(13, 3, t, lev);
    EXPECT_EQ(t, EventType::EXECUTE_SELL);
    EXPECT_EQ(lev, 0u);
}

}  // namespace test
}  // namespace qrsdp
