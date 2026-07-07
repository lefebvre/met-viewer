#include "viewer/app/tilelayer.h"

#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace met::app {

TileLayer::TileLayer(QObject* parent) : QObject(parent), memory_(512) {
    nam_ = new QNetworkAccessManager(this);

    // Persistent disk cache so tiles survive restarts and we respect servers.
    auto* disk = new QNetworkDiskCache(this);
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tiles";
    QDir().mkpath(dir);
    disk->setCacheDirectory(dir);
    disk->setMaximumCacheSize(512LL * 1024 * 1024);  // 512 MB
    nam_->setCache(disk);

    connect(nam_, &QNetworkAccessManager::finished, this, &TileLayer::onFinished);
    setSource(builtinSources().front());
}

QList<TileSource> TileLayer::builtinSources() {
    return {
        {"OpenStreetMap", "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
         "© OpenStreetMap contributors", 19},
        {"Carto Light", "https://basemaps.cartocdn.com/light_all/{z}/{x}/{y}.png",
         "© OpenStreetMap contributors © CARTO", 20},
        {"Carto Dark", "https://basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png",
         "© OpenStreetMap contributors © CARTO", 20},
        {"Esri World Imagery",
         "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
         "Esri, Maxar, Earthstar Geographics", 19},
        {"OpenTopoMap", "https://a.tile.opentopomap.org/{z}/{x}/{y}.png",
         "© OpenStreetMap contributors, SRTM | © OpenTopoMap", 17},
    };
}

void TileLayer::setSource(const TileSource& source) {
    source_ = source;
    memory_.clear();
    inFlight_.clear();
    pending_.clear();
    pendingUrls_.clear();
}

QString TileLayer::keyOf(int z, int x, int y) const {
    return QStringLiteral("%1/%2/%3").arg(z).arg(x).arg(y);
}

QString TileLayer::urlFor(int z, int x, int y) const {
    QString u = source_.urlTemplate;
    u.replace("{z}", QString::number(z));
    u.replace("{x}", QString::number(x));
    u.replace("{y}", QString::number(y));
    return u;
}

QImage TileLayer::tile(int z, int x, int y) {
    const int n = 1 << z;
    if (z < 0 || x < 0 || y < 0 || x >= n || y >= n) return {};

    const QString key = keyOf(z, x, y);
    if (QImage* img = memory_.object(key)) return *img;

    if (!inFlight_.contains(key) && !pendingUrls_.contains(key)) {
        pendingUrls_.insert(key, urlFor(z, x, y));
        pending_.enqueue(key);
        pump();
    }
    return {};  // caller draws a placeholder until tileReady
}

void TileLayer::pump() {
    while (!pending_.isEmpty() && inFlight_.size() < maxInFlight_) {
        const QString key = pending_.dequeue();
        const QString url = pendingUrls_.take(key);
        QNetworkRequest req{QUrl(url)};
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("met-viewer/0.3 (https://example.invalid; contact@example.invalid)"));
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setRawHeader("Accept", "image/png,image/*");
        QNetworkReply* reply = nam_->get(req);
        reply->setProperty("tileKey", key);
        inFlight_.insert(key);
    }
}

void TileLayer::onFinished(QNetworkReply* reply) {
    reply->deleteLater();
    const QString key = reply->property("tileKey").toString();
    inFlight_.remove(key);

    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply->readAll();
        QImage img;
        if (img.loadFromData(bytes)) {
            const QStringList parts = key.split('/');
            memory_.insert(key, new QImage(img));
            if (parts.size() == 3)
                emit tileReady(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
        }
    }
    pump();  // free the slot for the next queued tile
}

}  // namespace met::app
