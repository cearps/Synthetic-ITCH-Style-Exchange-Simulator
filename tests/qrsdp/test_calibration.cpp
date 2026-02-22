#include <gtest/gtest.h>
#include "qrsdp/event_log_parser.h"
#include "qrsdp/intensity_estimator.h"
#include "qrsdp/intensity_curve_io.h"
#include "qrsdp/records.h"
#include "qrsdp/event_types.h"
#include <cstdio>
#include <string>

namespace qrsdp {
namespace test {

TEST(EventLogParser, ResetAndPush) {
    EventLogParser parser;
    parser.reset();
    EXPECT_EQ(parser.event_count, 0u);
    EventRecord rec{};
    rec.type = static_cast<uint8_t>(EventType::ADD_BID);
    rec.side = 0;
    rec.price_ticks = 9999;
    rec.qty = 1;
    EXPECT_TRUE(parser.push(rec));
    EXPECT_EQ(parser.event_count, 1u);
    EXPECT_EQ(parser.best_bid_ticks, 9999);
}

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
    const std::string path = "/tmp/test_curve_io.json";
    ASSERT_TRUE(saveCurveToJson(path, c));
    IntensityCurve loaded;
    ASSERT_TRUE(loadCurveFromJson(path, loaded));
    EXPECT_EQ(loaded.nMax(), 2u);
    EXPECT_DOUBLE_EQ(loaded.value(0), 1.0);
    EXPECT_DOUBLE_EQ(loaded.value(1), 2.0);
    EXPECT_DOUBLE_EQ(loaded.value(2), 3.0);
    std::remove(path.c_str());
}

}  // namespace test
}  // namespace qrsdp
