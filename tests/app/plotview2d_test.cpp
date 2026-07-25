#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

#include <QApplication>
#include <QMouseEvent>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include "viewer/app/plotview2d.h"
#include "viewer/app/tilelayer.h"
#include "viewer/core/field.h"
#include "viewer/core/grid.h"

using namespace met;
using met::app::PlotView2D;
using met::app::TileLayer;

namespace {

void moveMouse(QWidget& w, QPointF pos) {
    QMouseEvent ev(QEvent::MouseMove, pos, w.mapToGlobal(pos), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
    QApplication::sendEvent(&w, &ev);
}

// The last probeMoved payload, captured with a plain connect (this Qt build has no
// QtTest module, so no QSignalSpy).
struct ProbeCatcher {
    bool got = false;
    double lat = 0, lon = 0, value = 0;
    bool hasValue = false;

    explicit ProbeCatcher(PlotView2D& view) {
        QObject::connect(&view, &PlotView2D::probeMoved, &view,
                         [this](double la, double lo, double v, bool has) {
                             got = true;
                             lat = la;
                             lon = lo;
                             value = v;
                             hasValue = has;
                         });
    }
    void clear() { got = false; }
};

// A HRRR-shaped Lambert field whose value equals its column index, so a probe's
// reported value identifies exactly which cell it sampled.
std::shared_ptr<core::Field2D> lambertRamp(int nx = 60, int ny = 40) {
    core::ProjectedGrid pg;
    pg.crs = core::Crs(
        "+proj=lcc +lat_1=38.5 +lat_2=38.5 +lat_0=38.5 +lon_0=-97.5 +R=6371229 +units=m +no_defs");
    pg.nx = nx;
    pg.ny = ny;
    pg.dx = 90000;
    pg.dy = 90000;
    (void)pg.crs.forward(-122.72, 21.14, pg.x0, pg.y0);

    auto f = std::make_shared<core::Field2D>();
    f->grid = pg;
    f->values.resize(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            f->values[static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
                      static_cast<std::size_t>(i)] = static_cast<float>(i);
    f->meta.units = "K";
    return f;
}

std::shared_ptr<core::Field2D> latLonRamp() {
    core::RegularLatLonGrid g;
    g.lat0 = 60;
    g.lon0 = -20;
    g.dlat = -1;
    g.dlon = 1;
    g.nlon = 40;
    g.nlat = 30;
    auto f = std::make_shared<core::Field2D>();
    f->grid = g;
    f->values.resize(40u * 30u);
    for (std::size_t j = 0; j < 30; ++j)
        for (std::size_t i = 0; i < 40; ++i) f->values[j * 40 + i] = static_cast<float>(i);
    f->meta.units = "K";
    return f;
}

}  // namespace

namespace {

// The drawn plot rect, recovered by finding where the field starts and stops along
// the widget's centre lines. plotRect() is private, and the point of these tests is
// to check the readout against the raster's geometry rather than to trust either.
QRectF measurePlotRect(PlotView2D& view, ProbeCatcher& probe) {
    auto span = [&](bool horizontal) {
        double lo = -1, hi = -1;
        const int n = horizontal ? view.width() : view.height();
        for (int k = 0; k < n; ++k) {
            probe.clear();
            moveMouse(view, horizontal ? QPointF(k, view.height() / 2.0)
                                       : QPointF(view.width() / 2.0, k));
            if (!probe.got || !probe.hasValue) continue;
            if (lo < 0) lo = k;
            hi = k;
        }
        return std::pair{lo, hi};
    };
    const auto [x0, x1] = span(true);
    const auto [y0, y1] = span(false);
    return QRectF(x0, y0, x1 - x0, y1 - y0);
}

}  // namespace

// The raster is drawn in flat grid-index space: column i occupies a fixed fraction
// of the plot rect. So the value read at a pixel must be the value of the column
// that pixel draws. Deriving the readout from a linear lat/lon interpolation over
// the grid's bounding box instead — which for a projected grid is a geographic
// *envelope*, not the grid — decouples the two and puts the corners of an HRRR
// domain over 11 degrees of longitude away from what is on screen.
TEST(PlotView2D, ProbeOnProjectedGridReadsTheColumnDrawnAtThatPixel) {
    PlotView2D view;
    view.resize(600, 480);
    auto field = lambertRamp();  // value == column index
    view.setField(field);

    ProbeCatcher probe(view);
    const QRectF rect = measurePlotRect(view, probe);
    ASSERT_GT(rect.width(), 100);
    ASSERT_GT(rect.height(), 100);

    const int nx = core::gridWidth(field->grid);
    int checked = 0;
    for (double fx : {0.02, 0.1, 0.3, 0.5, 0.7, 0.9, 0.98}) {
        for (double fy : {0.1, 0.5, 0.9}) {
            const QPointF pos(rect.left() + rect.width() * fx, rect.top() + rect.height() * fy);
            probe.clear();
            moveMouse(view, pos);
            if (!probe.got || !probe.hasValue) continue;
            // The raster maps column 0..nx-1 evenly across the rect (dx > 0, so no
            // flip), and the field's value is its column index.
            const double expectedColumn = fx * (nx - 1);
            EXPECT_NEAR(probe.value, expectedColumn, 0.5)
                << "readout disagrees with the drawn raster at rect fraction " << fx << "," << fy;
            ++checked;
        }
    }
    EXPECT_GT(checked, 15) << "the sweep must actually land on the field";
}

// The same invariant on a regular lat/lon grid, where index space really is linear
// in lat/lon — this is the case the old mapping got right, and it must stay right.
TEST(PlotView2D, ProbeOnLatLonGridReadsTheColumnDrawnAtThatPixel) {
    PlotView2D view;
    view.resize(600, 480);
    auto field = latLonRamp();
    view.setField(field);

    ProbeCatcher probe(view);
    const QRectF rect = measurePlotRect(view, probe);
    ASSERT_GT(rect.width(), 100);

    const int nx = core::gridWidth(field->grid);
    for (double fx : {0.05, 0.25, 0.5, 0.75, 0.95}) {
        probe.clear();
        moveMouse(view, QPointF(rect.left() + rect.width() * fx, rect.center().y()));
        ASSERT_TRUE(probe.got && probe.hasValue) << "at rect fraction " << fx;
        EXPECT_NEAR(probe.value, fx * (nx - 1), 0.5);
        // And the reported position must be the one that column actually sits at.
        const core::GridIndex gi =
            core::latlonToIndex(field->grid, core::LatLon{probe.lat, probe.lon});
        ASSERT_TRUE(gi.inDomain);
        EXPECT_NEAR(gi.x, probe.value, 0.05);
    }
}

// The reported lat/lon and the reported value must describe the same place: the
// position resolves back to the cell the value came from.
TEST(PlotView2D, ProbePositionAndValueAgreeOnAProjectedGrid) {
    PlotView2D view;
    view.resize(600, 480);
    auto field = lambertRamp();
    view.setField(field);

    ProbeCatcher probe(view);
    const QRectF rect = measurePlotRect(view, probe);
    for (double fx : {0.1, 0.5, 0.9}) {
        for (double fy : {0.1, 0.5, 0.9}) {
            probe.clear();
            moveMouse(view,
                      QPointF(rect.left() + rect.width() * fx, rect.top() + rect.height() * fy));
            if (!probe.got || !probe.hasValue) continue;
            const core::GridIndex gi =
                core::latlonToIndex(field->grid, core::LatLon{probe.lat, probe.lon});
            ASSERT_TRUE(gi.inDomain) << "reported position fell outside the grid";
            EXPECT_NEAR(gi.x, probe.value, 0.05);
        }
    }
}

// The plot rect keeps the data's true aspect. For a projected grid that is the
// physical extent in metres; using the lat/lon envelope stretched a square domain.
TEST(PlotView2D, ProjectedGridUsesItsPhysicalAspectNotTheLatLonEnvelope) {
    PlotView2D view;
    view.resize(900, 900);
    // A square domain in projected metres: 40x40 cells at equal spacing.
    view.setField(lambertRamp(40, 40));

    ProbeCatcher probe(view);
    // Walk the horizontal and vertical centre lines and record where data starts
    // and stops, which brackets the drawn plot rect.
    auto extent = [&](bool horizontal) {
        double lo = -1, hi = -1;
        for (int k = 0; k < 900; ++k) {
            probe.clear();
            moveMouse(view, horizontal ? QPointF(k, 450) : QPointF(450, k));
            if (!probe.got || !probe.hasValue) continue;
            if (lo < 0) lo = k;
            hi = k;
        }
        return hi - lo;
    };
    const double w = extent(true);
    const double h = extent(false);
    ASSERT_GT(w, 0);
    ASSERT_GT(h, 0);
    EXPECT_NEAR(w / h, 1.0, 0.1) << "a square projected domain must render square";
}

TEST(TileLayer, RejectsUnusableCustomUrlTemplates) {
    EXPECT_TRUE(TileLayer::isValidUrlTemplate("https://tile.example.org/{z}/{x}/{y}.png"));
    EXPECT_TRUE(TileLayer::isValidUrlTemplate("http://example.org/tiles?z={z}&x={x}&y={y}"));
    // Missing placeholders.
    EXPECT_FALSE(TileLayer::isValidUrlTemplate("https://tile.example.org/{z}/{x}.png"));
    EXPECT_FALSE(TileLayer::isValidUrlTemplate("https://tile.example.org/static.png"));
    // Non-http schemes: a pasted file:// URL must not turn the basemap box into a
    // local-file reader.
    EXPECT_FALSE(TileLayer::isValidUrlTemplate("file:///etc/{z}/{x}/{y}"));
    EXPECT_FALSE(TileLayer::isValidUrlTemplate("ftp://example.org/{z}/{x}/{y}"));
    EXPECT_FALSE(TileLayer::isValidUrlTemplate(""));
    EXPECT_FALSE(TileLayer::isValidUrlTemplate("{z}/{x}/{y}.png"));  // no host
}

TEST(TileLayer, BuiltinSourcesAreAllValidTemplates) {
    const auto sources = TileLayer::builtinSources();
    EXPECT_GE(sources.size(), 5);
    for (const auto& s : sources) {
        EXPECT_TRUE(TileLayer::isValidUrlTemplate(s.urlTemplate)) << s.name.toStdString();
        EXPECT_FALSE(s.attribution.isEmpty()) << s.name.toStdString();
    }
}
