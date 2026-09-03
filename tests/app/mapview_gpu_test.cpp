#include <gtest/gtest.h>

#include <cstddef>
#include <memory>

#include <QImage>

#include "viewer/app/mapview.h"
#include "viewer/app/tilelayer.h"
#include "viewer/core/field.h"
#include "viewer/core/grid.h"

using namespace met;
using met::app::MapView;
using met::app::TileLayer;

// The GPU field path ships off by default and is opt-in from the control panel, so
// nothing in the normal test run touches it. That is exactly how an off-by-default
// path rots: it keeps compiling long after it stopped working.
//
// These tests cannot exercise the shader itself — the headless platform plugin has
// no GL context, so glReady_ stays false — but they do pin the contract that
// matters for a fallback path: turning it on must never crash, and must never
// change what is drawn when the GPU is unavailable. A real GL check remains a
// manual step; see the note in Design.md.
namespace {

std::shared_ptr<core::Field2D> makeField(int nx, int ny) {
    auto f = std::make_shared<core::Field2D>();
    core::RegularLatLonGrid g;
    g.lat0 = 20.0;
    g.lon0 = 240.0;
    g.dlat = 0.5;
    g.dlon = 0.5;
    g.nlat = ny;
    g.nlon = nx;
    f->grid = g;
    f->values.resize(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            f->values[static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
                      static_cast<std::size_t>(i)] = static_cast<float>(273.0 + 0.5 * i + 0.3 * j);
    f->meta.varName = "t";
    f->meta.units = "K";
    return f;
}

QImage renderWith(bool gpuEnabled) {
    TileLayer tiles;
    MapView view(&tiles);
    view.resize(640, 480);
    view.setGpuEnabled(gpuEnabled);
    view.setField(makeField(40, 30));
    view.setColormapByName("viridis");
    return view.grab().toImage();
}

}  // namespace

TEST(MapViewGpu, EnablingGpuPathDoesNotCrash) {
    const QImage img = renderWith(true);
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.width(), 640);
    EXPECT_EQ(img.height(), 480);
}

// Without a GL context the GPU branch must decline and hand off to the CPU warp,
// producing a byte-identical raster. If this ever diverges, the fallback is no
// longer a fallback.
TEST(MapViewGpu, FallsBackToIdenticalCpuRasterWithoutGl) {
    const QImage gpuOn = renderWith(true);
    const QImage gpuOff = renderWith(false);
    ASSERT_FALSE(gpuOn.isNull());
    ASSERT_FALSE(gpuOff.isNull());
    EXPECT_EQ(gpuOn, gpuOff);
}

// The toggle is a plain setter that must be safe in any order relative to setField,
// including flipping back and forth with a field already loaded.
TEST(MapViewGpu, ToggleIsSafeInEitherOrder) {
    TileLayer tiles;
    MapView view(&tiles);
    view.resize(320, 240);
    view.setField(makeField(20, 16));
    for (int i = 0; i < 3; ++i) {
        view.setGpuEnabled(true);
        EXPECT_FALSE(view.grab().toImage().isNull());
        view.setGpuEnabled(false);
        EXPECT_FALSE(view.grab().toImage().isNull());
    }
}
