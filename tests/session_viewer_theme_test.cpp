#include "shared/theme/AppTheme.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "ground/wave/RawDataParserWindow.h"
#include "ground/session/SessionViewerWindow.h"
#include "ground/session/SessionViewerWidgets.h"
#include "shared/session/UnifiedRawDat.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"
#include "ground/trajectory/TrajectoryViewerDialog.h"
#include "ground/widgets/VisualTextLabel.h"
#include "test_ui_helpers.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QStackedWidget>
#include <QSplitter>
#include <QSplitterHandle>
#include <QComboBox>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QBuffer>
#include <QHeaderView>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFrame>
#include <QFontMetrics>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMainWindow>
#include <QMargins>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStyleOptionSlider>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTimeZone>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>

namespace
{

constexpr char kTestRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kTestRawRecordMarker = 0x44525756u;
constexpr quint16 kTestRawSourceEpsilon = 1u;
constexpr quint16 kTestRawSourceTcpWave = 5u;
constexpr quint16 kTestRawHeaderSize = 20u;
constexpr quint16 kTestRawRecordHeaderSize = 36u;
constexpr quint32 kTestRawTcpWaveCombinedPayloadFlag = 0x00000001u;

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

using VaporViewTest::processEventsFor;
using VaporViewTest::processEventsUntil;
using VaporViewTest::waitForWindowExposed;

void requireComboPopupStyled(QComboBox *combo, const char *message)
{
    VaporViewTest::requireComboPopupStyled(combo, message, require);
}

void clickWidgetCenterThroughWindow(QWidget *widget, int waitMs = 0)
{
    require(widget != nullptr, "click target exists");
    require(widget->isEnabled(), "click target is enabled");
    require(widget->isVisibleTo(widget->window()), "click target is visible in its window");
    require(widget->window() && widget->window()->isVisible() && !widget->window()->isMinimized(),
            "click target window is visible and not minimized");
    const QPoint globalCenter = widget->mapToGlobal(widget->rect().center());
    QWidget *target = QApplication::widgetAt(globalCenter);
    // Some Windows/offscreen runs do not report frameless windows through
    // widgetAt() even after the window is exposed. Fall back to the known
    // visible target so this helper still verifies the button behavior.
    if (!target)
    {
        target = widget;
    }
    else
    {
        require(target == widget || widget->isAncestorOf(target),
                "click target is the visible widget at its screen position");
    }
    const QPoint localPos = target->mapFromGlobal(globalCenter);
    QMouseEvent press(QEvent::MouseButtonPress,
                      localPos,
                      globalCenter,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        localPos,
                        globalCenter,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(target, &release);
    if (waitMs > 0)
    {
        processEventsFor(waitMs);
    }
}

void clickWidgetAt(QWidget *widget, const QPointF& localPos, int waitMs = 250)
{
    require(widget != nullptr, "click target exists");
    require(widget->rect().adjusted(-2, -2, 2, 2).contains(localPos.toPoint()), "click target point is inside widget");
    QMouseEvent press(QEvent::MouseButtonPress,
                      localPos,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        localPos,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &release);
    if (waitMs > 0)
    {
        processEventsFor(waitMs);
    }
}

SessionViewerWindow *visibleSessionViewerWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets())
    {
        auto *viewer = qobject_cast<SessionViewerWindow *>(widget);
        if (viewer && viewer->isVisible() && !viewer->isMinimized())
        {
            return viewer;
        }
    }
    return nullptr;
}

TrajectoryViewerDialog *visibleTrajectoryViewerDialog()
{
    for (QWidget *widget : QApplication::allWidgets())
    {
        auto *dialog = qobject_cast<TrajectoryViewerDialog *>(widget);
        if (dialog && dialog->isVisible())
        {
            return dialog;
        }
    }
    return nullptr;
}

int countRedDominantPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.red() > color.green() + 24 && color.red() > color.blue() + 24)
            {
                ++count;
            }
        }
    }
    return count;
}

QPointF testLatLonToPixel(double latitude, double longitude, int zoom)
{
    constexpr double kTileSize = 256.0;
    constexpr double kPi = 3.14159265358979323846;
    const double lat = std::clamp(latitude, -85.05112878, 85.05112878);
    const double sinLat = std::sin(lat * kPi / 180.0);
    const double worldSize = kTileSize * std::pow(2.0, zoom);
    return QPointF(
        (longitude + 180.0) / 360.0 * worldSize,
        (0.5 - std::log((1.0 + sinLat) / (1.0 - sinLat)) / (4.0 * kPi)) * worldSize);
}

QPointF trajectoryPointScreenPosition(QWidget *map, const QVector<RtkTrackPoint>& points, int pointIndex)
{
    require(map != nullptr, "trajectory map exists for point projection");
    require(pointIndex >= 0 && pointIndex < points.size(), "trajectory point index is in range");

    const QSizeF mapSize = map->rect().size();
    const double availableWidth = std::max(200.0, mapSize.width());
    const double availableHeight = std::max(160.0, mapSize.height());
    int fitZoom = 1;
    QPointF fitCenter;
    for (int candidateZoom = 19; candidateZoom >= 1; --candidateZoom)
    {
        double minX = std::numeric_limits<double>::infinity();
        double maxX = -std::numeric_limits<double>::infinity();
        double minY = std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        for (const RtkTrackPoint& point : points)
        {
            const QPointF pixel = testLatLonToPixel(point.latitude, point.longitude, candidateZoom);
            minX = std::min(minX, pixel.x());
            maxX = std::max(maxX, pixel.x());
            minY = std::min(minY, pixel.y());
            maxY = std::max(maxY, pixel.y());
        }
        if ((maxX - minX) <= availableWidth * 0.8 && (maxY - minY) <= availableHeight * 0.8)
        {
            fitZoom = candidateZoom;
            fitCenter = QPointF((minX + maxX) * 0.5, (minY + maxY) * 0.5);
            break;
        }
    }

    const QPointF pointPixel = testLatLonToPixel(points.at(pointIndex).latitude, points.at(pointIndex).longitude, fitZoom);
    const QPointF topLeft = fitCenter - QPointF(mapSize.width() * 0.5, mapSize.height() * 0.5);
    return QPointF(pointPixel.x() - topLeft.x(), pointPixel.y() - topLeft.y());
}

void writeUnifiedRawFile(const QString& path, int recordCount)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "temporary raw file can be written");

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData(kTestRawMagic, sizeof(kTestRawMagic));
    stream << quint32(2) << quint32(kTestRawHeaderSize) << quint16(kTestRawSourceEpsilon) << quint16(0);

    const QByteArray payload("TEST");
    for (int i = 0; i < recordCount; ++i)
    {
        stream << quint32(kTestRawRecordMarker)
               << quint32(kTestRawRecordHeaderSize)
               << quint64(1'700'000'000'000'000ULL + static_cast<quint64>(i))
               << quint32(payload.size())
               << quint16(kTestRawSourceEpsilon)
               << quint16(0x40)
               << quint32(0)
               << quint64(i);
        stream.writeRawData(payload.constData(), payload.size());
    }
}

void writeUnifiedRawPayloadFile(const QString& path,
                                quint16 recordType,
                                const QByteArray& payload,
                                quint16 sourceId = kTestRawSourceEpsilon)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "temporary raw payload file can be written");

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData(kTestRawMagic, sizeof(kTestRawMagic));
    stream << quint32(2) << quint32(kTestRawHeaderSize) << sourceId << quint16(0);
    stream << quint32(kTestRawRecordMarker)
           << quint32(kTestRawRecordHeaderSize)
           << quint64(1'700'000'000'000'000ULL)
           << quint32(payload.size())
           << sourceId
           << recordType
           << quint32(0)
           << quint64(0);
    stream.writeRawData(payload.constData(), payload.size());
}

QByteArray floatPayload(std::initializer_list<float> values)
{
    QByteArray payload;
    payload.resize(static_cast<int>(values.size() * sizeof(float)));
    char *cursor = payload.data();
    for (float value : values)
    {
        quint32 bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(value));
        const quint32 littleEndianBits = qToLittleEndian(bits);
        std::memcpy(cursor, &littleEndianBits, sizeof(littleEndianBits));
        cursor += sizeof(littleEndianBits);
    }
    return payload;
}

void writeMinimalRawTcpWaveFile(const QString& path, const QVector<quint64>& timestampsUs)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "temporary raw tcp wave file can be written");

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData(kTestRawMagic, sizeof(kTestRawMagic));
    stream << quint32(2) << quint32(kTestRawHeaderSize) << quint16(kTestRawSourceTcpWave) << quint16(0);

    for (int index = 0; index < timestampsUs.size(); ++index)
    {
        const QByteArray rawPayload = floatPayload({0.1f, 0.2f});
        const QByteArray harmonicPayload = floatPayload({
            0.1f + static_cast<float>(index),
            0.4f + static_cast<float>(index),
            0.9f + static_cast<float>(index),
            0.3f + static_cast<float>(index)});
        const quint32 rawSize = static_cast<quint32>(rawPayload.size());
        const quint32 harmonicSize = static_cast<quint32>(harmonicPayload.size());
        const quint32 payloadSize = sizeof(quint32) * 2u + rawSize + harmonicSize;

        stream << quint32(kTestRawRecordMarker)
               << quint32(kTestRawRecordHeaderSize)
               << timestampsUs.at(index)
               << payloadSize
               << quint16(kTestRawSourceTcpWave)
               << quint16(VaporView::SessionRawDat::kRecordTypeWaveformPayload)
               << quint32(kTestRawTcpWaveCombinedPayloadFlag)
               << quint64(index)
               << rawSize
               << harmonicSize;
        stream.writeRawData(rawPayload.constData(), rawPayload.size());
        stream.writeRawData(harmonicPayload.constData(), harmonicPayload.size());
    }
}

void writeMinimalTrajectorySession(const QString& sessionPath)
{
    QDir dir(sessionPath);
    require(dir.mkpath(QStringLiteral("sensors")), "temporary trajectory sensors directory can be created");

    QFile metadata(dir.filePath(QStringLiteral("session.json")));
    require(metadata.open(QIODevice::WriteOnly | QIODevice::Text), "temporary trajectory metadata can be written");
    metadata.write(R"json({
  "session_name": "trajectory_test_session",
  "start_time_utc": "2026-06-26T06:33:55.573Z",
  "end_time_utc": "2026-06-26T06:33:58.573Z",
  "sensor_rows": "4",
  "sensor_export_rate_hz": 1,
  "waveform_frames": "0",
  "waveform_points_per_frame": 16,
  "waveform_export_rate_hz": 0,
  "waveform_export_mode": "per_frame",
  "paths": {
    "devices_csv": "sensors/devices.csv",
    "waveform_directory": "waveform",
    "waveform_index": "waveform_index.csv"
  },
  "raw_files": {
    "tcp_wave": {
      "path": "raw/tcp_wave.dat"
    }
  }
})json");
    metadata.close();

    QFile sensors(dir.filePath(QStringLiteral("sensors/devices.csv")));
    require(sensors.open(QIODevice::WriteOnly | QIODevice::Text), "temporary trajectory CSV can be written");
    QTextStream stream(&sensors);
    stream << "record_timestamp_us,epsilon_valid,gnss_fix,nav_lat_deg,nav_lon_deg,nav_height_m\n";
    stream << "1782446035573000,true,RTK_FIXED,30.13698120,120.06938175,9.606\n";
    stream << "1782446036573000,true,RTK_FIXED,30.13712000,120.06952000,9.806\n";
    stream << "1782446037573000,true,RTK_FIXED,30.13736000,120.06972000,10.106\n";
    stream << "1782446038573000,true,RTK_FIXED,30.13762000,120.06990710,11.190\n";
}

void writeTrajectorySessionWithRawTcpPeaks(const QString& sessionPath)
{
    QDir dir(sessionPath);
    require(dir.mkpath(QStringLiteral("sensors")), "temporary peak trajectory sensors directory can be created");
    require(dir.mkpath(QStringLiteral("raw")), "temporary peak trajectory raw directory can be created");

    const QVector<quint64> timestampsUs = {
        1782446035573000ULL,
        1782446036573000ULL,
        1782446037573000ULL,
        1782446038573000ULL,
    };

    QFile metadata(dir.filePath(QStringLiteral("session.json")));
    require(metadata.open(QIODevice::WriteOnly | QIODevice::Text), "temporary peak trajectory metadata can be written");
    metadata.write(R"json({
  "session_name": "trajectory_peak_test_session",
  "start_time_utc": "2026-06-26T06:33:55.573Z",
  "end_time_utc": "2026-06-26T06:33:58.573Z",
  "sensor_rows": "4",
  "sensor_export_rate_hz": 1,
  "waveform_frames": "4",
  "waveform_points_per_frame": 4,
  "waveform_export_rate_hz": 1,
  "waveform_export_mode": "per_frame",
  "paths": {
    "devices_csv": "sensors/devices.csv",
    "waveform_directory": "waveform",
    "waveform_index": "waveform_index.csv",
    "waveform_peak_index": "raw/tcp_wave_peaks.csv"
  },
  "raw_files": {
    "tcp_wave": {
      "path": "raw/tcp_wave.dat",
      "record_count": "4"
    }
  }
})json");
    metadata.close();

    QFile sensors(dir.filePath(QStringLiteral("sensors/devices.csv")));
    require(sensors.open(QIODevice::WriteOnly | QIODevice::Text), "temporary peak trajectory CSV can be written");
    QTextStream stream(&sensors);
    stream << "record_timestamp_us,epsilon_valid,gnss_fix,nav_lat_deg,nav_lon_deg,nav_height_m\n";
    stream << "1782446035573000,true,RTK_FIXED,30.13698120,120.06938175,9.606\n";
    stream << "1782446036573000,true,RTK_FIXED,30.13712000,120.06952000,9.806\n";
    stream << "1782446037573000,true,RTK_FIXED,30.13736000,120.06972000,10.106\n";
    stream << "1782446038573000,true,RTK_FIXED,30.13762000,120.06990710,11.190\n";
    sensors.close();

    writeMinimalRawTcpWaveFile(dir.filePath(QStringLiteral("raw/tcp_wave.dat")), timestampsUs);
}

void testRawDataExportMenu()
{
    const bool writesSuspended = VaporView::settingsWritesSuspended();
    VaporView::setSettingsWritesSuspended(true);
    for (bool dark : {false, true})
    {
        qApp->setProperty(VaporView::kAppDarkThemeProperty, dark);
        qApp->setPalette(VaporView::appThemePalette(dark));
        for (bool embedded : {false, true})
        {
            QWidget host;
            RawDataParserWindow parser(embedded ? &host : nullptr, embedded);
            QWidget *window = embedded ? &host : &parser;
            if (embedded)
            {
                auto *layout = new QVBoxLayout(&host);
                layout->addWidget(&parser);
                host.resize(1280, 800);
            }
            window->show();
            window->raise();
            window->activateWindow();
            require(waitForWindowExposed(window), "export menu parser exposed");
            auto *button = parser.findChild<QPushButton *>(QStringLiteral("rawDataExportButton"));
            auto *menu = parser.findChild<VaporView::SingleLevelPopupMenu *>(QStringLiteral("rawDataExportMenu"));
            require(button && menu && menu->actions().size() == 5, "one export button provides five actions");
            for (bool english : {false, true})
            {
                parser.setEnglish(english);
                button->click();
                processEventsFor(50);
                require(menu->isVisible(), "export button opens menu");
                const QStringList expected = english
                    ? QStringList{"Export List CSV", "Export Checked JSON", "Export Checked BIN", "Export Decoded CSV", "Export Decoded JSON"}
                    : QStringList{QStringLiteral("导出列表CSV"), QStringLiteral("导出勾选JSON"), QStringLiteral("导出勾选BIN"), QStringLiteral("导出解析CSV"), QStringLiteral("导出解析JSON")};
                for (int index = 0; index < 5; ++index)
                {
                    auto *row = menu->rows().at(index);
                    require(row->text() == expected.at(index) && row->isVisible(), "all export choices are visible and translated");
                }
                menu->hide();
            }
            for (auto *row : menu->rows())
            {
                button->click();
                bool handled = false;
                QTimer::singleShot(0, &parser, [&] {
                    auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                    require(message != nullptr && message->text().contains(QStringLiteral("Export simulated")),
                            "export menu action reaches export handler");
                    handled = true;
                    message->accept();
                });
                row->click();
                require(handled && !menu->isVisible(), "export action dismisses menu before opening dialog");
            }
            window->close();
        }
    }
    VaporView::setSettingsWritesSuspended(writesSuspended);
    qApp->setProperty(VaporView::kAppDarkThemeProperty, false);
    qApp->setPalette(VaporView::appThemePalette(false));
}

void testRawDataCheckedExport()
{
    QTemporaryDir sessionDir;
    QTemporaryDir outputDir;
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("raw")), "batch fixture directory");
    const QString rawPath = sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat"));
    writeMinimalRawTcpWaveFile(rawPath, {1700000000000000ULL, 1700000000000001ULL,
                                       1700000000000002ULL, 1700000000000003ULL});
    RawDataParserWindow parser;
    parser.setEnglish(true);
    parser.show();
    auto *table = parser.findChild<QTableView *>();
    auto *button = parser.findChild<QPushButton *>(QStringLiteral("rawDataExportButton"));
    auto *menu = parser.findChild<VaporView::SingleLevelPopupMenu *>(QStringLiteral("rawDataExportMenu"));
    require(parser.openSessionPath(sessionDir.path()), "batch fixture opens");
    require(processEventsUntil(5000, [&] { return table->model()->rowCount() == 4; }), "batch fixture indexed");
    auto *model = table->model();
    require(model->index(0, 0).data(Qt::CheckStateRole).toInt() == Qt::Unchecked, "viewing first record does not check it");
    require(model->flags(model->index(0, 0)).testFlag(Qt::ItemIsUserCheckable), "record checkbox is interactive");
    processEventsFor(100);
    QStyleOptionViewItem option;
    option.initFrom(table);
    option.rect = table->visualRect(model->index(0, 0));
    option.features = QStyleOptionViewItem::HasCheckIndicator;
    require(model->columnCount() == 9 && !model->index(0, 0).data().isValid() &&
            model->index(0, 1).data().toInt() == 1 &&
            !model->index(0, 1).data(Qt::CheckStateRole).isValid(), "checkbox and row number occupy separate columns");
    require(table->columnWidth(0) == 24 && table->horizontalHeader()->sectionResizeMode(0) == QHeaderView::Fixed,
            "checkbox column stays narrow and fixed");
    const QRect indicator = option.rect;
    clickWidgetAt(table->viewport(), indicator.center(), 0);
    require(model->index(0, 0).data(Qt::CheckStateRole).toInt() == Qt::Checked, "mouse click checks record");
    model->setData(model->index(2, 0), Qt::Checked, Qt::CheckStateRole);
    table->selectRow(1);
    require(button->text().contains(QStringLiteral("2 checked")), "detail selection does not alter checked count");
    QLineEdit *sequenceFrom = nullptr;
    for (auto *edit : parser.findChildren<QLineEdit *>())
        if (edit->placeholderText() == QStringLiteral("seq from")) sequenceFrom = edit;
    require(sequenceFrom != nullptr, "sequence filter located");
    sequenceFrom->setText(QStringLiteral("2"));
    QMetaObject::invokeMethod(sequenceFrom, "editingFinished");
    require(processEventsUntil(2000, [&] { return model->rowCount() == 2; }), "batch filter applied");
    require(model->index(0, 0).data(Qt::UserRole).toInt() == 2 &&
            model->index(0, 0).data(Qt::CheckStateRole).toInt() == Qt::Checked &&
            button->text().contains(QStringLiteral("2 checked")), "checks use record identity and include hidden records");

    const bool suspended = VaporView::settingsWritesSuspended();
    const bool nativeDisabled = qApp->testAttribute(Qt::AA_DontUseNativeDialogs);
    VaporView::setSettingsWritesSuspended(false);
    qApp->setAttribute(Qt::AA_DontUseNativeDialogs, true);
    QString expectedDefaultName;
    const QString sessionPrefix = QDir(sessionDir.path()).dirName() + QLatin1Char('_');
    auto runExport = [&](int action, const QString& destination, bool cancel = false) {
        QTimer responder;
        bool dialogHandled = false;
        bool messageHandled = false;
        QObject::connect(&responder, &QTimer::timeout, &parser, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
            {
                if (dialogHandled) return;
                dialogHandled = true;
                if (!expectedDefaultName.isEmpty())
                    require(QFileInfo(dialog->selectedFiles().value(0)).fileName() == expectedDefaultName,
                            "default export filename identifies session and export contents");
                if (cancel) dialog->reject();
                else
                {
                    dialog->selectFile(destination);
                    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                }
            }
            else if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            {
                require(message->icon() != QMessageBox::Warning, "batch export has no write error");
                messageHandled = true;
                message->accept();
            }
        });
        responder.start(20);
        menu->actions().at(action)->trigger();
        require(dialogHandled || messageHandled, "export dialog handled");
    };
    const QString jsonPath = outputDir.filePath(QStringLiteral("checked.json"));
    expectedDefaultName = sessionPrefix + QStringLiteral("raw_checked_records.json");
    runExport(1, jsonPath);
    QFile jsonFile(jsonPath);
    require(jsonFile.open(QIODevice::ReadOnly), "checked JSON written");
    const auto root = QJsonDocument::fromJson(jsonFile.readAll()).object();
    const auto exported = root.value(QStringLiteral("records")).toArray();
    require(root.value(QStringLiteral("selected_record_count")).toInt() == 2 && exported.size() == 2 &&
            exported.at(0).toObject().value(QStringLiteral("sequence")).toString() == QStringLiteral("0") &&
            exported.at(1).toObject().value(QStringLiteral("sequence")).toString() == QStringLiteral("2"),
            "JSON exports exactly arbitrary checked records including hidden one");
    expectedDefaultName.clear();
    runExport(2, outputDir.path());
    const auto batches = QDir(outputDir.path()).entryList({sessionPrefix + QStringLiteral("raw_payloads_*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    require(batches.size() == 1, "BIN batch has one fresh subfolder");
    QDir batch(outputDir.filePath(batches.first()));
    const auto bins = batch.entryList({QStringLiteral("*.bin")}, QDir::Files, QDir::Name);
    require(bins.size() == 2, "BIN exports two separate records");
    QFile original(rawPath);
    require(original.open(QIODevice::ReadOnly), "batch source readable");
    for (int i = 0; i < 2; ++i)
    {
        require(bins.at(i) == sessionPrefix + QStringLiteral("tcp_wave_seq_%1_record_%2.bin").arg(i * 2).arg(i * 2 + 1),
                "batch BIN names retain session device sequence and unique record identity");
        QFile bin(batch.filePath(bins.at(i)));
        require(bin.open(QIODevice::ReadOnly), "BIN readable");
        original.seek(kTestRawHeaderSize + (i * 2) * (kTestRawRecordHeaderSize + 32) + kTestRawRecordHeaderSize);
        require(bin.readAll() == original.read(32), "BIN preserves exact selected payload bytes");
    }
    runExport(1, outputDir.filePath(QStringLiteral("cancelled.json")), true);
    require(!QFile::exists(outputDir.filePath(QStringLiteral("cancelled.json"))), "cancel does not create export");
    sequenceFrom->clear();
    QMetaObject::invokeMethod(sequenceFrom, "editingFinished");
    require(processEventsUntil(2000, [&] { return model->rowCount() == 4; }), "filter cleared");
    model->setData(model->index(2, 0), Qt::Unchecked, Qt::CheckStateRole);
    const QString singlePath = outputDir.filePath(QStringLiteral("single.json"));
    expectedDefaultName = sessionPrefix + QStringLiteral("tcp_wave_seq_0.json");
    runExport(1, singlePath);
    QFile single(singlePath);
    require(single.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(single.readAll()).object()
            .value(QStringLiteral("sequence")).toString() == QStringLiteral("0"), "one checked record preserves single JSON format");
    expectedDefaultName = sessionPrefix + QStringLiteral("tcp_wave_seq_0.bin");
    runExport(2, QString(), true);
    expectedDefaultName = sessionPrefix + QStringLiteral("raw_records.csv");
    runExport(0, QString(), true);
    expectedDefaultName = sessionPrefix + QStringLiteral("raw_decoded_fields.csv");
    runExport(3, QString(), true);
    expectedDefaultName = sessionPrefix + QStringLiteral("raw_decoded_records.json");
    runExport(4, QString(), true);
    model->setData(model->index(0, 0), Qt::Unchecked, Qt::CheckStateRole);
    runExport(1, QString());
    model->setData(model->index(2, 0), Qt::Checked, Qt::CheckStateRole);
    require(parser.openSessionPath(sessionDir.path()), "batch fixture reloads");
    require(processEventsUntil(5000, [&] { return model->rowCount() == 4; }), "batch fixture reindexed");
    require(!button->text().contains(QStringLiteral("checked")) &&
            model->index(2, 0).data(Qt::CheckStateRole).toInt() == Qt::Unchecked, "reload clears checked identities");
    VaporView::setSettingsWritesSuspended(suspended);
    qApp->setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
}

void testRawDataIssuesMatchDetails()
{
    QTemporaryDir sessionDir;
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("raw")), "validation fixture raw directory");
    const QByteArray frame = QByteArray::fromHex("fc403800090000") + QByteArray(56, '\0') + QByteArray::fromHex("fd");
    QList<QByteArray> frames{frame, QByteArray("short"), frame.left(8)};
    for (int offset : {0, 2, 4, 5, 7, 63})
    {
        QByteArray damaged = frame;
        damaged[offset] = static_cast<char>(damaged.at(offset) ^ 1);
        frames.append(damaged);
    }
    // Combined waveform: 4 raw bytes and 8 harmonic bytes.
    const QByteArray wave = QByteArray::fromHex("0400000008000000") + QByteArray(12, '\0');
    QList<QByteArray> waves{wave, QByteArray("short"), wave.left(8), wave + 'x',
                           QByteArray::fromHex("ffffffffffffffff")};
    auto write = [&](const QString &name, quint16 source, const QList<QByteArray> &payloads) {
        QFile file(sessionDir.filePath(QStringLiteral("raw/") + name));
        require(file.open(QIODevice::WriteOnly), "validation fixtures writable");
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData(kTestRawMagic, sizeof(kTestRawMagic));
        stream << quint32(2) << quint32(kTestRawHeaderSize) << source << quint16(0);
        for (int index = 0; index < payloads.size(); ++index)
        {
            const auto &payload = payloads.at(index);
            stream << quint32(kTestRawRecordMarker) << quint32(kTestRawRecordHeaderSize)
                   << quint64(1700000000000000ULL + index) << quint32(payload.size())
                   << source << quint16(source == kTestRawSourceTcpWave ? VaporView::SessionRawDat::kRecordTypeWaveformPayload : 0x40)
                   // Legacy unflagged records reach the viewer even with bad sub-payload sizes.
                   << quint32(0) << quint64(index);
            stream.writeRawData(payload.constData(), payload.size());
        }
    };
    write(QStringLiteral("epsilon.dat"), kTestRawSourceEpsilon, frames);
    write(QStringLiteral("tcp_wave.dat"), kTestRawSourceTcpWave, waves);
    RawDataParserWindow parser;
    parser.show();
    auto *table = parser.findChild<QTableView *>();
    auto *tree = parser.findChild<QTreeWidget *>();
    auto *issues = parser.findChild<QCheckBox *>();
    const int count = frames.size() + waves.size();
    auto open = [&] {
        require(parser.openSessionPath(sessionDir.path()), "validation fixtures open");
        require(processEventsUntil(5000, [&] { return table->model()->rowCount() == count; }), "validation fixtures indexed");
    };
    open();
    QList<int> expected;
    for (int row = 0; row < count; ++row)
    {
        table->setCurrentIndex(table->model()->index(row, 0));
        bool abnormal = false;
        for (int group = 0; group < tree->topLevelItemCount(); ++group)
            for (int field = 0; field < tree->topLevelItem(group)->childCount(); ++field)
                abnormal |= tree->topLevelItem(group)->child(field)->background(0).style() != Qt::NoBrush;
        if (abnormal) expected.append(table->model()->index(row, 0).data(Qt::UserRole).toInt());
    }
    require(!expected.isEmpty() && expected.size() < count, "fixtures include normal and abnormal full decodes");
    // Clear the index so cached full decodes cannot mask lightweight-path errors.
    parser.clearSession();
    open();
    issues->setChecked(true);
    require(table->model()->rowCount() == expected.size(), "lightweight validation matches full detail flags");
    for (int row = 0; row < expected.size(); ++row)
        require(table->model()->index(row, 0).data(Qt::UserRole).toInt() == expected.at(row), "lightweight validation preserves damaged record identities");
}

void testRawDataIssuesFilter()
{
    QTemporaryDir sessionDir;
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("raw")), "issues filter raw directory");
    constexpr int count = 20000;
    const QString path = sessionDir.filePath(QStringLiteral("raw/epsilon.dat"));
    auto writeRecords = [&](bool issues) {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "issues filter fixture writable");
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData(kTestRawMagic, sizeof(kTestRawMagic));
        stream << quint32(2) << quint32(kTestRawHeaderSize) << quint16(kTestRawSourceEpsilon) << quint16(0);
        // Valid IMU frame with a zero-filled body; CRC8=09, CRC16=0000.
        const QByteArray normal = QByteArray::fromHex("fc403800090000") + QByteArray(56, '\0') + QByteArray::fromHex("fd");
        for (int index = 0; index < count; ++index)
        {
            QByteArray payload = normal;
            if (issues && index % 10 == 9)
                payload[4] = '\0'; // Bad header CRC, with a structurally complete record.
            stream << quint32(kTestRawRecordMarker) << quint32(kTestRawRecordHeaderSize)
                   << quint64(1700000000000000ULL + index) << quint32(payload.size())
                   << quint16(kTestRawSourceEpsilon) << quint16(0x40) << quint32(0) << quint64(index);
            stream.writeRawData(payload.constData(), payload.size());
        }
    };
    writeRecords(true);
    RawDataParserWindow parser;
    parser.setEnglish(true);
    parser.show();
    require(waitForWindowExposed(&parser), "issues filter parser exposed");
    auto *table = parser.findChild<QTableView *>();
    auto *issues = parser.findChild<QCheckBox *>();
    require(table && issues, "issues filter controls exist");
    require(parser.openSessionPath(sessionDir.path()), "issues filter session opens");
    require(processEventsUntil(10000, [&] { return table->model()->rowCount() == count; }), "issues filter index ready");
    QElapsedTimer elapsed;
    elapsed.start();
    issues->setChecked(true);
    const qint64 coldMs = elapsed.elapsed();
    require(table->model()->rowCount() == count / 10, "issues filter retains precisely CRC failures");
    for (int row = 0; row < count / 10; ++row)
        require(table->model()->index(row, 0).data(Qt::UserRole).toInt() == row * 10 + 9, "issues filter preserves record identities");
    issues->setChecked(false);
    elapsed.restart();
    issues->setChecked(true);
    const qint64 warmMs = elapsed.elapsed();
    require(table->model()->rowCount() == count / 10, "repeated issues filter preserves results");
    std::cout << "issues filter " << count << " records: cold=" << coldMs << " ms warm=" << warmMs << " ms\n";
    parser.clearSession();
    writeRecords(false);
    require(parser.openSessionPath(sessionDir.path()), "issues filter reloads updated data");
    auto *progress = parser.findChild<QWidget *>(QStringLiteral("rawDataParserProgressPanel"));
    require(processEventsUntil(10000, [&] { return !progress->isVisible(); }), "issues filter reload completes");
    require(table->model()->rowCount() == 0, "reload invalidates previous anomaly results");
    issues->setChecked(false);
    require(table->model()->rowCount() == count, "disabling issues restores every record");
    parser.clearSession();
    writeRecords(true);
    require(parser.openSessionPath(sessionDir.path()), "cancellation fixture opens");
    require(processEventsUntil(10000, [&] { return table->model()->rowCount() == count; }), "cancellation fixture indexed");
    QTimer cancel;
    bool cancelled = false;
    QObject::connect(&cancel, &QTimer::timeout, &parser, [&] {
        auto *dialog = parser.findChild<QProgressDialog *>();
        if (dialog && dialog->value() > 0)
        {
            cancelled = true;
            cancel.stop();
            dialog->cancel();
        }
    });
    cancel.start(0);
    issues->setChecked(true);
    cancel.stop();
    require(cancelled && table->model()->rowCount() < count / 10, "issues scan remains cancellable");
    issues->setChecked(false);
    issues->setChecked(true);
    require(table->model()->rowCount() == count / 10, "cancelled scan does not mark unchecked records normal");
}

void testRawDataScanProgressVisible()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary scan progress session");
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("raw")), "scan progress raw directory");
    writeUnifiedRawFile(sessionDir.filePath(QStringLiteral("raw/epsilon.dat")), 60000);
    for (bool dark : {false, true})
    {
        qApp->setProperty(VaporView::kAppDarkThemeProperty, dark);
        qApp->setPalette(VaporView::appThemePalette(dark));
        RawDataParserWindow parser;
        parser.show();
        require(waitForWindowExposed(&parser), "scan progress parser exposed");
        bool inspected = false;
        QTimer inspect;
        QObject::connect(&inspect, &QTimer::timeout, &parser, [&] {
            auto *dialog = parser.findChild<QProgressDialog *>();
            if (!dialog || dialog->value() <= 0) return;
            inspect.stop();
            dialog->show();
            dialog->layout()->activate();
            auto *content = dialog->findChild<QWidget *>(QStringLiteral("customTitleBarContent"));
            require(content != nullptr, "scan progress has title bar content");
            auto *label = content->findChild<QLabel *>();
            auto *bar = content->findChild<QProgressBar *>();
            auto *cancel = content->findChild<QPushButton *>();
            require(label && bar && cancel, "scan text, progress and cancel belong to content layout");
            content->layout()->activate();
            for (QWidget *widget : {static_cast<QWidget *>(label), static_cast<QWidget *>(bar), static_cast<QWidget *>(cancel)})
            {
                require(widget->isVisible() && !widget->visibleRegion().isEmpty(), "scan control is visible and unobscured");
                require(content->rect().contains(widget->geometry()), "scan control fits inside content");
            }
            require(label->geometry().bottom() < bar->geometry().top() &&
                        bar->geometry().bottom() < cancel->geometry().top(), "scan controls do not overlap");
            require(bar->maximum() == 60000 && bar->value() > 0 && bar->value() < bar->maximum(),
                    "scan bar reports actual partial record progress");
            dialog->resize(dialog->size() + QSize(100, 60));
            dialog->layout()->activate();
            content->layout()->activate();
            require(content->rect().contains(bar->geometry()) && !bar->visibleRegion().isEmpty(),
                    "scan progress remains visible after resizing");
            inspected = true;
            if (dark)
            {
                cancel->click();
                require(dialog->wasCanceled(), "scan cancel button cancels the dialog");
            }
        });
        inspect.start(0);
        require(parser.openSessionPath(sessionDir.path()), "scan progress session opens");
        require(processEventsUntil(10000, [&] { return inspected; }), "scan progress inspected during filtering");
        require(parser.findChild<QProgressDialog *>() == nullptr, "scan dialog destroyed after completion or cancel");
        if (!dark)
            require(parser.findChild<QTableView *>()->model()->rowCount() == 60000, "normal scan retains all records");
        parser.close();
    }
    qApp->setProperty(VaporView::kAppDarkThemeProperty, false);
    qApp->setPalette(VaporView::appThemePalette(false));
}

void testRawDataParserOpenIsNonBlocking()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary raw parser session directory");
    QDir dir(sessionDir.path());
    require(dir.mkpath(QStringLiteral("raw")), "temporary raw directory can be created");
    writeUnifiedRawFile(dir.filePath(QStringLiteral("raw/epsilon.dat")), 300000);

    QFile indexFile(dir.filePath(QStringLiteral("raw/epsilon.dat")));
    require(indexFile.open(QIODevice::ReadOnly), "open index comparison file");
    const auto fileIndex = VaporView::SessionRawDat::scan(indexFile);
    uchar *mapped = indexFile.map(0, indexFile.size());
    require(mapped != nullptr, "map index comparison file");
    QBuffer mappedFile;
    mappedFile.setData(QByteArray::fromRawData(reinterpret_cast<const char *>(mapped), indexFile.size()));
    mappedFile.open(QIODevice::ReadOnly);
    const auto mappedIndex = VaporView::SessionRawDat::scan(mappedFile);
    require(fileIndex.status == mappedIndex.status && fileIndex.error == mappedIndex.error &&
                fileIndex.warning == mappedIndex.warning && fileIndex.records.size() == mappedIndex.records.size() &&
                fileIndex.lastValidOffset == mappedIndex.lastValidOffset,
            "mapped scan preserves index status and size");
    for (qsizetype i = 0; i < fileIndex.records.size(); ++i)
    {
        const auto &a = fileIndex.records[i];
        const auto &b = mappedIndex.records[i];
        require(a.recordOffset == b.recordOffset && a.payloadOffset == b.payloadOffset &&
                    a.header.sequence == b.header.sequence && a.header.hostTimestampUs == b.header.hostTimestampUs &&
                    a.header.sourceId == b.header.sourceId && a.header.recordType == b.header.recordType &&
                    a.header.flags == b.header.flags && a.header.payloadSize == b.header.payloadSize &&
                    a.waveformHarmonicOffset == b.waveformHarmonicOffset && a.waveformHarmonicSize == b.waveformHarmonicSize,
                "mapped scan preserves every record field");
    }
    mappedFile.close();
    mappedFile.setData(QByteArray());
    indexFile.unmap(mapped);

    RawDataParserWindow parser;
    parser.setEnglish(false);
    parser.show();
    require(waitForWindowExposed(&parser), "raw parser window becomes exposed");

    require(parser.openSessionPath(sessionDir.path()), "raw parser accepts temporary session directory");

    auto *progressBar = parser.findChild<QProgressBar *>(QStringLiteral("rawDataParserProgressBar"));
    require(progressBar != nullptr, "raw parser exposes an indexing progress bar");
    require(progressBar->isVisible(),
            "raw parser returns while background indexing is still active");

    bool eventLoopResponsive = false;
    QTimer::singleShot(0, &parser, [&eventLoopResponsive]() { eventLoopResponsive = true; });
    require(processEventsUntil(1000, [&eventLoopResponsive]() { return eventLoopResponsive; }),
            "raw parser keeps the UI event loop responsive while indexing");

    auto *statusLabel = parser.findChild<QLabel *>(QStringLiteral("rawDataParserStatusLabel"));
    require(statusLabel != nullptr, "raw parser exposes a status label");
    require(processEventsUntil(10000, [statusLabel]() {
                const QString text = statusLabel->text();
                return text.contains(QStringLiteral("建立 300000 条")) ||
                       text.contains(QStringLiteral("Indexed 300000"));
            }),
            "raw parser background indexing completes");

    auto *table = parser.findChild<QTableView *>();
    require(table != nullptr, "raw parser record table exists");
    table->setFixedWidth(1800);
    processEventsFor(200);
    const int wideWidth = table->horizontalHeader()->length();
    require(wideWidth >= table->viewport()->width(), "expanded record columns fill the viewport");
    table->setFixedWidth(900);
    processEventsFor(200);
    require(table->horizontalHeader()->length() < wideWidth, "record columns recalculate when shrinking");
    table->setFixedWidth(1800);
    processEventsFor(200);
    require(table->horizontalHeader()->length() == wideWidth, "record columns recalculate when expanding again");

    parser.close();
    processEventsFor(100);
}

void testRawDataParserRejectsTruncatedFdilinkFrame()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary malformed raw parser session directory");
    QDir dir(sessionDir.path());
    require(dir.mkpath(QStringLiteral("raw")), "temporary malformed raw directory can be created");

    const QByteArray truncatedFrame = QByteArray::fromHex("fc40ff00000000fd");
    writeUnifiedRawPayloadFile(
        dir.filePath(QStringLiteral("raw/epsilon.dat")),
        0x40,
        truncatedFrame);

    RawDataParserWindow parser;
    parser.setEnglish(true);
    parser.show();
    require(waitForWindowExposed(&parser), "malformed raw parser window becomes exposed");
    require(parser.openSessionPath(sessionDir.path()), "raw parser accepts malformed-frame session directory");

    auto *detailTree = parser.findChild<QTreeWidget *>();
    require(detailTree != nullptr, "malformed raw parser exposes a detail tree");
    require(processEventsUntil(5000, [detailTree]() {
                return detailTree->topLevelItemCount() >= 2;
            }),
            "truncated FDILink frame header diagnostics are displayed");

    bool foundDeclaredPayloadSize = false;
    bool foundPayloadFields = false;
    for (int groupIndex = 0; groupIndex < detailTree->topLevelItemCount(); ++groupIndex)
    {
        const QTreeWidgetItem *group = detailTree->topLevelItem(groupIndex);
        foundPayloadFields = foundPayloadFields || group->text(0) == QStringLiteral("Payload Fields");
        for (int fieldIndex = 0; fieldIndex < group->childCount(); ++fieldIndex)
        {
            const QTreeWidgetItem *field = group->child(fieldIndex);
            foundDeclaredPayloadSize = foundDeclaredPayloadSize ||
                (field->text(0) == QStringLiteral("payload_size") && field->text(1) == QStringLiteral("255"));
        }
    }
    require(foundDeclaredPayloadSize, "truncated FDILink frame reports its declared payload size");
    require(!foundPayloadFields, "truncated FDILink frame stops before payload-field decoding");

    parser.close();
    processEventsFor(100);
}

void testRawDataParserUsesHardwareTemperatureSourceNames()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary temperature raw parser session directory");
    QDir dir(sessionDir.path());
    require(dir.mkpath(QStringLiteral("raw")), "temporary temperature raw directory can be created");

    const QByteArray laserResponse = QByteArray::fromHex("01030400010002b844");
    const QByteArray systemResponse = QByteArray::fromHex("01031000010002000300040005000600070008");
    writeUnifiedRawPayloadFile(
        dir.filePath(QStringLiteral("raw/laser_temperature_controller.dat")),
        0x0310,
        laserResponse,
        VaporView::SessionRawDat::kSourceLaserTemperatureController);
    writeUnifiedRawPayloadFile(
        dir.filePath(QStringLiteral("raw/system_temperature_controller.dat")),
        VaporView::SessionRawDat::kRecordTypeSystemTemperatureMeasuredValues,
        systemResponse,
        VaporView::SessionRawDat::kSourceSystemTemperatureController);

    RawDataParserWindow parser;
    parser.setEnglish(false);
    parser.show();
    require(waitForWindowExposed(&parser), "temperature raw parser window becomes exposed");
    require(parser.openSessionPath(sessionDir.path()), "raw parser accepts temperature raw session directory");

    auto *table = parser.findChild<QTableView *>();
    require(table != nullptr, "raw parser exposes the record table");
    require(processEventsUntil(5000, [table]() {
                return table->model() && table->model()->rowCount() == 2;
            }),
            "temperature raw records are indexed");

    auto *deviceCombo = parser.findChild<QComboBox *>();
    require(deviceCombo != nullptr, "raw parser exposes the device filter combo");
    require(deviceCombo->findText(QStringLiteral("RD105")) >= 0 &&
                deviceCombo->findText(QStringLiteral("AI-8288")) >= 0,
            "raw parser lists temperature controller model names");

    const int systemIndex = deviceCombo->findData(
        static_cast<int>(VaporView::SessionRawDat::kSourceSystemTemperatureController));
    require(systemIndex >= 0, "system temperature source filter exists");
    deviceCombo->setCurrentIndex(systemIndex);
    require(processEventsUntil(1000, [table]() {
                return table->model() && table->model()->rowCount() == 1;
            }),
            "system temperature source filter applies");
    require(table->model()->index(0, 3).data().toString() == QStringLiteral("AI-8288") &&
                table->model()->index(0, 4).data().toString() == QStringLiteral("AI-8288 测量值"),
            "system temperature raw record uses the hardware model name");

    const int laserIndex = deviceCombo->findData(
        static_cast<int>(VaporView::SessionRawDat::kSourceLaserTemperatureController));
    require(laserIndex >= 0, "laser temperature source filter exists");
    deviceCombo->setCurrentIndex(laserIndex);
    require(processEventsUntil(1000, [table]() {
                return table->model() && table->model()->rowCount() == 1;
            }),
            "laser temperature source filter applies");
    const QString laserType = table->model()->index(0, 4).data().toString();
    require(table->model()->index(0, 3).data().toString() == QStringLiteral("RD105") &&
                laserType.contains(QStringLiteral("RD105")),
            "laser temperature raw record uses the hardware model name");

    parser.close();
    processEventsFor(100);
}

void testSessionViewerShowsRecoveredWaveformCatalogWarning()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary recovered waveform warning session directory");
    writeTrajectorySessionWithRawTcpPeaks(sessionDir.path());

    const QString rawPath = QDir(sessionDir.path()).filePath(QStringLiteral("raw/tcp_wave.dat"));
    QFile rawFile(rawPath);
    require(rawFile.exists(), "temporary raw tcp wave file exists before truncation");
    require(rawFile.open(QIODevice::ReadWrite), "temporary raw tcp wave file can be reopened for truncation");
    const qint64 originalSize = rawFile.size();
    require(originalSize > 2, "temporary raw tcp wave file is large enough to truncate");
    require(rawFile.resize(originalSize - 2), "temporary raw tcp wave file can be truncated at EOF");
    rawFile.close();

    SessionViewerWindow viewer;
    viewer.setEnglish(true);
    viewer.resize(1280, 800);
    viewer.show();
    processEventsFor(50);

    require(viewer.openSessionPath(sessionDir.path()), "session viewer loads session with recovered raw tcp wave tail");
    processEventsFor(100);

    auto *statusLabel = viewer.findChild<QLabel *>(QStringLiteral("sessionViewerStatusLabel"));
    require(statusLabel != nullptr, "session viewer exposes a status label");
    const QString statusText = statusLabel->text();
    const QString statusToolTip = statusLabel->toolTip();
    const bool warningVisible =
        statusText.contains(QStringLiteral("Truncated final raw DAT record")) ||
        statusToolTip.contains(QStringLiteral("Truncated final raw DAT record"));
    require(warningVisible, "session waveform recovery warning remains visible in the session viewer UI");
    require(statusText.contains(QStringLiteral("Warning")) || statusToolTip.contains(QStringLiteral("Truncated final raw DAT record")),
            "session viewer labels recovered waveform catalog as a warning");

    viewer.close();
    processEventsFor(100);
}

void testWaveformIndexContinuesWhileGuiIsBusy()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary independent waveform indexing session");
    writeTrajectorySessionWithRawTcpPeaks(sessionDir.path());
    const QString rawPath = sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat"));
    const QString cachePath = rawPath + QStringLiteral(".vvidx");
    constexpr int frameCount = 4096;
    QVector<quint64> timestamps(frameCount);
    for (int index = 0; index < frameCount; ++index)
        timestamps[index] = 1782446035573000ULL + static_cast<quint64>(index) * 1000;
    writeMinimalRawTcpWaveFile(rawPath, timestamps);

    SessionViewerWindow viewer;
    viewer.setEnglish(true);
    viewer.resize(1280, 800);
    viewer.show();
    require(waitForWindowExposed(&viewer), "independent index viewer is exposed");
    bool blockedGuiDuringIndex = false;
    bool indexedWhileGuiBusy = false;
    QTimer discoverProgress;
    QObject::connect(&discoverProgress, &QTimer::timeout, &viewer, [&] {
        auto* dialog = viewer.findChild<QProgressDialog *>();
        if (!dialog) return;
        discoverProgress.stop();
        for (auto* bar : dialog->findChildren<QProgressBar *>())
        {
            QObject::connect(bar, &QProgressBar::valueChanged, &viewer, [&](int) {
                auto* status = viewer.findChild<QLabel *>(QStringLiteral("sessionViewerStatusLabel"));
                if (blockedGuiDuringIndex || !status ||
                    !status->text().startsWith(QStringLiteral("Indexing waveform data..."))) return;
                blockedGuiDuringIndex = true;
                // Simulate a slow GUI handler without pumping events. The
                // worker must still finish and atomically publish its index.
                QElapsedTimer timeout;
                timeout.start();
                while (!QFileInfo::exists(cachePath) && timeout.elapsed() < 3000)
                    QThread::msleep(10);
                indexedWhileGuiBusy = QFileInfo::exists(cachePath);
            });
        }
    });
    discoverProgress.start(0);
    const bool loaded = viewer.openSessionPath(sessionDir.path());
    discoverProgress.stop();
    viewer.close();
    processEventsFor(100);
    require(loaded, "independent index viewer loads all waveform data");
    require(blockedGuiDuringIndex && indexedWhileGuiBusy,
            "waveform indexing completes while the GUI cannot process events");
    require(QFileInfo(cachePath).size() == 76 + frameCount * 24,
            "independent indexing preserves every frame in its cache");
}

void testCsvHighlightsStartAtTopWhenLoadingAndChangingFrames()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary CSV follow session directory");
    writeTrajectorySessionWithRawTcpPeaks(sessionDir.path());
    writeMinimalRawTcpWaveFile(sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat")),
                              {1782446036573400ULL, 1782446038573500ULL});

    SessionViewerWindow viewer;
    viewer.setEnglish(true);
    viewer.resize(1280, 1000);
    viewer.show();
    require(waitForWindowExposed(&viewer), "CSV follow viewer is exposed");
    require(viewer.openSessionPath(sessionDir.path()), "CSV follow viewer loads waveform and CSV data");
    processEventsFor(100);

    auto *table = viewer.findChild<QTableView *>(QStringLiteral("sessionViewerCsvTable"));
    require(table && table->rowAt(0) == 1,
            "initial waveform load puts its matching CSV pair in the first two visible rows");
    require(!table->model()->index(1, 1).data().toString().isEmpty() &&
                !table->model()->index(2, 1).data().toString().isEmpty(),
            "initial waveform load highlights both CSV matches");
    require(QMetaObject::invokeMethod(&viewer, "onFrameSpinChanged", Q_ARG(int, 2)),
            "CSV follow viewer changes waveform frames");
    processEventsFor(100);
    require(table->rowAt(0) == 2 && !table->model()->index(3, 1).data().toString().isEmpty(),
            "changing to the last waveform frame keeps both CSV matches at the top");
    viewer.close();
    processEventsFor(100);
}

void testFrameSliderReleaseRestoresDetails()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary slider release session directory");
    writeTrajectorySessionWithRawTcpPeaks(sessionDir.path());

    SessionViewerWindow viewer;
    viewer.setEnglish(true);
    viewer.resize(1280, 1000);
    viewer.show();
    require(waitForWindowExposed(&viewer), "slider release viewer is exposed");
    require(viewer.openSessionPath(sessionDir.path()), "slider release viewer loads waveform data");
    auto *slider = viewer.findChild<QSlider *>();
    auto *spin = viewer.findChild<QSpinBox *>();
    auto *table = viewer.findChild<QTableView *>(QStringLiteral("sessionViewerCsvTable"));
    auto *frameInfo = viewer.findChild<QLabel *>(QStringLiteral("sessionViewerFrameInfoLabel"));
    auto *frameTime = viewer.findChild<QLabel *>(QStringLiteral("sessionViewerFrameTimeLabel"));
    auto *frameIndex = viewer.findChild<QLabel *>(QStringLiteral("sessionViewerFrameIndexLabel"));
    require(slider && spin && table && frameInfo && frameTime && frameIndex,
            "slider release controls, annotations and details are available");
    const auto requireAnnotations = [&](int frame) {
        const quint64 timestampUs = 1782446035573000ULL + static_cast<quint64>(frame - 1) * 1000000ULL;
        const QDateTime time = QDateTime::fromMSecsSinceEpoch(timestampUs / 1000ULL, QTimeZone::UTC).toLocalTime();
        require(frameIndex->text() == QString::number(frame) &&
                    frameTime->text() == time.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral(".573000"),
                "preview and committed annotations follow the actual waveform timestamp and frame index");
    };

    const auto requireCommittedFrame = [&](int frame) {
        processEventsFor(50);
        const QString text = frameInfo->text();
        require(!text.contains(QStringLiteral("Previewing")) && text.contains(QStringLiteral("min=")) &&
                    text.contains(QStringLiteral("max=")) && text.contains(QStringLiteral("peak=")) &&
                    text.contains(QStringLiteral("tcp_wave.dat")) && text.contains(QStringLiteral("CSV row")),
                "releasing the slider restores frame details and CSV timing");
        require(slider->value() == frame && spin->value() == frame,
                "released slider and frame number stay synchronized");
        requireAnnotations(frame);
        auto *peakPlot = viewer.findChild<QWidget *>(QStringLiteral("sessionViewerPeakPlot"));
        const QImage peakImage = peakPlot->grab().toImage();
        const int plotTop = peakImage.height() - qRound(108 * peakImage.devicePixelRatio());
        const QColor tagFill = VaporView::appThemeColor(VaporView::AppThemeColor::PlotCurrentGuideLabelFill, false);
        QRect timeTag;
        for (int y = 0; y < plotTop; ++y)
            for (int x = 0; x < peakImage.width(); ++x)
                if (peakImage.pixelColor(x, y).rgb() == tagFill.rgb())
                    timeTag |= QRect(x, y, 1, 1);
        const QFontMetrics timeMetrics(VaporView::Ground::SessionUi::numericFontFrom(peakPlot->font()));
        require(timeTag.width() >= qRound(timeMetrics.horizontalAdvance(frameTime->text()) * peakImage.devicePixelRatio()),
                "loaded waveform timestamps reach the peak crosshair time tag after preview and release");
        const int firstCsvRow = std::max(0, frame - 2);
        require(table->rowAt(0) == firstCsvRow &&
                    !table->model()->index(frame - 1, 1).data().toString().isEmpty(),
                "released slider highlights the matching CSV rows at the top");
    };

    requireAnnotations(1);
    processEventsFor(100);
    requireAnnotations(1);
    viewer.setEnglish(false);
    requireAnnotations(1);
    viewer.setEnglish(true);
    requireAnnotations(1);

    slider->setSliderDown(true);
    slider->setSliderPosition(3);
    require(frameInfo->text().contains(QStringLiteral("Previewing")) && spin->value() == 3,
            "dragging previews the selected waveform before release");
    requireAnnotations(3);
    slider->setSliderDown(false);
    requireCommittedFrame(3);

    slider->setSliderDown(true);
    slider->setSliderPosition(4);
    slider->setSliderPosition(3);
    require(frameInfo->text().contains(QStringLiteral("Previewing")) && spin->value() == 3,
            "dragging back to the committed frame still shows its preview while held");
    slider->setSliderDown(false);
    requireCommittedFrame(3);

    QStyleOptionSlider sliderOption;
    sliderOption.initFrom(slider);
    sliderOption.orientation = slider->orientation();
    sliderOption.state |= QStyle::State_Horizontal;
    sliderOption.minimum = slider->minimum();
    sliderOption.maximum = slider->maximum();
    sliderOption.sliderPosition = slider->sliderPosition();
    sliderOption.sliderValue = slider->value();
    const QPoint dragStart = slider->style()->subControlRect(
        QStyle::CC_Slider, &sliderOption, QStyle::SC_SliderHandle, slider).center();
    sliderOption.sliderPosition = 4;
    sliderOption.sliderValue = 4;
    const QPoint dragEnd = slider->style()->subControlRect(
        QStyle::CC_Slider, &sliderOption, QStyle::SC_SliderHandle, slider).center();
    QMouseEvent sliderPress(QEvent::MouseButtonPress, dragStart, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(slider, &sliderPress);
    QMouseEvent sliderMove(QEvent::MouseMove, dragEnd, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(slider, &sliderMove);
    require(slider->isSliderDown() && frameInfo->text().contains(QStringLiteral("Previewing")) && spin->value() == 4,
            "mouse drag previews its waveform while held");
    requireAnnotations(4);
    QMouseEvent sliderRelease(QEvent::MouseButtonRelease, dragEnd, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(slider, &sliderRelease);
    requireCommittedFrame(4);
    QKeyEvent previousFrame(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QCoreApplication::sendEvent(slider, &previousFrame);
    requireCommittedFrame(3);
    spin->setValue(1);
    requireCommittedFrame(1);
    require(viewer.openSessionPath(sessionDir.path()), "viewer reopens the session with its cached waveform index");
    requireAnnotations(1);
    viewer.close();
    processEventsFor(100);
}

void testCsvViewportUsesNeutralBackground(SessionViewerWindow& viewer)
{
    auto *table = viewer.findChild<QTableView *>(QStringLiteral("sessionViewerCsvTable"));
    require(table != nullptr, "session viewer CSV table exists");
    require(table->viewport() != nullptr, "session viewer CSV viewport exists");

    const QColor base = table->viewport()->palette().color(QPalette::Base);
    require(base.lightness() > 180 || VaporView::isDarkThemeEnabled(), "CSV empty viewport uses a light neutral background");
    require(base.saturation() < 24 || VaporView::isDarkThemeEnabled(), "CSV empty viewport is not tinted blue");
}

void testCsvSelectionUsesThemeAccent(SessionViewerWindow& viewer, bool dark)
{
    auto *table = viewer.findChild<QTableView *>(QStringLiteral("sessionViewerCsvTable"));
    require(table != nullptr, "session viewer CSV table exists for selection theme");

    const QColor actualHighlight = table->palette().color(QPalette::Highlight);
    const QColor expectedHighlight = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
    require(actualHighlight.name() == expectedHighlight.name(),
            dark ? "CSV dark selection uses orange theme accent"
                 : "CSV light selection uses blue theme accent");

    const QColor actualText = table->palette().color(QPalette::HighlightedText);
    const QColor expectedText = dark
        ? VaporView::appThemeColor(VaporView::AppThemeColor::TableText, false)
        : VaporView::appThemeColor(VaporView::AppThemeColor::TextInverse, false);
    require(actualText.name() == expectedText.name(), "CSV selection text keeps contrast against accent background");

    if (dark)
    {
        require(actualHighlight.red() > actualHighlight.blue() + 24,
                "CSV dark selection accent is orange-tinted");
    }
    else
    {
        require(actualHighlight.blue() > actualHighlight.red() + 48,
                "CSV light selection accent is blue-tinted");
    }
}

void testWaveformEmptyPlotIsNotRed(SessionViewerWindow& viewer)
{
    auto *plot = viewer.findChild<QWidget *>(QStringLiteral("sessionViewerWaveformPlot"));
    require(plot != nullptr, "session viewer waveform plot exists");
    require(plot->width() > 0 && plot->height() > 0, "session viewer waveform plot has size");

    const QImage image = plot->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    require(!image.isNull(), "session viewer waveform plot renders");
    const int redDominantPixels = countRedDominantPixels(image);
    const int totalPixels = image.width() * image.height();
    require(redDominantPixels < totalPixels / 100, "session viewer waveform grid is not red-dominant");
}

void requireSessionViewerTitleBarWindowButtonsWork(SessionViewerWindow& viewer)
{
    auto *minimizeButton = viewer.findChild<QToolButton *>(QStringLiteral("windowMinimizeButton"));
    auto *maximizeButton = viewer.findChild<QToolButton *>(QStringLiteral("windowMaximizeButton"));
    require(minimizeButton != nullptr, "data viewer minimize button exists");
    require(maximizeButton != nullptr, "data viewer maximize button exists");
    require(minimizeButton->isEnabled(), "data viewer minimize button is enabled");
    require(maximizeButton->isEnabled(), "data viewer maximize button is enabled");
    viewer.raise();
    viewer.activateWindow();
    require(waitForWindowExposed(&viewer), "data viewer window is exposed before title-bar interaction");

    clickWidgetCenterThroughWindow(maximizeButton);
    require(processEventsUntil(1000, [&viewer]() {
                return viewer.isMaximized() ||
                       viewer.windowState().testFlag(Qt::WindowMaximized);
            }),
            "data viewer maximize button maximizes window");

    clickWidgetCenterThroughWindow(maximizeButton);
    require(processEventsUntil(1000, [&viewer]() {
                return viewer.isVisible() &&
                       !viewer.isMinimized() &&
                       !viewer.isMaximized() &&
                       !viewer.windowState().testFlag(Qt::WindowMinimized) &&
                       !viewer.windowState().testFlag(Qt::WindowMaximized);
            }),
            "data viewer maximize button restores window");

    clickWidgetCenterThroughWindow(minimizeButton);
    require(processEventsUntil(1000, [&viewer]() {
                return viewer.isMinimized() ||
                       viewer.windowState().testFlag(Qt::WindowMinimized);
            }),
            "data viewer minimize button minimizes window");

    viewer.showNormal();
    require(processEventsUntil(1000, [&viewer]() {
                return viewer.isVisible() && !viewer.isMinimized() && !viewer.isMaximized();
            }),
            "data viewer returns to a visible normal state after window-button checks");
}

void testSessionViewerTrajectoryActionLifetime()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary trajectory session directory");
    writeMinimalTrajectorySession(sessionDir.path());

    SessionViewerWindow viewer;
    viewer.resize(1280, 800);
    viewer.show();
    processEventsFor(300);

    require(viewer.openSessionPath(sessionDir.path()), "session viewer loads temporary trajectory session");
    processEventsFor(300);
    require(QMetaObject::invokeMethod(&viewer, "onViewTrajectoryClicked", Qt::DirectConnection),
            "session viewer invokes trajectory viewer action");

    TrajectoryViewerDialog *dialog = nullptr;
    require(processEventsUntil(2000, [&dialog]() {
                for (QWidget *widget : QApplication::allWidgets())
                {
                    dialog = qobject_cast<TrajectoryViewerDialog *>(widget);
                    if (dialog && dialog->isVisible())
                    {
                        return true;
                    }
                }
                return false;
            }),
            "trajectory viewer opens from loaded data viewer session");

    dialog->close();
    processEventsFor(700);
    viewer.close();
    processEventsFor(100);
}

void testTrajectoryViewerInitialHeatLegendFromPendingPeaks()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary trajectory peak session directory");
    writeTrajectorySessionWithRawTcpPeaks(sessionDir.path());

    SessionViewerWindow viewer;
    viewer.resize(1280, 800);
    viewer.show();
    processEventsFor(50);

    require(viewer.openSessionPath(sessionDir.path()), "session viewer loads trajectory session with raw tcp peaks");
    require(QMetaObject::invokeMethod(&viewer, "onViewTrajectoryClicked", Qt::DirectConnection),
            "trajectory viewer opens while peak calculation may still be pending");

    auto *dialog = visibleTrajectoryViewerDialog();
    require(dialog != nullptr, "trajectory viewer is visible after immediate open");
    auto *heatLegendCard = dialog->findChild<QFrame *>(QStringLiteral("trajectoryHeatLegendCard"));
    auto *heatGradientBar = dialog->findChild<QWidget *>(QStringLiteral("trajectoryHeatGradientBar"));
    require(heatLegendCard != nullptr, "initial trajectory heat legend card exists");
    require(heatGradientBar != nullptr, "initial trajectory heat gradient bar exists");
    require(heatLegendCard->isVisible(), "initial trajectory heat legend is visible before deferred refresh");
    require(heatGradientBar->isVisible(), "initial trajectory heat gradient is visible before deferred refresh");

    dialog->close();
    processEventsFor(100);
    viewer.close();
    processEventsFor(100);
}

void testTrajectoryViewerUsesSidebarLayout()
{
    TrajectoryViewerDialog dialog;
    dialog.resize(1080, 680);
    dialog.show();
    processEventsFor(200);

    require(dialog.objectName() == QStringLiteral("trajectoryViewerDialog"),
            "trajectory viewer dialog has scoped style object name");
    auto *sidebar = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerSidebar"));
    auto *sidebarCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryViewerSidebarCard"));
    auto *sidebarTitleBar = sidebarCard
        ? sidebarCard->findChild<QWidget *>(QStringLiteral("sectionTitleBar"))
        : nullptr;
    auto *sidebarIcon = sidebarTitleBar
        ? sidebarTitleBar->findChild<QLabel *>(QStringLiteral("trajectorySidebarTitleIcon"))
        : nullptr;
    auto *sidebarTitle = sidebarTitleBar
        ? sidebarTitleBar->findChild<QLabel *>(QStringLiteral("sectionTitleLabel"))
        : nullptr;
    auto *sidebarContent = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerSidebarContent"));
    auto *mapPanel = dialog.findChild<QFrame *>(QStringLiteral("trajectoryViewerMapPanel"));
    auto *map = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerMap"));
    auto *mapSourceCombo = dialog.findChild<QComboBox *>(QStringLiteral("trajectoryMapSourceCombo"));
    auto *heatLegendCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryHeatLegendCard"));
    auto *mapToolsCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryMapToolsCard"));
    auto *pointDetailCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryPointDetailCard"));
    auto *heatMetricButton = dialog.findChild<QToolButton *>(QStringLiteral("trajectoryHeatMetricButton"));
    auto *heatMetricMenu = dialog.findChild<VaporView::SingleLevelPopupMenu *>(QStringLiteral("trajectoryHeatMetricMenu"));
    auto *heatGradientBar = dialog.findChild<QWidget *>(QStringLiteral("trajectoryHeatGradientBar"));
    auto *heatPaletteButton = dialog.findChild<QToolButton *>(QStringLiteral("trajectoryHeatPaletteButton"));
    auto *heatPaletteMenu = dialog.findChild<VaporView::SingleLevelPopupMenu *>(QStringLiteral("trajectoryHeatPaletteMenu"));
    auto *pointDetailCloseButton = dialog.findChild<QToolButton *>(QStringLiteral("trajectoryPointDetailCloseButton"));
    auto *filterCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryFilterCard"));
    auto *filterTitle = dialog.findChild<QLabel *>(QStringLiteral("trajectoryFilterTitle"));
    auto *filterEmptyLabel = dialog.findChild<QLabel *>(QStringLiteral("trajectoryFilterEmptyLabel"));
    auto *filterList = dialog.findChild<QWidget *>(QStringLiteral("trajectoryFilterList"));
    auto *peakCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryPeakCard"));
    auto *peakTitle = dialog.findChild<QLabel *>(QStringLiteral("trajectoryPeakTitle"));
    auto *peakSearchStartSpin = dialog.findChild<QSpinBox *>(QStringLiteral("trajectoryPeakSearchStartSpin"));
    auto *peakSearchEndSpin = dialog.findChild<QSpinBox *>(QStringLiteral("trajectoryPeakSearchEndSpin"));
    auto *peakFilterModeCombo = dialog.findChild<QComboBox *>(QStringLiteral("trajectoryPeakFilterModeCombo"));
    auto *peakFilterMinEdit = dialog.findChild<QLineEdit *>(QStringLiteral("trajectoryPeakFilterMinEdit"));
    auto *peakFilterMaxEdit = dialog.findChild<QLineEdit *>(QStringLiteral("trajectoryPeakFilterMaxEdit"));
    for (QLabel *titleLabel : {sidebarTitle, filterTitle, peakTitle})
    {
        const bool visualTitle = dynamic_cast<VaporView::VisualTextLabel *>(titleLabel) != nullptr;
        require(titleLabel != nullptr &&
                    (visualTitle ||
                     titleLabel->textInteractionFlags().testFlag(Qt::TextSelectableByMouse)) &&
                    titleLabel->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard),
                "trajectory card titles are selectable and copyable");
    }
    auto *peakApplyButton = dialog.findChild<QPushButton *>(QStringLiteral("trajectoryPeakApplyButton"));
    const auto pointDetailActionButtons = dialog.findChildren<QToolButton *>(QStringLiteral("trajectoryPointDetailActionButton"));
    QToolButton *filterCurrentButton = nullptr;
    QToolButton *filterStartButton = nullptr;
    QToolButton *filterEndButton = nullptr;
    for (QToolButton *button : pointDetailActionButtons)
    {
        const QString role = button->property("pointActionRole").toString();
        if (role == QStringLiteral("filter-current"))
        {
            filterCurrentButton = button;
        }
        else if (role == QStringLiteral("filter-start"))
        {
            filterStartButton = button;
        }
        else if (role == QStringLiteral("filter-end"))
        {
            filterEndButton = button;
        }
    }
    const auto heatCaptionLabels = dialog.findChildren<QLabel *>(QStringLiteral("trajectoryHeatLegendCaption"));
    auto *trackWidthSlider = dialog.findChild<QSlider *>(QStringLiteral("trajectoryTrackWidthSlider"));
    auto *pointSizeSlider = dialog.findChild<QSlider *>(QStringLiteral("trajectoryPointSizeSlider"));
    const auto visibilityToggles = dialog.findChildren<QPushButton *>(QStringLiteral("trajectoryVisibilityToggle"));
    QPushButton *showRouteButton = nullptr;
    QPushButton *showPointsButton = nullptr;
    for (QPushButton *toggle : visibilityToggles)
    {
        if (toggle->property("visibilityRole").toString() == QStringLiteral("route"))
        {
            showRouteButton = toggle;
        }
        else if (toggle->property("visibilityRole").toString() == QStringLiteral("points"))
        {
            showPointsButton = toggle;
        }
    }
    auto *summaryLabel = dialog.findChild<QLabel *>(QStringLiteral("trajectorySidebarSummaryLabel"));
    auto *detailLabel = dialog.findChild<QLabel *>(QStringLiteral("trajectoryPointDetailLabel"));

    require(sidebarCard != nullptr, "trajectory viewer sidebar card exists");
    require(sidebarTitleBar != nullptr, "trajectory viewer sidebar title bar exists");
    require(sidebarIcon != nullptr, "trajectory viewer sidebar title icon exists");
    require(sidebarTitle != nullptr, "trajectory viewer sidebar title label exists");
    require(sidebarContent != nullptr, "trajectory viewer sidebar body exists");
    require(sidebar != nullptr, "trajectory viewer sidebar exists");
    require(mapPanel != nullptr, "trajectory viewer map panel exists");
    require(map != nullptr, "trajectory viewer map exists");
    require(mapSourceCombo != nullptr, "trajectory viewer map source control exists");
    require(mapSourceCombo->property("usesSingleLevelPopupMenu").toBool(),
            "trajectory map source combo uses the stable single-level popup");
    auto *mapSourceMenu = mapSourceCombo->findChild<VaporView::SingleLevelPopupMenu *>(
        QStringLiteral("singleLevelComboPopupMenu"));
    require(mapSourceMenu != nullptr, "trajectory map source combo owns its single-level popup");
    mapSourceCombo->showPopup();
    require(processEventsUntil(1000, [mapSourceMenu]() {
                return mapSourceMenu->isVisible() && mapSourceMenu->rows().size() == 3;
            }),
            "trajectory map source popup opens with all three rows");
    const QList<VaporView::SingleLevelPopupMenuRow *> mapSourceRows = mapSourceMenu->rows();
    require(mapSourceRows.size() == 3, "trajectory map source popup keeps one row per provider");
    for (int i = 0; i < mapSourceRows.size(); ++i)
    {
        require(mapSourceRows[i]->height() == 40,
                "trajectory map source popup rows keep a stable height");
        if (i > 0)
        {
            const QRect previousRect(mapSourceRows[i - 1]->mapTo(mapSourceMenu, QPoint(0, 0)),
                                     mapSourceRows[i - 1]->size());
            const QRect currentRect(mapSourceRows[i]->mapTo(mapSourceMenu, QPoint(0, 0)),
                                    mapSourceRows[i]->size());
            require(!previousRect.intersects(currentRect) && previousRect.bottom() < currentRect.top(),
                    "trajectory map source popup rows never overlap");
        }
    }
    mapSourceCombo->hidePopup();
    require(heatLegendCard != nullptr, "trajectory viewer floating heat legend card exists");
    require(mapToolsCard != nullptr, "trajectory viewer floating map tools card exists");
    require(pointDetailCard != nullptr, "trajectory viewer floating point detail card exists");
    require(heatMetricButton != nullptr, "trajectory viewer floating heat metric selector exists");
    require(heatMetricMenu != nullptr, "trajectory viewer floating heat metric menu exists");
    require(heatGradientBar != nullptr, "trajectory viewer floating heat gradient bar exists");
    require(heatPaletteButton != nullptr, "trajectory viewer heat palette control exists");
    require(heatPaletteMenu != nullptr, "trajectory viewer heat palette menu exists");
    require(pointDetailCloseButton != nullptr, "trajectory viewer point detail close button exists");
    require(filterCard != nullptr, "trajectory viewer filter card exists");
    require(filterTitle != nullptr, "trajectory viewer filter title exists");
    require(filterEmptyLabel != nullptr, "trajectory viewer filter empty text exists");
    require(filterList != nullptr, "trajectory viewer filter list exists");
    require(peakCard != nullptr, "trajectory viewer peak settings card exists");
    require(peakTitle != nullptr, "trajectory viewer peak settings title exists");
    require(peakSearchStartSpin != nullptr, "trajectory viewer peak search start control exists");
    require(peakSearchEndSpin != nullptr, "trajectory viewer peak search end control exists");
    require(peakFilterModeCombo != nullptr, "trajectory viewer peak filter mode control exists");
    requireComboPopupStyled(peakFilterModeCombo,
                            "trajectory peak filter combo uses the shared popup styling helper");
    require(peakFilterMinEdit != nullptr, "trajectory viewer peak filter min control exists");
    require(peakFilterMaxEdit != nullptr, "trajectory viewer peak filter max control exists");
    require(peakApplyButton != nullptr, "trajectory viewer peak settings apply button exists");
    require(filterCurrentButton != nullptr, "trajectory viewer filter-current point action exists");
    require(filterStartButton != nullptr, "trajectory viewer filter-start point action exists");
    require(filterEndButton != nullptr, "trajectory viewer filter-end point action exists");
    require(trackWidthSlider != nullptr, "trajectory viewer track width control exists");
    require(pointSizeSlider != nullptr, "trajectory viewer point size control exists");
    require(showRouteButton != nullptr, "trajectory viewer route visibility toggle exists");
    require(showPointsButton != nullptr, "trajectory viewer point visibility toggle exists");
    require(summaryLabel != nullptr, "trajectory viewer summary label exists");
    require(detailLabel != nullptr, "trajectory viewer detail label exists");
    require(dialog.findChild<QLabel *>(QStringLiteral("trajectorySidebarStatusLabel")) == nullptr,
            "trajectory viewer no longer puts map loading status in the sidebar");
    const auto sidebarToolButtons = sidebar->findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    require(sidebarToolButtons.size() >= 4, "trajectory viewer sidebar contains map tool buttons");
    require(sidebar->findChildren<QProgressBar *>().isEmpty(),
            "trajectory viewer no longer puts map loading progress in the sidebar");
    const auto sidebarActionButtons = sidebar->findChildren<QPushButton *>(QStringLiteral("trajectorySidebarActionButton"));
    require(sidebarActionButtons.size() == 2, "trajectory viewer sidebar only keeps copy and export actions");
    require(std::none_of(sidebarActionButtons.cbegin(), sidebarActionButtons.cend(), [](const QPushButton *button) {
                return button && (button->text() == QStringLiteral("Play") || button->text() == QStringLiteral("播放"));
            }),
            "trajectory viewer removes the unused playback button");

    require(sidebarCard->isAncestorOf(sidebarTitleBar), "sidebar title bar is inside card");
    require(sidebarCard->isAncestorOf(sidebar), "sidebar body scroll area is inside card");
    require(sidebar->isAncestorOf(sidebarContent), "sidebar content is inside scroll body");
    require(sidebar->isAncestorOf(peakCard), "trajectory peak settings card is in the sidebar");
    require(peakCard->isAncestorOf(peakTitle), "trajectory peak settings title is inside peak card");
    require(peakCard->isAncestorOf(peakSearchStartSpin), "trajectory peak search start is inside peak card");
    require(peakCard->isAncestorOf(peakSearchEndSpin), "trajectory peak search end is inside peak card");
    require(peakCard->isAncestorOf(peakFilterModeCombo), "trajectory peak filter mode is inside peak card");
    require(peakCard->isAncestorOf(peakFilterMinEdit), "trajectory peak filter min is inside peak card");
    require(peakCard->isAncestorOf(peakFilterMaxEdit), "trajectory peak filter max is inside peak card");
    require(peakCard->isAncestorOf(peakApplyButton), "trajectory peak apply button is inside peak card");
    require(sidebar->isAncestorOf(filterCard), "trajectory filter card is in the sidebar");
    require(filterCard->isAncestorOf(filterTitle), "trajectory filter title is inside filter card");
    require(filterCard->isAncestorOf(filterEmptyLabel), "trajectory filter empty text is inside filter card");
    require(filterCard->isAncestorOf(filterList), "trajectory filter list is inside filter card");
    require(sidebarCard->layout() != nullptr, "sidebar card layout exists");
    require(sidebarCard->layout()->contentsMargins() == QMargins(1, 1, 1, 1),
            "sidebar card preserves visible rounded border");
    require(sidebarTitleBar->minimumHeight() == sidebarTitleBar->maximumHeight()
                && sidebarTitleBar->minimumHeight() >= 40,
            "sidebar title bar keeps home card fixed height");
    require(sidebarContent->layout() != nullptr, "sidebar content layout exists");
    require(sidebarContent->layout()->contentsMargins() == QMargins(12, 12, 12, 12),
            "sidebar body uses home card interior padding");
    require(mapSourceCombo->minimumWidth() == 160, "map source combo keeps readable minimum width");
    require(mapSourceCombo->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding,
            "map source combo expands within sidebar controls");
    require(sidebar->isAncestorOf(mapSourceCombo), "map source control is in sidebar");
    require(peakSearchStartSpin->minimum() == 0 && peakSearchEndSpin->minimum() == 0,
            "trajectory peak search controls allow full-frame end value");
    require(peakSearchEndSpin->specialValueText().contains(QStringLiteral("整帧")) ||
                peakSearchEndSpin->specialValueText().contains(QStringLiteral("Full")),
            "trajectory peak search end exposes full-frame special value");
    require(peakFilterModeCombo->count() == 4,
            "trajectory peak filter mode mirrors the data viewer filter modes");
    dialog.setPeakSettings(100, 0, 2, 0.125, 0.875);
    processEventsFor(50);
    require(peakSearchStartSpin->value() == 100 && peakSearchEndSpin->value() == 0,
            "trajectory peak controls sync search range from the data viewer");
    require(peakFilterModeCombo->currentData().toInt() == 2,
            "trajectory peak controls sync filter mode from the data viewer");
    require(peakFilterMinEdit->text().startsWith(QStringLiteral("0.125")) &&
                peakFilterMaxEdit->text().startsWith(QStringLiteral("0.875")),
            "trajectory peak controls sync numeric filter bounds from the data viewer");
    require(peakFilterMinEdit->isEnabled() && peakFilterMaxEdit->isEnabled(),
            "trajectory peak range filter enables range bound editors");
    int requestedPeakStart = -1;
    int requestedPeakEnd = -1;
    int requestedPeakMode = -1;
    double requestedPeakMin = 0.0;
    double requestedPeakMax = 0.0;
    QObject::connect(&dialog,
                     &TrajectoryViewerDialog::peakSettingsChangeRequested,
                     &dialog,
                     [&](int start, int end, int mode, double minValue, double maxValue) {
                         requestedPeakStart = start;
                         requestedPeakEnd = end;
                         requestedPeakMode = mode;
                         requestedPeakMin = minValue;
                         requestedPeakMax = maxValue;
                     });
    peakSearchStartSpin->setValue(250);
    peakSearchEndSpin->setValue(0);
    peakFilterMinEdit->setText(QStringLiteral("0.200000"));
    peakFilterMaxEdit->setText(QStringLiteral("0.700000"));
    peakApplyButton->click();
    processEventsFor(50);
    require(requestedPeakStart == 250 && requestedPeakEnd == 0 && requestedPeakMode == 2,
            "trajectory peak apply emits the shared peak search and filter mode");
    require(std::abs(requestedPeakMin - 0.2) < 1e-9 && std::abs(requestedPeakMax - 0.7) < 1e-9,
            "trajectory peak apply emits the shared numeric filter bounds");
    require(heatLegendCard->minimumWidth() >= 390 && heatGradientBar->minimumWidth() >= 260,
            "heat palette card gives the gradient a longer readable span");
    require(heatCaptionLabels.size() >= 3, "heat legend exposes min, middle, and max captions");
    require(heatPaletteButton->minimumSize() == QSize(28, 24) && heatPaletteButton->maximumSize() == QSize(28, 24),
            "heat palette button is reduced to a compact arrow selector");
    require(heatPaletteButton->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed,
            "heat palette button stays compact inside floating heat legend");
    require(heatPaletteButton->arrowType() == Qt::NoArrow && !heatPaletteButton->icon().isNull(),
            "heat palette button uses a single lucide chevron affordance");
    require(heatPaletteMenu->testAttribute(Qt::WA_TranslucentBackground),
            "heat palette menu uses the shared translucent popup background for rounded corners");
    require(heatPaletteMenu->cornerRadius() == 10 &&
                heatPaletteMenu->panelPadding() == 12 &&
                heatPaletteMenu->property("floatingPanelChrome").toBool() &&
                heatPaletteMenu->property("shadowMargin").toInt() == 22 &&
                heatPaletteMenu->property("shadowBottomMargin").toInt() == 50 &&
                heatPaletteMenu->styleSheet().contains(QStringLiteral("background-color: transparent; border: none; border-radius: 10px; padding: 12px 0px")),
            "heat palette menu uses the macOS-style floating single-level popup chrome");
    require(heatPaletteMenu->actions().size() == 3, "heat palette menu exposes the curated vivid ramps");
    require(heatMetricMenu->actions().size() == 4, "heat metric selector exposes peak, humidity, temperature, and pressure");
    require(heatMetricButton->arrowType() == Qt::NoArrow && !heatMetricButton->icon().isNull(),
            "heat metric button uses the same lucide chevron affordance as the heat palette selector");
    bool foundCheckedHeatPaletteRow = false;
    for (VaporView::SingleLevelPopupMenuRow *row : heatPaletteMenu->rows())
    {
        require(row != nullptr && !row->text().trimmed().isEmpty(), "heat palette menu text is visible");
        require(row->property("textAlignment").toString() == QStringLiteral("left"),
                "heat palette menu rows use the shared left-aligned selector layout");
        if (row->isChecked())
        {
            foundCheckedHeatPaletteRow = true;
            require(row->property("hasCheckIcon").toBool(),
                    "selected heat palette row shows the shared check indicator");
        }
    }
    require(foundCheckedHeatPaletteRow, "heat palette menu marks the selected ramp");
    bool foundCheckedHeatMetricRow = false;
    for (VaporView::SingleLevelPopupMenuRow *row : heatMetricMenu->rows())
    {
        require(row != nullptr && !row->text().trimmed().isEmpty(), "heat metric menu text is visible");
        require(row->property("textAlignment").toString() == QStringLiteral("left"),
                "heat metric menu rows use the shared left-aligned selector layout");
        if (row->isChecked())
        {
            foundCheckedHeatMetricRow = true;
            require(row->property("hasCheckIcon").toBool(),
                    "selected heat metric row shows the shared check indicator");
        }
    }
    require(foundCheckedHeatMetricRow, "heat metric menu marks the selected metric");
    require(map->isAncestorOf(heatLegendCard), "heat legend card floats inside the map");
    require(heatLegendCard->isAncestorOf(heatPaletteButton), "heat palette control is inside floating heat legend");
    require(!sidebar->isAncestorOf(heatPaletteButton), "heat palette control is no longer in sidebar");
    require(map->isAncestorOf(pointDetailCard), "point detail card floats inside the map");
    require(pointDetailCard->isAncestorOf(detailLabel), "point detail label is inside the floating card");
    require(pointDetailCard->isAncestorOf(pointDetailCloseButton), "point detail close button is inside the floating card");
    require(pointDetailCard->isAncestorOf(filterCurrentButton), "filter-current action is inside point detail card");
    require(pointDetailCard->isAncestorOf(filterStartButton), "filter-start action is inside point detail card");
    require(pointDetailCard->isAncestorOf(filterEndButton), "filter-end action is inside point detail card");
    require(!sidebar->isAncestorOf(detailLabel), "point detail label is no longer in sidebar");
    require(!pointDetailCard->isVisible(), "point detail card stays hidden until a point is clicked");
    require(map->layout() != nullptr && map->layout()->contentsMargins().bottom() >= 40,
            "point detail card leaves vertical room above the map footer data bar");
    require(pointDetailCloseButton->minimumSize() == QSize(24, 24)
                && pointDetailCloseButton->maximumSize() == QSize(24, 24),
            "point detail close button stays compact in the card corner");
    for (QToolButton *button : {filterCurrentButton, filterStartButton, filterEndButton})
    {
        require(button->text().isEmpty(), "point detail filter actions are icon-only");
        require(!button->toolTip().trimmed().isEmpty(), "point detail filter actions expose hover tooltip text");
        require(!button->icon().isNull(), "point detail filter actions use lucide flag icons");
        require(button->minimumSize() == QSize(24, 24) && button->maximumSize() == QSize(24, 24),
                "point detail filter actions stay compact");
    }
    require(QFile::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/resources/lucide/flag.svg")),
            "trajectory filter-current icon resource is deployed");
    require(QFile::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/resources/lucide/flag-triangle-left.svg")),
            "trajectory filter-start icon resource is deployed");
    require(QFile::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/resources/lucide/flag-triangle-right.svg")),
            "trajectory filter-end icon resource is deployed");
    require(trackWidthSlider->minimum() == 10 && trackWidthSlider->maximum() == 80,
            "track width slider exposes a bounded visual range");
    require(pointSizeSlider->minimum() == 20 && pointSizeSlider->maximum() == 120,
            "point size slider exposes a bounded visual range");
    require(map->isAncestorOf(mapToolsCard), "map tools card floats inside the map");
    require(mapToolsCard->isAncestorOf(trackWidthSlider), "track width control is in floating map tools card");
    require(mapToolsCard->isAncestorOf(pointSizeSlider), "point size control is in floating map tools card");
    require(mapToolsCard->isAncestorOf(showRouteButton), "route visibility toggle is in floating map tools card");
    require(mapToolsCard->isAncestorOf(showPointsButton), "point visibility toggle is in floating map tools card");
    require(!sidebar->isAncestorOf(trackWidthSlider), "track width control is no longer in sidebar");
    require(!sidebar->isAncestorOf(pointSizeSlider), "point size control is no longer in sidebar");
    require(showRouteButton->isCheckable() && showRouteButton->isChecked(),
            "route visibility is enabled by default");
    require(showPointsButton->isCheckable() && showPointsButton->isChecked(),
            "point visibility is enabled by default");
    require(showRouteButton->text().isEmpty() && showPointsButton->text().isEmpty(),
            "visibility toggles use icon-only buttons");
    require(!showRouteButton->icon().isNull() && !showPointsButton->icon().isNull(),
            "visibility toggles use lucide icons");
    require(showRouteButton->minimumSize() == QSize(24, 24)
                && showPointsButton->minimumSize() == QSize(24, 24),
            "visibility toggles stay compact beside the sliders");
    require(mapPanel->isAncestorOf(map), "map remains in the map panel");
    require(!sidebar->isAncestorOf(map), "map is not in sidebar");
    require(mapPanel->layout() != nullptr, "map panel layout exists");
    require(mapPanel->layout()->contentsMargins() == QMargins(0, 0, 0, 0),
            "map panel lets the map fill the rounded card");
    require(mapPanel->layout()->spacing() == 0,
            "map panel has no fixed legend spacing above the map");
    const QPoint sidebarOrigin = sidebarCard->mapTo(&dialog, QPoint(0, 0));
    const QPoint mapOrigin = mapPanel->mapTo(&dialog, QPoint(0, 0));
    require(sidebarOrigin.x() < mapOrigin.x(),
            "trajectory sidebar card is positioned to the left of the map panel");
    require(sidebarOrigin.y() == mapOrigin.y(),
            "trajectory sidebar card and map panel share the same row");
    require(map->height() >= 300, "trajectory map keeps usable vertical space");
    const QRect mapPanelContents = mapPanel->contentsRect();
    require(std::abs(map->geometry().left() - mapPanelContents.left()) <= 2
                && std::abs(map->geometry().top() - mapPanelContents.top()) <= 2,
            "trajectory map starts at the map panel content origin");
    require(std::abs(map->width() - mapPanelContents.width()) <= 2
                && std::abs(map->height() - mapPanelContents.height()) <= 2,
            "trajectory map fills the whole rounded map panel");
    const QString styleSheet = dialog.styleSheet();
    require(styleSheet.contains(QStringLiteral("QDialog#trajectoryViewerDialog")),
            "trajectory viewer stylesheet is scoped to dialog");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryViewerSidebarCard")),
            "trajectory viewer stylesheet includes sidebar card styling");
    require(styleSheet.contains(QStringLiteral("QWidget#sectionTitleBar")),
            "trajectory viewer stylesheet includes home-style section title bar");
    require(styleSheet.contains(QStringLiteral("QPushButton#trajectorySidebarActionButton")),
            "trajectory viewer stylesheet includes sidebar action button styling");
    require(styleSheet.contains(QStringLiteral("QLabel#trajectorySidebarTitleIcon")),
            "trajectory viewer stylesheet includes sidebar title icon styling");
    require(styleSheet.contains(QStringLiteral("QLabel#trajectorySidebarTitleIcon { background-color: transparent; border: none")),
            "trajectory sidebar title icon is not drawn with a blue badge background");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryPeakCard")),
            "trajectory viewer stylesheet includes sidebar peak settings card styling");
    require(styleSheet.contains(QStringLiteral("QSpinBox#trajectoryPeakSearchStartSpin")),
            "trajectory viewer stylesheet includes peak search spinbox styling");
    require(styleSheet.contains(QStringLiteral("QPushButton#trajectoryPeakApplyButton")),
            "trajectory viewer stylesheet includes peak settings apply button styling");
    require(!styleSheet.contains(QStringLiteral("trajectorySidebarStatusLabel")),
            "trajectory viewer stylesheet no longer targets a sidebar map status label");
    require(!styleSheet.contains(QStringLiteral("QProgressBar")),
            "trajectory viewer stylesheet no longer styles a sidebar map loading progress bar");
    require(styleSheet.contains(QStringLiteral("QLabel#trajectoryControlLabel")),
            "trajectory viewer stylesheet includes route control label styling");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryHeatLegendCard")),
            "trajectory viewer stylesheet includes floating heat legend styling");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryMapToolsCard")),
            "trajectory viewer stylesheet includes floating map tools styling");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryPointDetailCard")),
            "trajectory viewer stylesheet includes floating point detail styling");
    require(styleSheet.contains(QStringLiteral("QLabel#trajectoryPointDetailLabel")),
            "trajectory viewer stylesheet includes point detail label styling");
    require(styleSheet.contains(QStringLiteral("QToolButton#trajectoryPointDetailCloseButton")),
            "trajectory viewer stylesheet includes point detail close button styling");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryFilterCard")),
            "trajectory viewer stylesheet includes sidebar filter card styling");
    require(styleSheet.contains(QStringLiteral("QToolButton#trajectoryPointDetailActionButton")),
            "trajectory viewer stylesheet includes point detail filter action styling");
    const QString titleHoverColor = styleSheet.contains(VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover, true))
        ? VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover, true)
        : VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover, false);
    require(styleSheet.contains(VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover, true)) ||
                styleSheet.contains(VaporView::appThemeColorName(VaporView::AppThemeColor::TitleBarHover, false)),
            "trajectory viewer icon hover background uses the same neutral title hover color as the main window");
    for (const QString& iconHoverRule : {
             QStringLiteral("QToolButton#trajectoryPointDetailCloseButton:hover, QDialog#trajectoryViewerDialog QToolButton#trajectoryPointDetailCloseButton:focus, QDialog#trajectoryViewerDialog QToolButton#trajectoryPointDetailActionButton:hover, QDialog#trajectoryViewerDialog QToolButton#trajectoryPointDetailActionButton:focus { background-color: "),
             QStringLiteral("QToolButton#trajectoryHeatPaletteButton:hover, QDialog#trajectoryViewerDialog QToolButton#trajectoryHeatPaletteButton:focus { background-color: "),
             QStringLiteral("QToolButton#titleBarButton:hover, QDialog#trajectoryViewerDialog QToolButton#titleBarButton:focus { background-color: ")})
    {
        require(styleSheet.contains(iconHoverRule + titleHoverColor),
                "trajectory viewer icon focus background uses the neutral title hover color");
        require(!styleSheet.contains(iconHoverRule + VaporView::appThemeColorName(VaporView::AppThemeColor::PrimarySubtle, true)) &&
                    !styleSheet.contains(iconHoverRule + VaporView::appThemeColorName(VaporView::AppThemeColor::PrimarySubtle, false)),
                "trajectory viewer icon focus background does not use the primary accent color");
    }
    require(styleSheet.contains(QStringLiteral("QToolButton#titleBarButton { background-color: transparent; border: none")),
            "trajectory viewer title bar icon buttons match the main window borderless background");
    require(styleSheet.contains(QStringLiteral("QToolButton#titleBarButton:hover, QDialog#trajectoryViewerDialog QToolButton#titleBarButton:focus { background-color: ")
                + titleHoverColor + QStringLiteral("; border: none; }")),
            "trajectory viewer title bar icon hover state keeps the main window borderless style");
    require(styleSheet.contains(QStringLiteral("QPushButton#trajectoryVisibilityToggle")),
            "trajectory viewer stylesheet includes visibility toggle styling");
    require(styleSheet.contains(QStringLiteral("QToolButton#trajectoryHeatPaletteButton")),
            "trajectory viewer stylesheet renders the heat palette as an arrow-only selector");
    require(styleSheet.contains(QStringLiteral("QToolButton#trajectoryHeatPaletteButton::menu-indicator")),
            "trajectory viewer stylesheet hides the native menu indicator");
    require(!styleSheet.contains(QStringLiteral("QMenu#trajectoryHeatPaletteMenu")),
            "trajectory viewer stylesheet leaves heat palette popup chrome to SingleLevelPopupMenu");
    require(styleSheet.contains(QStringLiteral("QToolButton#trajectoryHeatMetricButton")),
            "trajectory viewer stylesheet renders the heat metric as a menu button");
    require(!styleSheet.contains(QStringLiteral("QMenu#trajectoryHeatMetricMenu")),
            "trajectory viewer stylesheet leaves heat metric popup chrome to SingleLevelPopupMenu");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryViewerMapPanel")),
            "trajectory viewer stylesheet includes rounded map panel styling");
    mapSourceCombo->setCurrentIndex(0);
    processEventsFor(100);

    RtkTrackStats stats;
    stats.scanned_rows = 3;
    stats.accepted_points = 2;
    stats.rejected_invalid_nav = 1;
    RtkTrackPoint firstPoint;
    firstPoint.latitude = 30.13698120;
    firstPoint.longitude = 120.06938175;
    firstPoint.height_m = 9.606;
    firstPoint.cumulative_distance_m = 0.0;
    firstPoint.speed_mps = 3.2;
    firstPoint.timestamp_us = 1782446035573000ULL;
    firstPoint.peak_value = 0.380270f;
    firstPoint.csv_row = 0;
    firstPoint.waveform_frame_index = 0;
    firstPoint.waveform_delta_us = 13929;
    firstPoint.has_height = true;
    firstPoint.has_speed = true;
    firstPoint.has_peak_value = true;
    firstPoint.has_waveform_match = true;
    RtkTrackPoint secondPoint = firstPoint;
    secondPoint.latitude = 30.1376200;
    secondPoint.longitude = 120.0699071;
    secondPoint.height_m = 11.190;
    secondPoint.cumulative_distance_m = 448.82;
    secondPoint.speed_mps = 4.1;
    secondPoint.csv_row = 1;
    secondPoint.peak_value = 0.420000f;
    dialog.setTrackStats(stats);
    dialog.setTrackPoints({firstPoint, secondPoint});
    processEventsFor(200);
    const QString footerStatus = map->property("_vvFooterStatusText").toString();
    require(footerStatus.contains(QStringLiteral("底图")) ||
                footerStatus.contains(QStringLiteral("Base map")) ||
                footerStatus.contains(QStringLiteral("tiles")),
            "map loading status is shown in the map footer data bar");
    require(map->property("_vvFooterProgressFormat").toString().contains(QStringLiteral("/")),
            "map loading progress is exposed by the map footer data bar");
    auto visibleFilterRows = [filterList]() {
        QList<QLabel*> rows;
        for (QLabel *label : filterList->findChildren<QLabel *>(QStringLiteral("trajectoryFilterRowLabel")))
        {
            if (label && !label->isHidden())
            {
                rows.push_back(label);
            }
        }
        return rows;
    };

    require(heatLegendCard->isVisible(), "heat legend card is visible when peak samples exist");
    require(heatGradientBar->isVisible(), "heat gradient bar is visible when peak samples exist");
    require(!pointDetailCard->isVisible(), "point detail card remains hidden after data load until map point click");
    require(filterEmptyLabel->isVisible(), "filter card starts with an empty state");
    require(filterCurrentButton->isEnabled(), "point filter action is enabled when track data exists");
    filterCurrentButton->click();
    const QString pointFilterStatus = map->property("_vvFooterStatusText").toString();
    processEventsFor(100);
    const auto pointFilterRows = visibleFilterRows();
    require(pointFilterRows.size() == 1, "filter-current action adds one row to the sidebar filter list");
    require(pointFilterRows.first()->text().contains(QStringLiteral("#1")),
            "filter-current row records the selected point number");
    require(pointFilterRows.first()->text().contains(QStringLiteral("过滤点")),
            "filter-current row describes an excluded point");
    require(pointFilterStatus.contains(QStringLiteral("已过滤 1 个点"))
                && pointFilterStatus.contains(QStringLiteral("剩余 1 个点")),
            "filter-current action excludes the point instead of keeping only it");
    require(pointFilterRows.first()->text().contains(QStringLiteral("remove:0")),
            "filter-current row exposes a row-scoped remove link");
    require(!pointFilterRows.first()->toolTip().trimmed().isEmpty(),
            "filter row exposes hover tooltip text for removal");
    require(!filterEmptyLabel->isVisible(), "filter empty text hides after adding a filter");
    require(QMetaObject::invokeMethod(pointFilterRows.first(),
                "linkActivated",
                Qt::DirectConnection,
                Q_ARG(QString, QStringLiteral("remove:0"))),
            "filter row remove link activates");
    processEventsFor(100);
    require(visibleFilterRows().isEmpty(), "filter remove link hides the canceled filter row");
    require(filterEmptyLabel->isVisible(), "filter empty text returns after removing all filters");
    filterCurrentButton->click();
    processEventsFor(100);
    filterStartButton->click();
    processEventsFor(100);
    const auto rangeStartRows = visibleFilterRows();
    require(rangeStartRows.size() == 2, "filter-start action adds a pending range row");
    require(std::any_of(rangeStartRows.cbegin(), rangeStartRows.cend(), [](const QLabel *label) {
                return label && label->text().contains(QStringLiteral("过滤区间")) &&
                       label->text().contains(QStringLiteral("#1")) &&
                       label->text().contains(QStringLiteral("--"));
            }),
            "filter-start row leaves the filter end empty until another point is clicked");
    require(std::all_of(rangeStartRows.cbegin(), rangeStartRows.cend(), [](const QLabel *label) {
                return label && label->text().contains(QStringLiteral("remove:"));
            }),
            "each visible filter row exposes its own remove link");
    filterEndButton->click();
    processEventsFor(100);
    const auto rangeEndRows = visibleFilterRows();
    require(std::any_of(rangeEndRows.cbegin(), rangeEndRows.cend(), [](const QLabel *label) {
                return label && label->text().contains(QStringLiteral("终点 #1")) && !label->text().contains(QStringLiteral("--"));
            }),
            "filter-end action fills the pending range end");
    pointDetailCard->show();
    processEventsFor(50);
    pointDetailCloseButton->click();
    processEventsFor(50);
    require(!pointDetailCard->isVisible(), "point detail close button hides the floating detail card");
    require(heatLegendCard->geometry().left() < mapToolsCard->geometry().left(),
            "floating heat legend stays to the left of map tools card");
    require(std::abs(heatLegendCard->geometry().top() - mapToolsCard->geometry().top()) <= 2,
            "floating heat legend and map tools card share the same top row");

    require(summaryLabel->textFormat() == Qt::RichText, "trajectory summary uses rich text");
    require(detailLabel->textFormat() == Qt::RichText, "trajectory detail uses rich text");
    require(summaryLabel->text().contains(QStringLiteral("<table")),
            "trajectory summary fields are formatted as a table");
    require(detailLabel->text().contains(QStringLiteral("<table")),
            "trajectory detail fields are formatted as a table");
    require(!detailLabel->text().contains(QStringLiteral(" | ")),
            "trajectory detail no longer uses a dense pipe-delimited sentence");

    auto *filterScroll = dialog.findChild<QScrollArea *>(QStringLiteral("trajectoryFilterScroll"));
    require(filterScroll, "filter list has its own scroll area");
    filterCurrentButton->click();
    processEventsFor(50);
    const int threeRowHeight = filterCard->height();
    filterCurrentButton->click();
    filterCurrentButton->click();
    processEventsFor(100);
    require(filterCard->height() == threeRowHeight, "filter card stops growing after three rows");
    require(filterScroll->verticalScrollBar()->maximum() > 0, "additional filters scroll inside their card");
    filterScroll->verticalScrollBar()->setValue(filterScroll->verticalScrollBar()->maximum());
    const auto lastRows = visibleFilterRows();
    require(filterScroll->viewport()->rect().contains(
                lastRows.last()->mapTo(filterScroll->viewport(), lastRows.last()->rect().bottomLeft())),
            "last filter and its remove action remain reachable by scrolling");

    dialog.close();
    processEventsFor(100);
}

void testTrajectoryViewerExternalSidebarScrollBar()
{
    const QFont originalFont = qApp->font();
    const QPalette originalPalette = qApp->palette();
    const bool originalDark = qApp->property(VaporView::kAppDarkThemeProperty).toBool();
    for (bool dark : {false, true})
    {
        qApp->setProperty(VaporView::kAppDarkThemeProperty, dark);
        qApp->setPalette(VaporView::appThemePalette(dark));
        for (double fontScale : {1.0, 1.3})
        {
            QFont font = originalFont;
            font.setPointSizeF(originalFont.pointSizeF() * fontScale);
            qApp->setFont(font);
            TrajectoryViewerDialog dialog;
            dialog.resize(1080, 660);
            dialog.show();
            RtkTrackPoint point;
            point.latitude = 30.23094;
            point.longitude = 120.13970;
            point.navigation_source = QStringLiteral("PPK");
            point.gnss_fix = QStringLiteral("RTK_FIXED");
            dialog.setTrackPoints({point});
            auto *sidebar = dialog.findChild<QScrollArea *>(QStringLiteral("trajectoryViewerSidebar"));
            auto *card = dialog.findChild<QFrame *>(QStringLiteral("trajectoryViewerSidebarCard"));
            auto *bar = dialog.findChild<QScrollBar *>(QStringLiteral("trajectorySidebarScrollBar"));
            auto *mapPanel = dialog.findChild<QFrame *>(QStringLiteral("trajectoryViewerMapPanel"));
            require(sidebar && card && bar && mapPanel, "external sidebar scrollbar and its layout exist");
            auto *internalBar = sidebar->verticalScrollBar();
            for (bool english : {false, true})
            {
                dialog.setEnglish(english);
                processEventsFor(100);
                require(bar->isVisible() && !card->isAncestorOf(bar) && !internalBar->isVisible(),
                        "overflow scrollbar sits outside the card and the native scrollbar stays hidden");
                const QRect cardRect(card->mapTo(&dialog, QPoint(0, 0)), card->size());
                const QRect barRect(bar->mapTo(&dialog, QPoint(0, 0)), bar->size());
                const QRect mapRect(mapPanel->mapTo(&dialog, QPoint(0, 0)), mapPanel->size());
                require(cardRect.right() < barRect.left() && barRect.right() < mapRect.left(),
                        "scrollbar occupies the gutter between the sidebar card and map");
                require(bar->maximum() == internalBar->maximum() && bar->pageStep() == internalBar->pageStep(),
                        "external scrollbar range and thumb size follow the viewport");
                bar->setValue(bar->maximum());
                processEventsFor(30);
                require(internalBar->value() == bar->value(), "external scrollbar scrolls the card body");
                for (auto *button : sidebar->findChildren<QAbstractButton *>())
                {
                    if (!button->isVisible())
                        continue;
                    const QRect buttonRect(button->mapTo(sidebar->viewport(), QPoint(0, 0)), button->size());
                    require(buttonRect.left() >= 0 && buttonRect.right() < sidebar->viewport()->width(),
                            "all sidebar buttons retain their complete horizontal edges");
                }
                internalBar->setValue(0);
                require(bar->value() == 0, "native scrolling keeps the external thumb in sync");
                const QPoint wheelPosition(20, 20);
                QWheelEvent wheel(QPointF(wheelPosition), QPointF(sidebar->viewport()->mapToGlobal(wheelPosition)),
                    QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(sidebar->viewport(), &wheel);
                require(internalBar->value() > 0 && bar->value() == internalBar->value(),
                        "mouse wheel still scrolls the card with its native scrollbar hidden");
            }
            const int mapLeft = mapPanel->mapTo(&dialog, QPoint(0, 0)).x();
            dialog.resize(1080, 1400);
            processEventsFor(100);
            require(internalBar->maximum() == 0 && !bar->isVisible(),
                    "external scrollbar hides when the viewport fits all controls");
            require(mapPanel->mapTo(&dialog, QPoint(0, 0)).x() == mapLeft,
                    "reserved scrollbar gutter prevents the map from shifting when overflow ends");
            dialog.close();
        }
    }
    qApp->setFont(originalFont);
    qApp->setProperty(VaporView::kAppDarkThemeProperty, originalDark);
    qApp->setPalette(originalPalette);
}

void testTrajectoryViewerPointNavigationProvenance()
{
    TrajectoryViewerDialog dialog;
    dialog.resize(1080, 680);
    dialog.show();
    processEventsFor(100);
    auto *details = dialog.findChild<QLabel *>(QStringLiteral("trajectoryPointDetailLabel"));
    require(details != nullptr, "trajectory point details exist for navigation provenance");
    auto *summary = dialog.findChild<QLabel *>(QStringLiteral("trajectorySidebarSummaryLabel"));
    auto *detailCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryPointDetailCard"));
    require(summary && detailCard, "sidebar summary and floating point details exist");

    RtkTrackPoint point;
    point.latitude = 30.23094;
    point.longitude = 120.13970;
    point.timestamp_us = 1'000'000;
    point.csv_row = 0;
    struct Example
    {
        QString source;
        QString fix;
        QString chineseSource;
        QString englishSource;
        QString chineseStatus;
        QString englishStatus;
    };
    const QVector<Example> examples = {
        {QStringLiteral("EPSILON_REALTIME"), QStringLiteral("3D"),
         QStringLiteral("原始接收机记录"), QStringLiteral("Original receiver record"),
         QStringLiteral("单点定位"), QStringLiteral("Single")},
        {QStringLiteral("EPSILON_REALTIME"), QStringLiteral("RTK_FIXED"),
         QStringLiteral("原始记录 · 实时 RTK"), QStringLiteral("Original · real-time RTK"),
         QStringLiteral("固定解（FIX）"), QStringLiteral("Fixed (FIX)")},
        {QStringLiteral("PPK"), QStringLiteral("RTK_FIXED"),
         QStringLiteral("PPK 后处理"), QStringLiteral("PPK post-processing"),
         QStringLiteral("固定解（FIX）"), QStringLiteral("Fixed (FIX)")},
        {QStringLiteral("PPK"), QStringLiteral("RTK_FLOAT"),
         QStringLiteral("PPK 后处理"), QStringLiteral("PPK post-processing"),
         QStringLiteral("浮点解（FLOAT）"), QStringLiteral("Float (FLOAT)")},
        {QStringLiteral("EPSILON_REALTIME"), QStringLiteral("RTK_FLOAT"),
         QStringLiteral("原始记录 · 实时 RTK"), QStringLiteral("Original · real-time RTK"),
         QStringLiteral("浮点解（FLOAT）"), QStringLiteral("Float (FLOAT)")},
        {QStringLiteral("EPSILON_REALTIME"), QString(),
         QStringLiteral("原始接收机记录"), QStringLiteral("Original receiver record"),
         QStringLiteral("未知"), QStringLiteral("Unknown")},
        {QStringLiteral("OTHER"), QStringLiteral("<CUSTOM>"),
         QStringLiteral("未知（OTHER）"), QStringLiteral("Unknown (OTHER)"),
         QStringLiteral("未知（&lt;CUSTOM&gt;）"), QStringLiteral("Unknown (&lt;CUSTOM&gt;)")}
    };
    const bool originalDark = qApp->property(VaporView::kAppDarkThemeProperty).toBool();
    const QPalette originalPalette = qApp->palette();
    for (bool dark : {false, true})
    {
        qApp->setProperty(VaporView::kAppDarkThemeProperty, dark);
        qApp->setPalette(VaporView::appThemePalette(dark));
        for (bool english : {false, true})
        {
            dialog.setEnglish(english);
            require(dialog.windowTitle() == (english ? QStringLiteral("Positioning Trajectory Viewer") : QStringLiteral("定位轨迹查看")),
                    "trajectory title covers raw GNSS, RTK and PPK in both languages");
            for (const auto &example : examples)
            {
                point.navigation_source = example.source;
                point.gnss_fix = example.fix;
                dialog.setTrackPoints({point});
                const QString sourceLabel = english ? QStringLiteral("Trajectory source") : QStringLiteral("轨迹来源");
                require(summary->isVisible() && !detailCard->isVisible() &&
                            summary->text().contains(sourceLabel) &&
                            summary->text().contains(english ? example.englishSource : example.chineseSource),
                        "sidebar shows the trajectory source before selecting a map point");
                require(summary->text().indexOf(sourceLabel) <
                            summary->text().indexOf(english ? QStringLiteral("Points") : QStringLiteral("点数")),
                        "trajectory source is the first sidebar summary field");
                const QString text = details->text();
                require(text.contains(english ? QStringLiteral("Trajectory source") : QStringLiteral("轨迹来源")) &&
                            text.contains(english ? example.englishSource : example.chineseSource),
                        "point details show actual coordinate provenance after changing tracks");
                require(text.contains(english ? QStringLiteral("Solution status") : QStringLiteral("解状态")) &&
                            text.contains(english ? example.englishStatus : example.chineseStatus),
                        "solution status stays separate from real-time or post-processed provenance");
                if (example.source == QStringLiteral("PPK"))
                    require(!text.contains(QStringLiteral("实时 RTK")) && !text.contains(QStringLiteral("real-time RTK")),
                            "PPK FIX/FLOAT must not be labelled real-time RTK");
            }
            point.navigation_source = QStringLiteral("PPK");
            point.gnss_fix = QStringLiteral("RTK_FIXED");
            RtkTrackPoint secondPoint = point;
            secondPoint.gnss_fix = QStringLiteral("RTK_FLOAT");
            dialog.setTrackPoints({point, secondPoint});
            require(summary->text().contains(english ? QStringLiteral("PPK post-processing") : QStringLiteral("PPK 后处理")),
                    "FIX and FLOAT share the same PPK trajectory source");
            point.navigation_source = QStringLiteral("ORIGINAL");
            secondPoint.navigation_source = QStringLiteral("EPSILON_REALTIME");
            secondPoint.gnss_fix = QStringLiteral("3D");
            dialog.setTrackPoints({point, secondPoint});
            require(summary->text().contains(english ? QStringLiteral("Original (includes real-time RTK)") : QStringLiteral("原始记录（含实时 RTK）")),
                    "changing receiver fix quality does not turn Original into mixed navigation sources");
            point.navigation_source = QStringLiteral("PPK");
            dialog.setTrackPoints({point, secondPoint});
            require(summary->text().contains(english ? QStringLiteral("Mixed sources") : QStringLiteral("混合来源")),
                    "partially corrected tracks do not claim one uniform coordinate source");
            dialog.setTrackPoints({});
            require(!summary->text().contains(english ? QStringLiteral("Trajectory source") : QStringLiteral("轨迹来源")),
                    "clearing the track also clears the previous sidebar source");
        }
    }
    qApp->setProperty(VaporView::kAppDarkThemeProperty, originalDark);
    qApp->setPalette(originalPalette);
    dialog.close();
    processEventsFor(50);
}

void testTrajectoryViewerBridgesFilteredRouteRanges()
{
    TrajectoryViewerDialog dialog;
    dialog.resize(1080, 680);
    dialog.show();
    processEventsFor(250);

    auto *map = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerMap"));
    auto *filterList = dialog.findChild<QWidget *>(QStringLiteral("trajectoryFilterList"));
    QToolButton *filterStartButton = nullptr;
    for (QToolButton *button : dialog.findChildren<QToolButton *>(QStringLiteral("trajectoryPointDetailActionButton")))
    {
        if (button && button->property("pointActionRole").toString() == QStringLiteral("filter-start"))
        {
            filterStartButton = button;
            break;
        }
    }
    require(map != nullptr, "trajectory map exists for filtered route bridge test");
    require(filterList != nullptr, "trajectory filter list exists for filtered route bridge test");
    require(filterStartButton != nullptr, "filter-start button exists for filtered route bridge test");

    QVector<RtkTrackPoint> points;
    points.reserve(5);
    for (int index = 0; index < 5; ++index)
    {
        RtkTrackPoint point;
        point.latitude = 30.13698120;
        point.longitude = 120.06100000 + index * 0.002;
        point.height_m = 10.0 + index;
        point.cumulative_distance_m = index * 100.0;
        point.csv_row = index;
        point.has_height = true;
        points.push_back(point);
    }
    dialog.setTrackPoints(points);
    processEventsFor(300);
    require(map->property("_vvFilterBridgeSegmentCount").toInt() == 0,
            "trajectory route starts without filtered bridge segments");

    clickWidgetAt(map, trajectoryPointScreenPosition(map, points, 1));
    filterStartButton->click();
    processEventsFor(120);
    clickWidgetAt(map, trajectoryPointScreenPosition(map, points, 3));
    processEventsFor(160);

    require(map->property("_vvFilterBridgeSegmentCount").toInt() == 1,
            "trajectory route bridges the visible points around a filtered range");
    bool hasCompletedRange = false;
    for (QLabel *label : filterList->findChildren<QLabel *>(QStringLiteral("trajectoryFilterRowLabel")))
    {
        if (label && !label->isHidden() &&
            label->text().contains(QStringLiteral("起点 #2")) &&
            label->text().contains(QStringLiteral("终点 #4")))
        {
            hasCompletedRange = true;
            break;
        }
    }
    require(hasCompletedRange, "filtered route bridge test completes the intended point range");

    dialog.close();
    processEventsFor(100);
}

void testTrajectoryViewerRouteLodLimitsDenseTracks()
{
    TrajectoryViewerDialog dialog;
    dialog.resize(1080, 680);
    dialog.show();
    processEventsFor(250);

    auto *map = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerMap"));
    require(map != nullptr, "trajectory map exists for dense route LOD test");

    QVector<RtkTrackPoint> points;
    constexpr int kDensePointCount = 12000;
    points.reserve(kDensePointCount);
    for (int index = 0; index < kDensePointCount; ++index)
    {
        RtkTrackPoint point;
        const double t = static_cast<double>(index) / static_cast<double>(kDensePointCount - 1);
        point.latitude = 30.13698120 + std::sin(t * 8.0 * 3.14159265358979323846) * 0.0012;
        point.longitude = 120.06100000 + t * 0.012;
        point.height_m = 10.0;
        point.cumulative_distance_m = index;
        point.csv_row = index;
        point.has_height = true;
        points.push_back(point);
    }

    dialog.setTrackPoints(points);
    processEventsFor(300);
    const QPixmap renderedMap = map->grab();
    require(!renderedMap.isNull(), "dense route LOD test renders the map");
    const int routeSegmentCount = map->property("_vvRouteSegmentCount").toInt();
    require(routeSegmentCount > 0, "dense route LOD draws visible route segments");
    require(routeSegmentCount < kDensePointCount / 3,
            "dense route LOD limits the number of route segments given to Qt");

    dialog.close();
    processEventsFor(100);
}

void testSessionWorkspaceNavigation()
{
    using Page = SessionViewerWindow::Page;
    QTemporaryDir session;
    require(session.isValid(), "temporary workspace session");
    writeTrajectorySessionWithRawTcpPeaks(session.path());
    SessionViewerWindow viewer;
    viewer.setUiTestMode(true);
    viewer.setEnglish(true);
    viewer.resize(1280, 800);
    viewer.show();
    processEventsFor(100);
    auto *stack = viewer.findChild<QStackedWidget *>("sessionViewerPageStack");
    auto *splitter = viewer.findChild<QSplitter *>("sessionViewerNavigationSplitter");
    auto *toggle = viewer.findChild<QLabel *>("customTitleLogo");
    require(!viewer.findChild<QWidget *>("sessionViewerSidebarToggle"), "viewer has no redundant hamburger button");
    require(stack && stack->count() == 4 && viewer.currentPage() == Page::Data,
            "independent viewer starts with four pages and Data selected");
    require(viewer.isWindow() && toggle && splitter, "viewer keeps one independent window and sidebar toggle");
    auto *titleBar = viewer.findChild<QWidget *>("customTitleBar");
    auto *controls = viewer.findChild<QWidget *>("sessionViewerSessionControls");
    require(titleBar && controls && titleBar->isAncestorOf(controls), "session actions live in the shared title bar");
    require(controls->findChildren<QPushButton *>().size() == 3,
            "title bar contains only open, reload, and clear actions");
    for (int i = 0; i < 4; ++i)
    {
        QPushButton *nav = nullptr;
        for (auto *button : viewer.findChildren<QPushButton *>("appSidebarButton"))
            if (button->property("sessionViewerPage").toInt() == i)
                nav = button;
        require(nav, "all four sidebar entries exist");
        nav->click();
        require(stack->currentIndex() == i && nav->isChecked(), "sidebar selects its page");
        require(controls->isVisible(), "session title controls remain visible on every page");
    }
    viewer.setCurrentPage(Page::Data);
    auto *pathEdit = controls->findChild<QLineEdit *>("sessionViewerSessionPath");
    auto *reload = controls->findChild<QPushButton *>("sessionViewerReloadButton");
    require(pathEdit && !pathEdit->isReadOnly() && reload, "session path can be edited");
    require(controls->findChild<QFrame *>("titleBarSeparator"), "session actions end with title bar separator");
    const QString editedPath = session.filePath(QStringLiteral("session.json"));
    pathEdit->setText(editedPath);
    QMetaObject::invokeMethod(pathEdit, "editingFinished");
    QMetaObject::invokeMethod(pathEdit, "returnPressed");
    processEventsFor(100);
    require(pathEdit->text() == editedPath, "editing or Enter does not resolve or load the pending path");
    reload->click();
    require(processEventsUntil(5000, [&] { return pathEdit->text() == session.path() && reload->isEnabled(); }),
            "explicit reload loads edited session.json and normalizes its path");
    pathEdit->setText(session.filePath(QStringLiteral("does-not-exist")));
    reload->click();
    require(pathEdit->text().endsWith(QStringLiteral("does-not-exist")), "invalid edited path remains available to correct");
    pathEdit->setText(session.path());
    require(viewer.openSessionPath(session.path()), "workspace loads shared session");
    viewer.setCurrentPage(Page::Trajectory);
    auto *trajectory = viewer.findChild<TrajectoryViewerDialog *>();
    require(trajectory && !trajectory->isWindow() && trajectory->isVisible(), "trajectory is embedded");
    require(!trajectory->findChild<QWidget *>("customTitleBar"), "trajectory has no nested title bar");
    auto *peakStart = trajectory->findChild<QSpinBox *>("trajectoryPeakSearchStartSpin");
    require(peakStart, "embedded trajectory exposes peak settings");
    peakStart->setValue(321);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(trajectory, &escape);
    require(trajectory->isVisible(), "Escape does not dismiss an embedded trajectory");
    viewer.setCurrentPage(Page::RawData);
    RawDataParserWindow *raw = nullptr;
    for (auto *child : viewer.findChildren<QMainWindow *>())
        if (auto *parser = dynamic_cast<RawDataParserWindow *>(child))
            raw = parser;
    require(raw && !raw->isWindow() && raw->isVisible(), "parser is embedded in workspace");
    auto *table = raw->findChild<QTableView *>();
    require(processEventsUntil(5000, [&] { return table && table->model()->rowCount() > 0; }), "embedded parser indexes shared session");
    auto *model = table->model();
    const int rows = model->rowCount();
    auto *filter = raw->findChild<QLineEdit *>();
    require(filter, "parser filter exists");
    filter->setText(QStringLiteral("0x50"));
    viewer.setCurrentPage(Page::Data);
    viewer.setCurrentPage(Page::RawData);
    require(table->model() == model && model->rowCount() == rows && filter->text() == QStringLiteral("0x50"),
            "switching pages preserves parser index and filter without rescanning");
    viewer.setCurrentPage(Page::Trajectory);
    require(viewer.findChild<TrajectoryViewerDialog *>() == trajectory, "trajectory page is reused");
    require(peakStart->value() == 321, "page switching preserves unsubmitted trajectory settings");
    auto *linkedFrame = viewer.findChild<QSpinBox *>("sessionViewerFrameNumberSpin");
    require(linkedFrame && linkedFrame->maximum() >= 3, "trajectory fixture has linked waveform frames");
    // The fixture's later GPS points exceed the jump threshold; only point 0 is retained.
    for (int frame : {3, 2})
    {
        linkedFrame->setValue(frame);
        trajectory->trackPointActivated(0);
        require(viewer.currentPage() == Page::Trajectory && trajectory->isVisible(),
                "trajectory point navigation preserves the active map page");
        require(linkedFrame->value() == 1, "trajectory selection still locates the matching waveform");
    }
    trajectory->trackPointActivated(-1);
    require(viewer.currentPage() == Page::Trajectory, "invalid point selection does not navigate");
    viewer.setCurrentPage(Page::RawData);
    viewer.close();
    viewer.show();
    processEventsFor(50);
    require(viewer.currentPage() == Page::RawData && raw->isVisible() && model->rowCount() == rows,
            "closing and reopening preserves current page and loaded records");
    QEvent enter(QEvent::Enter);
    QCoreApplication::sendEvent(toggle, &enter);
    require(toggle->property("_vv_logo_state").toString() == "close-sidebar", "logo hover offers sidebar collapse");
    require(toggle->focusPolicy() == Qt::TabFocus, "sidebar logo is keyboard accessible");
    clickWidgetAt(toggle, toggle->rect().center(), 50);
    require(splitter->sizes().value(0) == 0 && toggle->toolTip() == "Show left sidebar",
            "logo hides sidebar and updates accessible action");
    require(toggle->property("_vv_logo_state").toString() == "open-sidebar", "hidden sidebar hover offers expansion");
    clickWidgetAt(toggle, toggle->rect().center(), 50);
    require(splitter->sizes().value(0) == 62, "logo restores single-icon width rather than expanded mode");
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(toggle, &space);
    require(splitter->sizes().value(0) == 0, "space collapses sidebar");
    QKeyEvent enterKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(toggle, &enterKey);
    require(splitter->sizes().value(0) == 62, "enter restores sidebar");
    auto *handle = splitter->handle(1);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(handle->mapToGlobal(QPoint(1, 1))),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(handle, &press);
    splitter->setSizes({190, splitter->width() - splitter->handleWidth() - 190});
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(handle->mapToGlobal(QPoint(1, 1))),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(handle, &release);
    processEventsFor(50);
    const int expandedWidth = splitter->sizes().value(0);
    require(expandedWidth >= 190, "splitter still supports expanded sidebar");
    clickWidgetAt(toggle, toggle->rect().center(), 50);
    clickWidgetAt(toggle, toggle->rect().center(), 50);
    require(splitter->sizes().value(0) == expandedWidth, "logo remembers manually expanded sidebar width");
    const bool maximized = viewer.isMaximized();
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(toggle->rect().center()),
                            QPointF(toggle->mapToGlobal(toggle->rect().center())),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(toggle, &doubleClick);
    require(viewer.isMaximized() == maximized && splitter->sizes().value(0) == 0,
            "logo double-click toggles sidebar without maximizing the window");
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(toggle, &leave);
    require(toggle->property("_vv_logo_state").toString() == "logo", "logo returns when pointer leaves");
    clickWidgetAt(toggle, toggle->rect().center(), 50);
    viewer.setCurrentPage(Page::Data);
    require(QMetaObject::invokeMethod(&viewer, "onClearViewClicked", Qt::DirectConnection), "clear shared session data");
    require(model->rowCount() == 0, "clearing shared data clears parser records");
    viewer.setCurrentPage(Page::RawData);
    require(!raw->isVisible(), "cleared parser shows empty state instead of stale content");
    viewer.close();
}

void testSessionViewerTitleBarWindowButtons()
{
    SessionViewerWindow viewer;
    viewer.resize(1280, 800);
    viewer.show();
    require(waitForWindowExposed(&viewer), "data viewer becomes exposed for title-bar button test");

    requireSessionViewerTitleBarWindowButtonsWork(viewer);
    viewer.close();
    processEventsFor(100);
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    VaporView::installButtonFocusStyle();
    app.setOrganizationName(QStringLiteral("VaporViewSessionViewerThemeTest"));
    app.setApplicationName(QStringLiteral("session_viewer_theme_test"));
    app.setProperty(VaporView::kAppDarkThemeProperty, false);
    app.setPalette(VaporView::appThemePalette(false));

    QString selectedGroup = QStringLiteral("all");
    const QStringList arguments = QCoreApplication::arguments();
    for (int index = 1; index < arguments.size(); ++index)
    {
        if (arguments.at(index) == QStringLiteral("--group") && index + 1 < arguments.size())
        {
            selectedGroup = arguments.at(++index);
        }
    }
    const QStringList validGroups = {
        QStringLiteral("all"),
        QStringLiteral("io"),
        QStringLiteral("issues"),
        QStringLiteral("window-state"),
        QStringLiteral("trajectory"),
        QStringLiteral("theme")
    };
    require(validGroups.contains(selectedGroup), "session viewer test group is recognized");
    const auto runsGroup = [&selectedGroup](const QString& group) {
        return selectedGroup == QStringLiteral("all") || selectedGroup == group;
    };

    if (runsGroup(QStringLiteral("io")))
    {
        testRawDataCheckedExport();
        testRawDataIssuesFilter();
        testRawDataIssuesMatchDetails();
        testRawDataScanProgressVisible();
        testRawDataParserOpenIsNonBlocking();
        testRawDataParserRejectsTruncatedFdilinkFrame();
        testRawDataParserUsesHardwareTemperatureSourceNames();
        testSessionViewerShowsRecoveredWaveformCatalogWarning();
        testWaveformIndexContinuesWhileGuiIsBusy();
        testCsvHighlightsStartAtTopWhenLoadingAndChangingFrames();
        testFrameSliderReleaseRestoresDetails();
        testSessionViewerTrajectoryActionLifetime();
    }
    if (selectedGroup == QStringLiteral("issues"))
    {
        testRawDataIssuesFilter();
        testRawDataIssuesMatchDetails();
    }
    if (runsGroup(QStringLiteral("window-state")))
    {
        testSessionViewerTitleBarWindowButtons();
        testSessionWorkspaceNavigation();
    }
    if (runsGroup(QStringLiteral("trajectory")))
    {
        testTrajectoryViewerInitialHeatLegendFromPendingPeaks();
        testTrajectoryViewerUsesSidebarLayout();
        testTrajectoryViewerExternalSidebarScrollBar();
        testTrajectoryViewerPointNavigationProvenance();
        testTrajectoryViewerBridgesFilteredRouteRanges();
        testTrajectoryViewerRouteLodLimitsDenseTracks();
    }

    if (runsGroup(QStringLiteral("theme")))
    {
        testRawDataExportMenu();
        app.setProperty(VaporView::kAppDarkThemeProperty, false);
        app.setPalette(VaporView::appThemePalette(false));
        {
            SessionViewerWindow viewer;
            viewer.resize(1280, 800);
            viewer.show();
            require(waitForWindowExposed(&viewer), "light data viewer becomes exposed for theme test");

            testCsvViewportUsesNeutralBackground(viewer);
            testCsvSelectionUsesThemeAccent(viewer, false);
            testWaveformEmptyPlotIsNotRed(viewer);

            viewer.close();
            processEventsFor(100);
        }

        app.setProperty(VaporView::kAppDarkThemeProperty, true);
        app.setPalette(VaporView::appThemePalette(true));
        {
            SessionViewerWindow viewer;
            viewer.resize(1280, 800);
            viewer.show();
            require(waitForWindowExposed(&viewer), "dark data viewer becomes exposed for theme test");

            testCsvSelectionUsesThemeAccent(viewer, true);

            viewer.close();
            processEventsFor(100);
        }
    }

    std::cout << "session_viewer_theme_test group "
              << selectedGroup.toStdString() << " passed\n";
    return 0;
}
