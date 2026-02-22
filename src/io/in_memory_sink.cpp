#include "io/in_memory_sink.h"

namespace qrsdp {

void InMemorySink::append(const EventRecord& rec) {
    events_.push_back(rec);
}

}  // namespace qrsdp
