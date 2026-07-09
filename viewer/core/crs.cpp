#include "viewer/core/crs.h"

#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>

#include <proj.h>

namespace met::core {
namespace {

// Runtime-registered PROJ data directory (see setProjDataPath). Written once at
// startup, read from every thread's ThreadProj ctor, so it is mutex-guarded.
std::mutex g_projDataMutex;
std::string g_projDataPath;

std::string projDataOverride() {
    std::lock_guard<std::mutex> lock(g_projDataMutex);
    return g_projDataPath;
}

// Per-thread PROJ context and a cache of normalized lon/lat<->projected
// transforms keyed by projection string. proj_normalize_for_visualization gives
// consistent (lon,lat) axis order in degrees on the geographic side.
struct ThreadProj {
    PJ_CONTEXT* ctx = nullptr;
    std::unordered_map<std::string, PJ*> cache;

    ThreadProj() {
        ctx = proj_context_create();
        // Ensure PROJ can find proj.db (needed to resolve EPSG:4326) even when
        // PROJ_DATA is not set in the environment. Precedence:
        //   1. PROJ_DATA / PROJ_LIB env vars (handled by PROJ itself) — honored
        //      by leaving the search path unset here.
        //   2. A path registered via setProjDataPath() — used by installed
        //      builds, resolved relative to the executable at startup.
        //   3. The compile-time MET_PROJ_DATA fallback — the dev/build-tree path.
        if (!std::getenv("PROJ_DATA") && !std::getenv("PROJ_LIB")) {
            const std::string override = projDataOverride();
#ifdef MET_PROJ_DATA
            const std::string path = override.empty() ? std::string(MET_PROJ_DATA) : override;
#else
            const std::string path = override;
#endif
            if (!path.empty()) {
                const char* p = path.c_str();
                proj_context_set_search_paths(ctx, 1, &p);
            }
        }
    }
    ~ThreadProj() {
        for (auto& [k, pj] : cache)
            if (pj) proj_destroy(pj);
        if (ctx) proj_context_destroy(ctx);
    }

    PJ* get(const std::string& proj) {
        auto it = cache.find(proj);
        if (it != cache.end()) return it->second;
        PJ* raw = proj_create_crs_to_crs(ctx, "EPSG:4326", proj.c_str(), nullptr);
        PJ* norm = raw ? proj_normalize_for_visualization(ctx, raw) : nullptr;
        if (raw) proj_destroy(raw);
        cache.emplace(proj, norm);
        return norm;
    }
};

ThreadProj& tls() {
    static thread_local ThreadProj instance;
    return instance;
}

}  // namespace

void setProjDataPath(std::string path) {
    std::lock_guard<std::mutex> lock(g_projDataMutex);
    g_projDataPath = std::move(path);
}

bool Crs::forward(double lon, double lat, double& x, double& y) const {
    PJ* pj = tls().get(proj_);
    if (!pj) return false;
    PJ_COORD c = proj_coord(lon, lat, 0, 0);
    PJ_COORD r = proj_trans(pj, PJ_FWD, c);
    x = r.xy.x;
    y = r.xy.y;
    return std::isfinite(x) && std::isfinite(y);
}

bool Crs::inverse(double x, double y, double& lon, double& lat) const {
    PJ* pj = tls().get(proj_);
    if (!pj) return false;
    PJ_COORD c = proj_coord(x, y, 0, 0);
    PJ_COORD r = proj_trans(pj, PJ_INV, c);
    lon = r.xy.x;
    lat = r.xy.y;
    return std::isfinite(lon) && std::isfinite(lat);
}

void Crs::forwardBatch(double* a, double* b, std::size_t n) const {
    PJ* pj = tls().get(proj_);
    if (!pj) {
        // Honor the documented contract: failed points become HUGE_VAL so callers
        // can detect them, rather than silently leaving the input unchanged.
        for (std::size_t i = 0; i < n; ++i) { a[i] = HUGE_VAL; b[i] = HUGE_VAL; }
        return;
    }
    // proj_trans_generic transforms in place over strided arrays.
    proj_trans_generic(pj, PJ_FWD, a, sizeof(double), n, b, sizeof(double), n, nullptr, 0, 0,
                       nullptr, 0, 0);
}

void Crs::inverseBatch(double* a, double* b, std::size_t n) const {
    PJ* pj = tls().get(proj_);
    if (!pj) {
        for (std::size_t i = 0; i < n; ++i) { a[i] = HUGE_VAL; b[i] = HUGE_VAL; }
        return;
    }
    proj_trans_generic(pj, PJ_INV, a, sizeof(double), n, b, sizeof(double), n, nullptr, 0, 0,
                       nullptr, 0, 0);
}

}  // namespace met::core
