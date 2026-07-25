#include "viewer/core/field.h"

#include <atomic>

namespace met::core {

std::uint64_t nextFieldId() {
    // Relaxed is enough: the only requirement is that no two ids collide, which
    // fetch_add guarantees on its own. Decode jobs construct fields on worker
    // threads, so this must be atomic even though the consumers are GUI-thread.
    static std::atomic<std::uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace met::core
