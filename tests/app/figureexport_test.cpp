#include <gtest/gtest.h>

#include <cmath>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QList>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>

#include "viewer/analysis/sounding.h"
#include "viewer/app/figureexport.h"
#include "viewer/app/skewtview.h"
#include "viewer/app/timeseriesview.h"
#include "viewer/render/colormap.h"

using namespace met;

namespace {
analysis::Sounding makeSounding() {
    analysis::Sounding s;
    s.point = {45.0, 10.0};
    for (double p : {250.0, 400.0, 500.0, 700.0, 850.0, 1000.0}) {
        analysis::SoundingLevel lvl;
        lvl.pressure = p;
        lvl.tempK = 273.0f - static_cast<float>((1000.0 - p) * 0.02);
        lvl.dewpointK = lvl.tempK - 5.0f;
        s.levels.push_back(lvl);
    }
    return s;
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

// The page box a PDF declares, in points. Left uncompressed by the writer, so it can
// be read back without a PDF parser — which is the point of checking it here: it is
// the one thing that says the figure was laid out at the size we asked for.
QRectF mediaBox(const QByteArray& pdf) {
    const qsizetype at = pdf.indexOf("/MediaBox");
    if (at < 0) return {};
    const qsizetype open = pdf.indexOf('[', at), close = pdf.indexOf(']', open);
    if (open < 0 || close < 0) return {};
    const QList<QByteArray> n = pdf.mid(open + 1, close - open - 1).simplified().split(' ');
    if (n.size() != 4) return {};
    return QRectF(n[0].toDouble(), n[1].toDouble(), n[2].toDouble() - n[0].toDouble(),
                  n[3].toDouble() - n[1].toDouble());
}
}  // namespace

TEST(FigureExport, WritesAPdfOfTheCanvas) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    app::SkewTView view;
    view.resize(500, 560);
    view.setSounding(makeSounding());

    const QString path = dir.filePath(QStringLiteral("skewt.pdf"));
    QString error;
    ASSERT_TRUE(app::exportFigureToPdf({&view}, path, &error)) << error.toStdString();

    const QByteArray pdf = readAll(path);
    ASSERT_FALSE(pdf.isEmpty());
    EXPECT_TRUE(pdf.startsWith("%PDF-")) << pdf.left(16).toStdString();

    // 500 x 560 px laid down at 96 px/inch is 375 x 420 pt. A page that came out at
    // some default paper size would mean the canvas was scaled or cropped to fit it.
    const QRectF box = mediaBox(pdf);
    EXPECT_NEAR(box.width(), 375.0, 1.0);
    EXPECT_NEAR(box.height(), 420.0, 1.0);
}

// A view with a colormap gets its colour scale on the page beside the canvas, because
// on screen that scale lives in the control panel and would otherwise be left behind.
TEST(FigureExport, ColourScaleWidensThePageBesideTheCanvas) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    app::SkewTView view;  // any canvas will do; the scale is drawn from the colormap
    view.resize(500, 560);
    view.setSounding(makeSounding());

    const QString bare = dir.filePath(QStringLiteral("bare.pdf"));
    const QString scaled = dir.filePath(QStringLiteral("scaled.pdf"));
    QString error;
    ASSERT_TRUE(app::exportFigureToPdf({&view}, bare, &error)) << error.toStdString();

    const render::Colormap cmap = render::Colormap::builtin("viridis");
    app::Figure fig{&view, &cmap, QStringLiteral("K")};
    ASSERT_TRUE(app::exportFigureToPdf(fig, scaled, &error)) << error.toStdString();

    const QRectF bareBox = mediaBox(readAll(bare)), scaledBox = mediaBox(readAll(scaled));
    EXPECT_GT(scaledBox.width(), bareBox.width());
    EXPECT_NEAR(scaledBox.height(), bareBox.height(), 1.0);  // only wider, not taller
}

TEST(FigureExport, RefusesAViewWithNothingToDraw) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("empty.pdf"));
    QString error;

    EXPECT_FALSE(app::exportFigureToPdf({nullptr}, path, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFile::exists(path));
}

// Say why, and leave nothing behind. A half-written figure that reported success is
// worse than no figure at all.
TEST(FigureExport, ReportsWhyAnUnwritablePathFailed) {
    app::TimeSeriesView view;
    view.resize(400, 260);
    QString error;

    const QString path = QDir::tempPath() + QStringLiteral("/no-such-directory-here/fig.pdf");
    EXPECT_FALSE(app::exportFigureToPdf({&view}, path, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFile::exists(path));
}
