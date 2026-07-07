#include "viewer/app/coastlines.h"

#include <cstring>

#include <QDataStream>
#include <QFile>

namespace met::app {

std::shared_ptr<std::vector<GeoPolyline>> loadCoastlines(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return nullptr;
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 12 || std::memcmp(bytes.constData(), "MVCL", 4) != 0) return nullptr;

    QDataStream in(bytes);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);
    in.skipRawData(4);  // magic

    quint32 version = 0, nLines = 0;
    in >> version >> nLines;
    if (version != 1) return nullptr;

    auto out = std::make_shared<std::vector<GeoPolyline>>();
    out->reserve(nLines);
    for (quint32 i = 0; i < nLines; ++i) {
        quint32 nPts = 0;
        in >> nPts;
        if (in.status() != QDataStream::Ok) return nullptr;
        GeoPolyline line;
        line.reserve(nPts);
        for (quint32 j = 0; j < nPts; ++j) {
            float lon = 0, lat = 0;
            in >> lon >> lat;
            line.emplace_back(lon, lat);
        }
        out->push_back(std::move(line));
    }
    if (in.status() != QDataStream::Ok) return nullptr;
    return out;
}

}  // namespace met::app
