#pragma once

#include <memory>
#include <vector>

#include <QPointF>
#include <QString>

namespace met::app {

// A polyline of (lon, lat) vertices in degrees.
using GeoPolyline = std::vector<QPointF>;

// Load coastline polylines from the met-viewer binary format (magic "MVCL").
// `path` may be a Qt resource path (":/...") or a filesystem path. Returns null
// on any error (missing file, bad magic) so the overlay is simply skipped.
std::shared_ptr<std::vector<GeoPolyline>> loadCoastlines(const QString& path);

}  // namespace met::app
