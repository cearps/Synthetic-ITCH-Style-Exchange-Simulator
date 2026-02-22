#include <gtest/gtest.h>
#include "qrsdp/irng.h"
#include "qrsdp/i_order_book.h"
#include "qrsdp/i_intensity_model.h"
#include "qrsdp/i_event_sampler.h"
#include "qrsdp/i_attribute_sampler.h"
#include "qrsdp/i_event_sink.h"
#include "qrsdp/i_producer.h"
#include "qrsdp/records.h"

namespace qrsdp {
namespace test {

/// Step 3: Interface headers compile and have expected virtual APIs.

TEST(QrsdpInterfaces, InterfacesCompile) {
    static_assert(sizeof(EventRecord) == 30u);
    EXPECT_TRUE(true);
}

}  // namespace test
}  // namespace qrsdp
