#include "viewer/core/core.h"

#include <fmt/format.h>
#include <proj.h>

namespace met::core {

int placeholder() {
    // Smoke-test that PROJ and fmt actually link through the vcpkg toolchain.
    PJ_CONTEXT* ctx = proj_context_create();
    proj_context_destroy(ctx);

    return static_cast<int>(fmt::format("met_core {}", 0).size());
}

}  // namespace met::core
