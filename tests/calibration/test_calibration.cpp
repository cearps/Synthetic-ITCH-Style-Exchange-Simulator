#include <gtest/gtest.h>
#include "calibration/intensity_estimator.h"
#include "calibration/intensity_curve_io.h"
#include "model/hlr_params.h"
#include "core/records.h"
#include "core/event_types.h"
#include <cstdio>
#include <string>

namespace qrsdp {
namespace test {

TEST(IntensityEstimator, LambdaTotalAndType) {
    IntensityEstimator est;
    est.reset();
    est.recordSojourn(5, 0.1, EventType::ADD_BID);
    est.recordSojourn(5, 0.2, EventType::CANCEL_BID);
    est.recordSojourn(5, 0.3, EventType::EXECUTE_SELL);
    const double lambda_tot = est.lambdaTotal(5);
    EXPECT_GT(lambda_tot, 0.0);
    EXPECT_NEAR(lambda_tot, 3.0 / 0.6, 0.01);  // 3 events, 0.6 sec total
    const double lambda_add = est.lambdaType(5, EventType::ADD_BID);
    EXPECT_NEAR(lambda_add, lambda_tot / 3.0, 0.01);
}

TEST(IntensityCurveIo, SaveAndLoad) {
    IntensityCurve c;
    c.setTable({1.0, 2.0, 3.0}, IntensityCurve::TailRule::FLAT);
    const std::string path = "test_curve_io_tmp.json";
    ASSERT_TRUE(saveCurveToJson(path, c));
    IntensityCurve loaded;
    ASSERT_TRUE(loadCurveFromJson(path, loaded));
    EXPECT_EQ(loaded.nMax(), 2u);
    EXPECT_DOUBLE_EQ(loaded.value(0), 1.0);
    EXPECT_DOUBLE_EQ(loaded.value(1), 2.0);
    EXPECT_DOUBLE_EQ(loaded.value(2), 3.0);
    std::remove(path.c_str());
}

TEST(HLRParamsIo, SaveAndLoadRoundTrip) {
    HLRParams orig = makeDefaultHLRParams(3, 10);
    orig.spread_sensitivity = 0.42;

    const std::string path = "test_hlr_params_tmp.json";
    ASSERT_TRUE(saveHLRParamsToJson(path, orig));

    HLRParams loaded;
    ASSERT_TRUE(loadHLRParamsFromJson(path, loaded));
    EXPECT_EQ(loaded.K, 3);
    EXPECT_EQ(loaded.n_max, 10);
    EXPECT_NEAR(loaded.spread_sensitivity, 0.42, 1e-6);
    ASSERT_EQ(loaded.lambda_L_bid.size(), 3u);
    ASSERT_EQ(loaded.lambda_C_ask.size(), 3u);

    for (int i = 0; i < 3; ++i) {
        for (int n = 0; n <= 10; ++n) {
            const size_t si = static_cast<size_t>(i);
            const size_t sn = static_cast<size_t>(n);
            EXPECT_NEAR(loaded.lambda_L_bid[si].value(sn), orig.lambda_L_bid[si].value(sn), 0.01)
                << "L_bid[" << i << "](" << n << ")";
            EXPECT_NEAR(loaded.lambda_C_bid[si].value(sn), orig.lambda_C_bid[si].value(sn), 0.01)
                << "C_bid[" << i << "](" << n << ")";
        }
    }
    for (int n = 0; n <= 10; ++n) {
        const size_t sn = static_cast<size_t>(n);
        EXPECT_NEAR(loaded.lambda_M_buy.value(sn), orig.lambda_M_buy.value(sn), 0.01)
            << "M_buy(" << n << ")";
        EXPECT_NEAR(loaded.lambda_M_sell.value(sn), orig.lambda_M_sell.value(sn), 0.01)
            << "M_sell(" << n << ")";
    }

    EXPECT_TRUE(loaded.hasCurves());
    std::remove(path.c_str());
}

TEST(HLRParamsIo, LoadBadPathFails) {
    HLRParams p;
    EXPECT_FALSE(loadHLRParamsFromJson("nonexistent_file_xyz.json", p));
}

TEST(HLRParams, DefaultsHaveSpreadSensitivity) {
    HLRParams p = makeDefaultHLRParams(5);
    EXPECT_NEAR(p.spread_sensitivity, 0.3, 1e-6);
    EXPECT_TRUE(p.hasCurves());
    EXPECT_EQ(p.lambda_L_bid.size(), 5u);
    EXPECT_GT(p.lambda_M_buy.value(5), 0.0);
    EXPECT_DOUBLE_EQ(p.lambda_M_buy.value(0), 0.0);
}

}  // namespace test
}  // namespace qrsdp
