#include <gtest/gtest.h>
#include "model/simple_imbalance_intensity.h"
#include "core/records.h"
#include <cmath>

namespace qrsdp {
namespace test {

TEST(QrsdpIntensity, AllIntensitiesNonNegative) {
    IntensityParams p{10.0, 0.1, 5.0, 1.0, 1.0, 0.05};
    SimpleImbalanceIntensity model(p);

    BookState state;
    state.features = BookFeatures{9999, 10001, 50, 50, 2, 0.0};
    Intensities i = model.compute(state);
    EXPECT_GE(i.add_bid, 1e-9);
    EXPECT_GE(i.add_ask, 1e-9);
    EXPECT_GE(i.cancel_bid, 1e-9);
    EXPECT_GE(i.cancel_ask, 1e-9);
    EXPECT_GE(i.exec_buy, 1e-9);
    EXPECT_GE(i.exec_sell, 1e-9);
    EXPECT_GT(i.total(), 0.0);
}

TEST(QrsdpIntensity, BalancedBookGivesSymmetricAdds) {
    IntensityParams p{20.0, 0.1, 5.0, 1.0, 1.0, 0.05};
    SimpleImbalanceIntensity model(p);
    BookState state;
    state.features = BookFeatures{9999, 10001, 50, 50, 2, 0.0};
    Intensities i = model.compute(state);
    EXPECT_DOUBLE_EQ(i.add_bid, i.add_ask);
}

TEST(QrsdpIntensity, PositiveImbalanceIncreasesAddAskDecreasesAddBid) {
    IntensityParams p{20.0, 0.1, 5.0, 1.0, 1.0, 0.05};
    SimpleImbalanceIntensity model(p);
    BookState stateBal, stateBid;
    stateBal.features = BookFeatures{9999, 10001, 50, 50, 2, 0.0};
    stateBid.features = BookFeatures{9999, 10001, 80, 20, 2, 0.6};
    Intensities iBal = model.compute(stateBal);
    Intensities iBid = model.compute(stateBid);
    EXPECT_GT(iBid.add_ask, iBal.add_ask);
    EXPECT_LT(iBid.add_bid, iBal.add_bid);
}

TEST(QrsdpIntensity, CancelProportionalToQueueSize) {
    IntensityParams p{10.0, 0.5, 5.0, 1.0, 1.0, 0.05};
    SimpleImbalanceIntensity model(p);
    BookState stateSmall, stateLarge;
    stateSmall.features = BookFeatures{9999, 10001, 10, 10, 2, 0.0};
    stateLarge.features = BookFeatures{9999, 10001, 100, 100, 2, 0.0};
    Intensities iSmall = model.compute(stateSmall);
    Intensities iLarge = model.compute(stateLarge);
    EXPECT_GT(iLarge.cancel_bid, iSmall.cancel_bid);
    EXPECT_GT(iLarge.cancel_ask, iSmall.cancel_ask);
}

TEST(QrsdpIntensity, NoNanForExtremeImbalance) {
    IntensityParams p{10.0, 0.1, 5.0, 1.0, 1.0, 0.05};
    SimpleImbalanceIntensity model(p);
    BookState state;
    state.features = BookFeatures{9999, 10001, 1, 99, 2, 0.98};
    Intensities i = model.compute(state);
    EXPECT_FALSE(std::isnan(i.add_bid));
    EXPECT_FALSE(std::isnan(i.add_ask));
    EXPECT_FALSE(std::isnan(i.total()));
}

TEST(QrsdpIntensity, InterfaceComputeBookState) {
    IntensityParams p{10.0, 0.1, 5.0, 1.0, 1.0, 0.05};
    SimpleImbalanceIntensity impl(p);
    IIntensityModel& model = impl;
    BookState state;
    state.features = BookFeatures{9999, 10001, 50, 50, 2, 0.0};
    Intensities i = model.compute(state);
    EXPECT_GT(i.total(), 0.0);
    EXPECT_DOUBLE_EQ(i.add_bid, i.add_ask);
}

}  // namespace test
}  // namespace qrsdp
