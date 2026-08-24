#include "viewer/render/warp.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

#include "viewer/render/tilemath.h"

namespace met::render {
namespace {

using core::ProjectedGrid;
using core::RegularLatLonGrid;

// NaN-aware bilinear sample at fractional (x, y). Inlined here to keep met_render
// independent of met_analysis. Returns NaN if all corners are missing.
float sampleBilinear(const core::Field2D& f, double x, double y, bool wrapX = false) {
    const int w = f.width();
    const int h = f.height();
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    // On a global-wrap grid the east neighbor of the last column is column 0, so
    // the ±180° seam interpolates instead of showing a one-cell clamp artifact.
    int x1 = x0 + 1;
    if (x1 >= w) x1 = wrapX ? 0 : w - 1;
    const int y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0;
    const double fy = y - y0;

    const float v00 = f.at(x0, y0);
    const float v10 = f.at(x1, y0);
    const float v01 = f.at(x0, y1);
    const float v11 = f.at(x1, y1);

    double acc = 0.0, wsum = 0.0, bestW = -1.0;
    float best = std::numeric_limits<float>::quiet_NaN();
    auto add = [&](float v, double weight) {
        if (!std::isnan(v)) {
            acc += weight * v;
            wsum += weight;
            if (weight > bestW) {
                bestW = weight;
                best = v;
            }
        }
    };
    add(v00, (1 - fx) * (1 - fy));
    add(v10, fx * (1 - fy));
    add(v01, (1 - fx) * fy);
    add(v11, fx * fy);
    if (wsum <= 0.0) return std::numeric_limits<float>::quiet_NaN();
    return wsum > 0.999 ? static_cast<float>(acc / wsum) : best;
}

// Write one colormapped, opacity-scaled, premultiplied pixel.
inline void writePixel(QRgb* scan, int px, float v, const Colormap& cmap, double alphaScale) {
    if (std::isnan(v)) return;
    const Rgba c = cmap.map(v);
    if (c.a == 0) return;
    const double a = (c.a / 255.0) * alphaScale;
    scan[px] =
        qRgba(static_cast<int>(std::lround(c.r * a)), static_cast<int>(std::lround(c.g * a)),
              static_cast<int>(std::lround(c.b * a)), static_cast<int>(std::lround(a * 255.0)));
}

// A process-wide pool of parked worker threads for row chunks.
//
// This used to spawn and join a fresh std::thread per warp call. That is correct,
// and invisible at release rate, but a warp runs on every zoom step, every pan
// that leaves the blit region, and every colormap or field change — so during a
// fast zoom the create/join cost lands over and over in the interactive path.
// Parking the threads moves that cost to the first warp of the process.
//
// The pool is deliberately never destroyed. Joining workers from a static
// destructor buys nothing at process exit and risks racing the rest of static
// teardown; parked threads are reclaimed when the process goes away.
class RowPool {
public:
    static RowPool& instance() {
        static RowPool* pool = new RowPool();
        return *pool;
    }

    // Runs body(i) for i in [0, count). Chunk 0 runs on the calling thread, the
    // rest on workers; returns once every chunk has finished.
    void run(int count, const std::function<void(int)>& body) {
        // A warp invoked from inside a pool worker runs serially rather than
        // waiting on the pool: a nested batch could otherwise wait on workers that
        // are themselves blocked waiting on it.
        if (count <= 1 || workers_.empty() || isWorker_) {
            for (int i = 0; i < count; ++i) body(i);
            return;
        }

        Batch batch{&body, count - 1, {}, {}};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (int i = 1; i < count; ++i) queue_.push_back(Task{&batch, i});
        }
        cv_.notify_all();

        body(0);

        std::unique_lock<std::mutex> lock(batch.mutex);
        batch.cv.wait(lock, [&batch] { return batch.remaining == 0; });
    }

private:
    struct Batch {
        const std::function<void(int)>* body;
        int remaining;
        std::mutex mutex;
        std::condition_variable cv;
    };
    struct Task {
        Batch* batch;
        int index;
    };

    RowPool() {
        const unsigned hw = std::thread::hardware_concurrency();
        const int n = static_cast<int>(hw > 1 ? hw - 1 : 1u);
        workers_.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) workers_.emplace_back([this] { workerLoop(); });
    }

    void workerLoop() {
        isWorker_ = true;
        for (;;) {
            Task task{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !queue_.empty(); });
                task = queue_.front();
                queue_.pop_front();
            }
            try {
                (*task.batch->body)(task.index);
            } catch (...) {
                // A chunk that throws still has to release its batch or the caller
                // waits forever. Warp bodies do not throw; this is the backstop.
            }
            {
                std::lock_guard<std::mutex> lock(task.batch->mutex);
                --task.batch->remaining;
            }
            task.batch->cv.notify_one();
        }
    }

    std::vector<std::thread> workers_;
    std::deque<Task> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    static thread_local bool isWorker_;
};

thread_local bool RowPool::isWorker_ = false;

// Run `body(y0, y1)` over the row range, single- or multi-threaded.
template <typename F>
void runRows(int height, int threads, F&& body) {
    if (threads <= 1 || height <= 1) {
        body(0, height);
        return;
    }
    const int n = std::min(threads, height);
    const int chunk = (height + n - 1) / n;
    const std::function<void(int)> chunkBody = [&body, chunk, height](int t) {
        const int y0 = t * chunk;
        const int y1 = std::min(y0 + chunk, height);
        if (y0 < y1) body(y0, y1);
    };
    RowPool::instance().run(n, chunkBody);
}

// Regular lat/lon path: fractional column depends only on x, row only on y, so
// both are precomputed once and the inner loop is a bilinear gather + LUT.
void warpRegular(const core::Field2D& field, const RegularLatLonGrid& g, const Colormap& cmap,
                 const MercatorViewport& view, double alphaScale, int threads, QImage& img) {
    std::vector<double> fxCol(static_cast<std::size_t>(view.width));
    std::vector<unsigned char> okCol(static_cast<std::size_t>(view.width), 0);
    for (int px = 0; px < view.width; ++px) {
        const double lon = tile::worldXToLon(view.topLeftWorldX + px + 0.5, view.zoom);
        double delta = lon - g.lon0;
        while (delta > 180.0) delta -= 360.0;
        while (delta <= -180.0) delta += 360.0;
        double fx = g.dlon != 0.0 ? delta / g.dlon : 0.0;
        bool ok = g.globalWrapLon ? true : (fx >= 0.0 && fx <= g.nlon - 1.0);
        if (g.globalWrapLon && g.nlon > 0) {
            const double n = g.nlon;
            fx = std::fmod(std::fmod(fx, n) + n, n);
        }
        fxCol[static_cast<std::size_t>(px)] = fx;
        okCol[static_cast<std::size_t>(px)] = ok ? 1 : 0;
    }
    std::vector<double> fyRow(static_cast<std::size_t>(view.height));
    std::vector<unsigned char> okRow(static_cast<std::size_t>(view.height), 0);
    for (int py = 0; py < view.height; ++py) {
        const double lat = tile::worldYToLat(view.topLeftWorldY + py + 0.5, view.zoom);
        const double fy = g.dlat != 0.0 ? (lat - g.lat0) / g.dlat : 0.0;
        fyRow[static_cast<std::size_t>(py)] = fy;
        okRow[static_cast<std::size_t>(py)] = (fy >= 0.0 && fy <= g.nlat - 1.0) ? 1 : 0;
    }

    runRows(view.height, threads, [&](int y0, int y1) {
        for (int py = y0; py < y1; ++py) {
            if (!okRow[static_cast<std::size_t>(py)]) continue;
            const double fy = fyRow[static_cast<std::size_t>(py)];
            auto* scan = reinterpret_cast<QRgb*>(img.scanLine(py));
            for (int px = 0; px < view.width; ++px) {
                if (!okCol[static_cast<std::size_t>(px)]) continue;
                writePixel(
                    scan, px,
                    sampleBilinear(field, fxCol[static_cast<std::size_t>(px)], fy, g.globalWrapLon),
                    cmap, alphaScale);
            }
        }
    });
}

// Projected path: invert each output pixel's Mercator position to lon/lat (cheap
// closed form), batch-project lon/lat to grid coordinates through PROJ, then
// gather. This is the per-viewport mapping cache for arbitrary projections.
//
// The PROJ batch is the dominant cost (roughly two thirds of the total on a
// 1080p Lambert warp), so it runs inside the row-chunk parallel region rather
// than once up front — Crs caches its PJ per thread, so each worker transforms
// its own slice independently. Keeping the buffers per chunk also drops the peak
// allocation from two viewport-sized double arrays (~33 MB at 1080p) to a slice.
void warpProjected(const core::Field2D& field, const ProjectedGrid& g, const Colormap& cmap,
                   const MercatorViewport& view, double alphaScale, int threads, QImage& img) {
    // Longitude depends only on column, latitude only on row: precompute both.
    std::vector<double> lonCol(static_cast<std::size_t>(view.width));
    for (int px = 0; px < view.width; ++px)
        lonCol[static_cast<std::size_t>(px)] =
            tile::worldXToLon(view.topLeftWorldX + px + 0.5, view.zoom);
    std::vector<double> latRow(static_cast<std::size_t>(view.height));
    for (int py = 0; py < view.height; ++py)
        latRow[static_cast<std::size_t>(py)] =
            tile::worldYToLat(view.topLeftWorldY + py + 0.5, view.zoom);

    const std::size_t stride = static_cast<std::size_t>(view.width);
    runRows(view.height, threads, [&](int y0, int y1) {
        const std::size_t rows = static_cast<std::size_t>(y1 - y0);
        std::vector<double> a(rows * stride), b(rows * stride);  // lon/lat -> x/y in place
        for (int py = y0; py < y1; ++py) {
            const std::size_t row = static_cast<std::size_t>(py - y0) * stride;
            const double lat = latRow[static_cast<std::size_t>(py)];
            for (int px = 0; px < view.width; ++px) {
                a[row + static_cast<std::size_t>(px)] = lonCol[static_cast<std::size_t>(px)];
                b[row + static_cast<std::size_t>(px)] = lat;
            }
        }
        g.crs.forwardBatch(a.data(), b.data(), rows * stride);  // now a=x, b=y (metres)

        for (int py = y0; py < y1; ++py) {
            const std::size_t row = static_cast<std::size_t>(py - y0) * stride;
            auto* scan = reinterpret_cast<QRgb*>(img.scanLine(py));
            for (int px = 0; px < view.width; ++px) {
                const double x = a[row + static_cast<std::size_t>(px)];
                const double y = b[row + static_cast<std::size_t>(px)];
                if (!std::isfinite(x) || !std::isfinite(y)) continue;
                const double fx = g.dx != 0.0 ? (x - g.x0) / g.dx : 0.0;
                const double fy = g.dy != 0.0 ? (y - g.y0) / g.dy : 0.0;
                if (fx < 0.0 || fx > g.nx - 1.0 || fy < 0.0 || fy > g.ny - 1.0) continue;
                writePixel(scan, px, sampleBilinear(field, fx, fy), cmap, alphaScale);
            }
        }
    });
}

}  // namespace

QImage warpToMercator(const core::Field2D& field, const Colormap& cmap,
                      const MercatorViewport& view, double opacity, int threads) {
    QImage img(view.width, view.height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (view.width <= 0 || view.height <= 0) return img;

    const double alphaScale = std::clamp(opacity, 0.0, 1.0);
    std::visit(
        [&](const auto& grid) {
            using T = std::decay_t<decltype(grid)>;
            if constexpr (std::is_same_v<T, RegularLatLonGrid>)
                warpRegular(field, grid, cmap, view, alphaScale, threads, img);
            else warpProjected(field, grid, cmap, view, alphaScale, threads, img);
        },
        field.grid);
    return img;
}

}  // namespace met::render
