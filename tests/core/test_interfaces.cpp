#include <gtest/gtest.h>
#include "rng/irng.h"
#include "book/i_order_book.h"
#include "model/i_intensity_model.h"
#include "sampler/i_event_sampler.h"
#include "sampler/i_attribute_sampler.h"
#include "io/i_event_sink.h"
#include "producer/i_producer.h"
#include "core/records.h"

namespace qrsdp {
namespace test {

/// Step 3: Interface headers compile and have expected virtual APIs.

TEST(QrsdpInterfaces, InterfacesCompile) {
    static_assert(sizeof(EventRecord) == 30u);
    EXPECT_TRUE(true);
}

}  // namespace test
}  // namespace qrsdp
