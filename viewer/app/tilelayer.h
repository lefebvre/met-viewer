#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace met::app {

// A named XYZ raster tile source.
struct TileSource {
    QString name;
    QString urlTemplate;   // with {z} {x} {y} placeholders
    QString attribution;
    int maxZoom = 19;
};

// Fetches and caches XYZ basemap tiles. Tiles are served from an in-memory cache
// immediately when present; misses trigger an async network fetch (disk-cached),
// and tileReady() is emitted when the image arrives. Fully graceful offline: a
// failed fetch simply never produces a tile, and callers draw a placeholder.
class TileLayer : public QObject {
    Q_OBJECT
public:
    explicit TileLayer(QObject* parent = nullptr);

    // Built-in sources (OSM, Carto light/dark, Esri imagery, OpenTopoMap).
    static QList<TileSource> builtinSources();

    void setSource(const TileSource& source);
    [[nodiscard]] const TileSource& source() const { return source_; }

    // Return the cached tile image, or a null QImage (queuing a fetch) on miss.
    [[nodiscard]] QImage tile(int z, int x, int y);

signals:
    void tileReady(int z, int x, int y);

private slots:
    void onFinished(QNetworkReply* reply);

private:
    QString keyOf(int z, int x, int y) const;
    QString urlFor(int z, int x, int y) const;
    void pump();  // start queued requests up to the in-flight cap

    QNetworkAccessManager* nam_ = nullptr;
    TileSource source_;
    QCache<QString, QImage> memory_;      // decoded tiles
    QSet<QString> inFlight_;              // keys currently fetching
    QQueue<QString> pending_;             // keys waiting for a slot
    QHash<QString, QString> pendingUrls_; // key -> url
    QSet<QNetworkReply*> replies_;        // active replies (to abort on source switch)
    unsigned sourceGen_ = 0;             // bumped on setSource; stale replies are dropped
    int maxInFlight_ = 6;
};

}  // namespace met::app
