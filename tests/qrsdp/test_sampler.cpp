#include <gtest/gtest.h>
#include "qrsdp/competing_intensity_sampler.h"
#include "qrsdp/mt19937_rng.h"
#include "qrsdp/records.h"
#include <cmath>
#include <cstddef>
#include <vector>

namespace qrsdp {
namespace test {

TEST(QrsdpSampler, ExponentialMean) {
    Mt19937Rng rng(12345);
    CompetingIntensitySampler sampler(rng);
    const double lambda = 50.0;
    const int N = 200000;
    double sum = 0.0;
    for (int i = 0; i < N; ++i) {
        sum += sampler.sampleDeltaT(lambda);
    }
    const double mean = sum / N;
    const double expected = 1.0 / lambda;
    const double relErr = std::fabs(mean - expected) / expected;
    EXPECT_LE(relErr, 0.05) << "mean " << mean << " expected " << expected << " relErr " << relErr;
}

TEST(QrsdpSampler, CategoricalRatios) {
    Mt19937Rng rng(67890);
    CompetingIntensitySampler sampler(rng);
    Intensities intens{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    const double total = intens.total();
    EXPECT_DOUBLE_EQ(total, 21.0);
    const int N = 200000;
    std::vector<int> counts(6, 0);
    for (int i = 0; i < N; ++i) {
        EventType t = sampler.sampleType(intens);
        counts[static_cast<int>(t)]++;
    }
    for (int k = 0; k < 6; ++k) {
        const double expected = (k + 1) / total;
        const double observed = static_cast<double>(counts[k]) / N;
        const double relErr = std::fabs(observed - expected) / expected;
        EXPECT_LE(relErr, 0.05) << "type " << k << " observed " << observed
                                << " expected " << expected << " relErr " << relErr;
    }
}

TEST(QrsdpSampler, DeterminismSameSeed) {
    Mt19937Rng rng1(42);
    Mt19937Rng rng2(42);
    CompetingIntensitySampler s1(rng1);
    CompetingIntensitySampler s2(rng2);
    Intensities intens{10.0, 20.0, 30.0, 40.0, 50.0, 60.0};
    for (int i = 0; i < 100; ++i) {
        EXPECT_DOUBLE_EQ(s1.sampleDeltaT(50.0), s2.sampleDeltaT(50.0));
        EXPECT_EQ(s1.sampleType(intens), s2.sampleType(intens));
    }
}

TEST(QrsdpSampler, DifferentSeedDifferentStream) {
    Mt19937Rng rng1(1);
    Mt19937Rng rng2(2);
    CompetingIntensitySampler s1(rng1);
    CompetingIntensitySampler s2(rng2);
    Intensities intens{1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    bool diff = false;
    for (int i = 0; i < 50 && !diff; ++i) {
        if (s1.sampleDeltaT(10.0) != s2.sampleDeltaT(10.0)) diff = true;
        if (s1.sampleType(intens) != s2.sampleType(intens)) diff = true;
    }
    EXPECT_TRUE(diff);
}

TEST(QrsdpSampler, DeltaTPositiveAndFinite) {
    Mt19937Rng rng(999);
    CompetingIntensitySampler sampler(rng);
    for (int i = 0; i < 1000; ++i) {
        double dt = sampler.sampleDeltaT(100.0);
        EXPECT_GT(dt, 0.0);
        EXPECT_TRUE(std::isfinite(dt));
    }
}

}  // namespace test
}  // namespace qrsdp
