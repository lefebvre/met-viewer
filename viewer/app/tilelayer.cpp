#include "viewer/app/tilelayer.h"

#include <QCoreApplication>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include "viewer/core/log.h"

namespace met::app {
namespace {
// Consecutive failures after which a tile stops being re-requested.
constexpr int kMaxTileRetries = 3;
}  // namespace

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
        {"Esri World Shaded Relief",
         "https://server.arcgisonline.com/ArcGIS/rest/services/World_Shaded_Relief/MapServer/tile/{z}/{y}/{x}",
         "Esri", 13},
        {"OpenTopoMap", "https://a.tile.opentopomap.org/{z}/{x}/{y}.png",
         "© OpenStreetMap contributors, SRTM | © OpenTopoMap", 17},
    };
}

bool TileLayer::isValidUrlTemplate(const QString& t) {
    const QUrl url(t);
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) return false;
    if (url.host().isEmpty()) return false;
    return t.contains(QLatin1String("{z}")) && t.contains(QLatin1String("{x}")) &&
           t.contains(QLatin1String("{y}"));
}

void TileLayer::setSource(const TileSource& source) {
    source_ = source;
    ++sourceGen_;
    // Abort in-flight requests from the previous source so their tiles neither
    // poison the new source's cache (keys are source-agnostic z/x/y) nor blow the
    // concurrency budget. Iterate a copy: abort() delivers finished() synchronously,
    // which re-enters onFinished and mutates replies_.
    const QSet<QNetworkReply*> toAbort = replies_;
    replies_.clear();
    for (QNetworkReply* r : toAbort) r->abort();
    memory_.clear();
    inFlight_.clear();
    pending_.clear();
    pendingUrls_.clear();
    failed_.clear();  // a new source deserves a fresh try at every tile
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

    // Give up on a tile after a few consecutive failures until the source changes.
    // Repaints are frequent (every mouse-move, for the cursor readout), so without
    // this a 404 or an offline session turns into a steady request stream.
    if (failed_.value(key, 0) >= kMaxTileRetries) return {};

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
        // OSM's tile usage policy requires a User-Agent that identifies the app and
        // offers a way to reach its maintainer; a placeholder domain gets blocked.
        // The version tracks the application version rather than being hardcoded.
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("met-viewer/%1 (+https://github.com/lefebvre/met-viewer)")
                          .arg(QCoreApplication::applicationVersion().isEmpty()
                                   ? QStringLiteral("dev")
                                   : QCoreApplication::applicationVersion()));
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setRawHeader("Accept", "image/png,image/*");
        QNetworkReply* reply = nam_->get(req);
        reply->setProperty("tileKey", key);
        reply->setProperty("sourceGen", sourceGen_);
        replies_.insert(reply);
        inFlight_.insert(key);
    }
}

void TileLayer::onFinished(QNetworkReply* reply) {
    replies_.remove(reply);
    reply->deleteLater();
    // Drop replies from a superseded source: don't cache their tiles under the
    // new source's keys, and don't disturb the new source's in-flight bookkeeping.
    if (reply->property("sourceGen").toUInt() != sourceGen_) return;

    const QString key = reply->property("tileKey").toString();
    inFlight_.remove(key);

    bool ok = false;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply->readAll();
        QImage img;
        if (img.loadFromData(bytes)) {
            const QStringList parts = key.split('/');
            memory_.insert(key, new QImage(img));
            failed_.remove(key);
            ok = true;
            if (parts.size() == 3)
                emit tileReady(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
        }
    }
    if (!ok && reply->error() != QNetworkReply::OperationCanceledError) {
        // An abort (source switch / shutdown) is not the tile's fault, so it does
        // not count against the retry budget.
        const int n = failed_.value(key, 0) + 1;
        failed_.insert(key, n);
        if (n == kMaxTileRetries)
            MET_LOG_WARN("tile {} failed {} times ({}); not retrying until the basemap changes",
                         key.toStdString(), n, reply->errorString().toStdString());
        else
            MET_LOG_DEBUG("tile {} fetch failed: {}", key.toStdString(),
                          reply->errorString().toStdString());
    }
    pump();  // free the slot for the next queued tile
}

}  // namespace met::app
