// Point-profile picking on the map: the click that produces a coordinate, and
// the marker that stays behind.
//
// What is NOT covered here, deliberately: the marker's drawing. MapView is a
// QOpenGLWidget and paintGL never runs under the headless "minimal" platform
// plugin the suite uses, so a pixel assertion would pass whether or not the
// crosshair were ever drawn. Saying that is more useful than a test that is
// vacuously green. Its state is covered; its appearance is checked by hand.

#include <gtest/gtest.h>

#include <optional>

#include <QApplication>
#include <QMouseEvent>
#include <QObject>

#include "viewer/app/mapview.h"
#include "viewer/app/tilelayer.h"

using namespace met;
using namespace met::app;

namespace {

// Deliver a synthetic left click at widget coordinates. The views read
// QMouseEvent::position(), so the local position is what matters.
void clickAt(QWidget& w, QPointF pos) {
    QMouseEvent press(QEvent::MouseButtonPress, pos, w.mapToGlobal(pos), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &press);
}

}  // namespace

TEST(MapViewPick, EmitsPointPickedWithTheClickedCoordinateInPointMode) {
    TileLayer tiles;
    MapView view(&tiles);
    view.resize(400, 300);
    view.setInteractionMode(MapView::Mode::Point);

    std::optional<core::LatLon> got;
    QObject::connect(&view, &MapView::pointPicked, [&got](core::LatLon ll) { got = ll; });

    clickAt(view, QPointF(200, 150));
    ASSERT_TRUE(got.has_value());
    const core::LatLon centre = *got;
    EXPECT_GE(centre.lat, -90.0);
    EXPECT_LE(centre.lat, 90.0);

    // Pin the mapping's orientation rather than the default view's centre, which
    // is not this test's business: right is east, and down is south.
    clickAt(view, QPointF(300, 150));
    ASSERT_TRUE(got.has_value());
    EXPECT_GT(got->lon, centre.lon);
    EXPECT_NEAR(got->lat, centre.lat, 1e-9);

    clickAt(view, QPointF(200, 250));
    EXPECT_LT(got->lat, centre.lat);
    EXPECT_NEAR(got->lon, centre.lon, 1e-9);
}

TEST(MapViewPick, DoesNotPickWhilePanning) {
    TileLayer tiles;
    MapView view(&tiles);
    view.resize(400, 300);
    view.setInteractionMode(MapView::Mode::Pan);

    int picks = 0;
    QObject::connect(&view, &MapView::pointPicked, [&picks](core::LatLon) { ++picks; });

    clickAt(view, QPointF(200, 150));
    EXPECT_EQ(picks, 0);
}

// The cross-section path is cleared on a mode change because a half-drawn path
// is meaningless once you leave that mode. A picked point is not: the usual
// thing to do next is switch back to Pan and look at the field around the site.
TEST(MapViewPick, KeepsTheMarkerWhenTheModeChangesBackToPan) {
    TileLayer tiles;
    MapView view(&tiles);
    view.setPickedPoint(core::LatLon{45.0, -100.0});
    ASSERT_TRUE(view.pickedPoint().has_value());

    view.setInteractionMode(MapView::Mode::Pan);
    ASSERT_TRUE(view.pickedPoint().has_value());
    EXPECT_NEAR(view.pickedPoint()->lat, 45.0, 1e-9);

    view.setInteractionMode(MapView::Mode::CrossSection);
    EXPECT_TRUE(view.pickedPoint().has_value());
}

TEST(MapViewPick, ClearsTheMarkerWhenAskedTo) {
    TileLayer tiles;
    MapView view(&tiles);
    view.setPickedPoint(core::LatLon{45.0, -100.0});
    view.setPickedPoint(std::nullopt);
    EXPECT_FALSE(view.pickedPoint().has_value());
}
