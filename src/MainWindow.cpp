#include "MainWindow.h"
#include "RtkConfigDialog.h"
#include "SessionViewerWindow.h"
#include "SkyDeviceConfigDialog.h"
#include "TcpWaveEncoding.h"
#include "TcpWavePanel.h"
#include "WindowSizing.h"
#include "data_collector.h"
#include "data_types.h"
#include "serial_probe_utils.h"
#include <QMenu>
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFont>
#include <QSaveFile>
#include <QTextStream>
#include <QStringConverter>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QGridLayout>
#include <QFrame>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QDir>
#include <QDirIterator>
#include <QScrollBar>
#include <QShortcut>
#include <QScreen>
#include <QSpacerItem>
#include <QStringList>
#include <QApplication>
#include <QGuiApplication>
#include <QLayout>
#include <QIntValidator>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>
#include <QSet>
#include <QSignalBlocker>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QThread>
#include <QToolButton>
#include <QVector>
#include <QWindow>
#include <QtEndian>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace
{
class MenuItemEventFilter : public QObject
{
public:
    MenuItemEventFilter(std::function<void()> hoverCallback,
                        std::function<void()> clickCallback,
                        QObject *parent)
        : QObject(parent)
        , hover_callback_(std::move(hoverCallback))
        , click_callback_(std::move(clickCallback))
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched);
        if ((event->type() == QEvent::Enter || event->type() == QEvent::MouseMove) && hover_callback_)
        {
            hover_callback_();
        }
        else if (event->type() == QEvent::MouseButtonRelease && click_callback_)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                click_callback_();
                return true;
            }
        }
        return false;
    }

private:
    std::function<void()> hover_callback_;
    std::function<void()> click_callback_;
};

constexpr const char *kBaseMinWidthProperty = "_vv_base_min_width";
constexpr const char *kBaseMinHeightProperty = "_vv_base_min_height";
constexpr const char *kBaseMaxWidthProperty = "_vv_base_max_width";
constexpr const char *kBaseMaxHeightProperty = "_vv_base_max_height";
constexpr const char *kBaseSpacingProperty = "_vv_base_spacing";
constexpr const char *kBaseMarginsLeftProperty = "_vv_base_margin_left";
constexpr const char *kBaseMarginsTopProperty = "_vv_base_margin_top";
constexpr const char *kBaseMarginsRightProperty = "_vv_base_margin_right";
constexpr const char *kBaseMarginsBottomProperty = "_vv_base_margin_bottom";
constexpr const char *kMainCardMinimumHeightProperty = "_vv_main_card_minimum_height";
constexpr int kMainPageInputHeight = 36;
constexpr int kMainPageButtonHeight = 36;
constexpr int kMainPageTitleBarHeight = kMainPageButtonHeight + 4;
constexpr int kConfigFormBottomPadding = 4;
constexpr int kConfigRowsHeight = kMainPageInputHeight * 4 + 4 * 3 + kConfigFormBottomPadding;
constexpr int kConfigCardBottomPadding = 4;
constexpr int kConfigCardMinHeight = kMainPageTitleBarHeight + 4 + kConfigRowsHeight + kConfigCardBottomPadding;
constexpr int kConfigRemoteCardMinHeight = kConfigCardMinHeight + (kMainPageInputHeight + 4) + 4;
constexpr int kTcpWaveCardMinHeight = 430;
constexpr int kMainCardResizeHandleHeight = 3;
constexpr int kEnvStatusIconSize = 18;
constexpr int kEnvironmentRateLabelMinWidth = 72;
constexpr int kPtbPressureValueMinWidth = 112;
constexpr int kHmpValueMinWidth = 92;
constexpr int kLidarDistanceValueMinWidth = 92;
constexpr int kLidarStrengthValueMinWidth = 56;
constexpr int kEpsilonSideTitleWidth = 24;
constexpr int kEpsilonTitleColumnWidth = 102;
constexpr int kEpsilonMotionTitleColumnWidth = 116;
constexpr int kEpsilonLeftValueColumnWidth = 240;
constexpr int kEpsilonPositionValueColumnWidth = 230;
constexpr int kEpsilonMotionValueColumnWidth = 300;
constexpr int kTelemetrySummaryRateCardWidth = 250;
constexpr int kTelemetrySummaryInfoCardWidth = 250;
constexpr int kTelemetrySummaryGapWidth = 2;
constexpr int kTelemetrySummaryRateLabelWidth = 128;
constexpr int kTelemetrySummaryRateValueWidth = 86;
constexpr int kTelemetrySummaryInfoLabelWidth = 118;
constexpr int kTelemetrySummaryInfoValueWidth = 86;
constexpr int kTelemetrySummaryTitleColumnWidth = kEpsilonSideTitleWidth;
constexpr int kPtbMinSampleRateHz = 1;
constexpr int kPtbMaxSampleRateHz = 70;

std::string epsilonGnssFixTextForCode(int fix_code)
{
    switch (fix_code)
    {
    case 0: return "NO_GPS";
    case 1: return "NO_FIX";
    case 2: return "2D";
    case 3: return "3D";
    case 4: return "DGPS";
    case 5: return "RTK_FLOAT";
    case 6: return "RTK_FIXED";
    case 7: return "STATIC";
    case 8: return "PPP";
    case 9: return "RTK_DUAL";
    default: return "UNKNOWN";
    }
}

class MainCardResizeHandle : public QWidget
{
public:
    MainCardResizeHandle(QWidget *targetCard, int minimumTargetHeight, QWidget *parent = nullptr)
        : QWidget(parent)
        , target_card_(targetCard)
        , minimum_target_height_(minimumTargetHeight)
        , drag_start_y_(0)
        , target_start_height_(0)
        , dragging_(false)
    {
        setObjectName(QStringLiteral("mainCardResizeHandle"));
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::SizeVerCursor);
        setFixedHeight(kMainCardResizeHandleHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setProperty("dragging", false);
        if (target_card_)
        {
            target_card_->setProperty(kMainCardMinimumHeightProperty, minimum_target_height_);
        }
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !target_card_)
        {
            QWidget::mousePressEvent(event);
            return;
        }

        dragging_ = true;
        drag_start_y_ = event->globalPosition().toPoint().y();
        target_start_height_ = target_card_->height();
        setProperty("dragging", true);
        refreshStyle();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!dragging_ || !target_card_)
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        const int deltaY = event->globalPosition().toPoint().y() - drag_start_y_;
        bool ok = false;
        const int propertyMinimum = target_card_->property(kMainCardMinimumHeightProperty).toInt(&ok);
        const int effectiveMinimum = ok ? std::max(minimum_target_height_, propertyMinimum) : minimum_target_height_;
        const int nextHeight = std::max(effectiveMinimum, target_start_height_ + deltaY);
        target_card_->setFixedHeight(nextHeight);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && dragging_)
        {
            dragging_ = false;
            setProperty("dragging", false);
            refreshStyle();
            event->accept();
            return;
        }

        QWidget::mouseReleaseEvent(event);
    }

private:
    void refreshStyle()
    {
        style()->unpolish(this);
        style()->polish(this);
        update();
    }

    QWidget *target_card_;
    int minimum_target_height_;
    int drag_start_y_;
    int target_start_height_;
    bool dragging_;
};

class WindowResizeHandle : public QWidget
{
public:
    explicit WindowResizeHandle(Qt::Edges edges, QWidget *parent)
        : QWidget(parent)
        , edges_(edges)
    {
        setFocusPolicy(Qt::NoFocus);
        setCursor(cursorForEdges(edges_));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QWidget *topLevelWindow = window();
        if (event->button() == Qt::LeftButton && topLevelWindow && topLevelWindow->windowHandle())
        {
            topLevelWindow->windowHandle()->startSystemResize(edges_);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    static QCursor cursorForEdges(Qt::Edges edges)
    {
        const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
        const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
        if (horizontal && vertical)
        {
            const bool topLeftBottomRight =
                (edges.testFlag(Qt::TopEdge) && edges.testFlag(Qt::LeftEdge)) ||
                (edges.testFlag(Qt::BottomEdge) && edges.testFlag(Qt::RightEdge));
            return QCursor(topLeftBottomRight ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        }
        if (horizontal)
        {
            return QCursor(Qt::SizeHorCursor);
        }
        return QCursor(Qt::SizeVerCursor);
    }

    Qt::Edges edges_;
};

QString skyDeviceDisplayName(VaporView::SkyDeviceId device)
{
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon: return QStringLiteral("EPSILON");
    case VaporView::SkyDeviceId::Ptb: return QStringLiteral("PTB210");
    case VaporView::SkyDeviceId::Hmp: return QStringLiteral("HMP3");
    case VaporView::SkyDeviceId::Lidar: return QStringLiteral("TFA1500-L");
    case VaporView::SkyDeviceId::WaveTcp: return QStringLiteral("Wave TCP");
    case VaporView::SkyDeviceId::All: return QStringLiteral("全部设备");
    }
    return QStringLiteral("Device");
}

QString formatBitRate(double bitsPerSecond)
{
    if (!std::isfinite(bitsPerSecond) || bitsPerSecond <= 0.0)
    {
        return QStringLiteral("0 bps");
    }
    if (bitsPerSecond < 1000.0)
    {
        return QStringLiteral("%1 bps").arg(bitsPerSecond, 0, 'f', 0);
    }
    return QStringLiteral("%1 kbps").arg(bitsPerSecond / 1000.0, 0, 'f', bitsPerSecond < 10000.0 ? 2 : 1);
}

QString remoteNoDataText(bool english)
{
    return english ? QStringLiteral("No data") : QStringLiteral("无数据");
}

QString remoteDisconnectedText(bool english)
{
    return english ? QStringLiteral("Not connected") : QStringLiteral("未连接");
}

QString remoteStaleText(bool english)
{
    return english ? QStringLiteral("Stale") : QStringLiteral("超时");
}

QString formatElapsedCompact(quint64 elapsedMs)
{
    const quint64 totalSeconds = elapsedMs / 1000ULL;
    const quint64 hours = totalSeconds / 3600ULL;
    const quint64 minutes = (totalSeconds / 60ULL) % 60ULL;
    const quint64 seconds = totalSeconds % 60ULL;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

constexpr int kDefaultEpsilonSampleRateHz = 100;
constexpr int kDefaultPtbSampleRateHz = 20;
constexpr int kDefaultHmpSampleRateHz = 20;
constexpr int kDefaultLidarSampleRateHz = 100;
constexpr int kFallbackMainWindowWidth = 1440;
constexpr int kFallbackMainWindowHeight = 860;
constexpr qreal kMainWindowDefaultScreenFraction = 0.5;
constexpr qreal kMainWindowMinimumScreenFraction = 0.25;
constexpr quint64 kImuPpsSyncWindowUs = 2ULL * 1000ULL * 1000ULL;
constexpr char kUnifiedRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kUnifiedRawFormatVersion = 2u;
constexpr quint32 kUnifiedRawRecordMarker = 0x44525756u;
constexpr quint16 kRawSourceEpsilon = 1u;
constexpr quint16 kRawSourcePtb = 2u;
constexpr quint16 kRawSourceHmp = 3u;
constexpr quint16 kRawSourceLidar = 4u;
constexpr quint16 kRawSourceTcpWave = 5u;
constexpr quint16 kRawRecordTypeGeneric = 1u;
constexpr quint32 kRawTcpWaveCombinedPayloadFlag = 0x00000001u;
const QColor kToolbarBlue(40, 105, 190);
const QColor kToolbarGreen(35, 150, 95);
const QColor kToolbarRed(205, 72, 72);
const QColor kToolbarAmber(220, 150, 35);
const QColor kToolbarDisabled(145, 150, 158);

int clampPtbSampleRate(int hz)
{
    return std::clamp(hz, kPtbMinSampleRateHz, kPtbMaxSampleRateHz);
}

QString findResourceFile(const QString& relativePath)
{
    const QString appDir = QApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(relativePath),
        QDir(appDir).filePath(QStringLiteral("../") + relativePath),
        QDir(appDir).filePath(QStringLiteral("../../") + relativePath)
    };

    for (const QString& path : candidates)
    {
        if (QFileInfo::exists(path))
        {
            return path;
        }
    }
    return QString();
}

QPixmap renderLucidePixmap(const QByteArray& svgData, const QColor& color)
{
    QByteArray tinted = svgData;
    tinted.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(tinted);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(2, 2, 28, 28));
    return pixmap;
}

QIcon createLucideIcon(const QString& iconName, const QColor& color)
{
    QFile file(findResourceFile(QStringLiteral("resources/lucide/%1.svg").arg(iconName)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QIcon();
    }

    const QByteArray svgData = file.readAll();
    QIcon icon;
    icon.addPixmap(renderLucidePixmap(svgData, color), QIcon::Normal);
    icon.addPixmap(renderLucidePixmap(svgData, kToolbarDisabled), QIcon::Disabled);
    return icon;
}

QString vaporViewLogoResourcePath(bool dark)
{
    return findResourceFile(dark
        ? QStringLiteral("resources/VaproViewLOGO/VaporViewLOGO_rgb217_119_87.svg")
        : QStringLiteral("resources/VaproViewLOGO/VaporViewLOGO_black.svg"));
}

QPixmap renderVaporViewLogo(bool dark, int size, qreal devicePixelRatio)
{
    const QString path = vaporViewLogoResourcePath(dark);
    if (path.isEmpty())
    {
        return QPixmap();
    }

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    const int logicalSize = std::max(1, size);
    const int physicalSize = std::max(1, qCeil(logicalSize * dpr));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(path);
    if (!renderer.isValid())
    {
        return QPixmap();
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, logicalSize, logicalSize));
    return pixmap;
}

QIcon createVaporViewLogoIcon(bool dark)
{
    const QString path = vaporViewLogoResourcePath(dark);
    return path.isEmpty() ? QIcon() : QIcon(path);
}

QIcon createRefreshIcon()
{
    return createLucideIcon(QStringLiteral("refresh-cw"), kToolbarBlue);
}

QIcon createConnectIcon()
{
    return createLucideIcon(QStringLiteral("plug"), kToolbarGreen);
}

QIcon createCancelIcon()
{
    return createLucideIcon(QStringLiteral("circle-x"), kToolbarRed);
}

QIcon createDisconnectIcon()
{
    return createLucideIcon(QStringLiteral("unplug"), kToolbarRed);
}

QIcon createPlayIcon()
{
    return createLucideIcon(QStringLiteral("play"), kToolbarGreen);
}

QIcon createPauseIcon()
{
    return createLucideIcon(QStringLiteral("pause"), kToolbarBlue);
}

QIcon createStopIcon()
{
    return createLucideIcon(QStringLiteral("square"), kToolbarRed);
}

QIcon createRtkSatelliteIcon()
{
    return createLucideIcon(QStringLiteral("satellite"), kToolbarBlue);
}

QIcon createClearLogIcon()
{
    return createLucideIcon(QStringLiteral("trash-2"), kToolbarBlue);
}

QIcon createWaveformViewerIcon()
{
    return createLucideIcon(QStringLiteral("audio-waveform"), kToolbarBlue);
}

QIcon createLanguageIcon()
{
    return createLucideIcon(QStringLiteral("languages"), kToolbarBlue);
}

QIcon createDarkThemeIcon()
{
    return createLucideIcon(QStringLiteral("moon"), kToolbarBlue);
}

QIcon createLightThemeIcon()
{
    return createLucideIcon(QStringLiteral("sun"), kToolbarAmber);
}

QIcon createTitleBarIcon(const QString& iconName, bool dark)
{
    return createLucideIcon(iconName, dark ? QColor("#d8dee9") : QColor("#111827"));
}

QString titleApplicationPanelStyleSheet(bool dark, int cornerRadius = 8)
{
    if (dark)
    {
        return QStringLiteral(R"(
QFrame#titleApplicationPanel,
QFrame#titleApplicationSubPanel {
    background-color: transparent;
    border: none;
}
QFrame#titleApplicationMainMenu,
QFrame#titleApplicationSubMenu {
    background-color: #121212;
    border: 1px solid #202020;
    border-radius: %1px;
}
QFrame#titleApplicationMenuItem {
    background-color: transparent;
    border: none;
    border-radius: 0px;
}
QFrame#titleApplicationMenuItem[selected="true"],
QFrame#titleApplicationMenuItem:hover {
    background-color: #202020;
}
QLabel#titleApplicationMenuText {
    color: #f3f6fb;
    background-color: transparent;
    border: none;
    padding: 0px;
}
QLabel#titleApplicationMenuShortcut,
QLabel#titleApplicationMenuArrow,
QLabel#titleApplicationMenuCheck {
    color: #d7dce2;
    background-color: transparent;
    border: none;
    padding: 0px;
}
QLabel#titleApplicationMenuCheck {
    color: #9aa0a6;
}
QLabel#titleApplicationMenuText:disabled,
QLabel#titleApplicationMenuShortcut:disabled,
QLabel#titleApplicationMenuArrow:disabled,
QLabel#titleApplicationMenuCheck:disabled {
    color: #777777;
}
QWidget#titleApplicationSubPage {
    background-color: #121212;
    border: none;
}
QWidget#titleApplicationSubPageContent,
QStackedWidget#titleApplicationSubStack,
QScrollArea#titleApplicationSubScroll,
QScrollArea#titleApplicationSubScroll > QWidget,
QScrollArea#titleApplicationSubScroll > QWidget > QWidget {
    background-color: #121212;
    border: none;
}
)").arg(cornerRadius);
    }

    return QStringLiteral(R"(
QFrame#titleApplicationPanel,
QFrame#titleApplicationSubPanel {
    background-color: transparent;
    border: none;
}
QFrame#titleApplicationMainMenu,
QFrame#titleApplicationSubMenu {
    background-color: #FDFDFC;
    border: 1px solid #EAEAE9;
    border-radius: %1px;
}
QFrame#titleApplicationMenuItem {
    background-color: transparent;
    border: none;
    border-radius: 0px;
}
QFrame#titleApplicationMenuItem[selected="true"],
QFrame#titleApplicationMenuItem:hover {
    background-color: #eeeeee;
}
QLabel#titleApplicationMenuText {
    color: #000000;
    background-color: transparent;
    border: none;
    padding: 0px;
}
QLabel#titleApplicationMenuShortcut,
QLabel#titleApplicationMenuArrow,
QLabel#titleApplicationMenuCheck {
    color: #4b5563;
    background-color: transparent;
    border: none;
    padding: 0px;
}
QLabel#titleApplicationMenuCheck {
    color: #6b7280;
}
QLabel#titleApplicationMenuText:disabled,
QLabel#titleApplicationMenuShortcut:disabled,
QLabel#titleApplicationMenuArrow:disabled,
QLabel#titleApplicationMenuCheck:disabled {
    color: #9ca3af;
}
QWidget#titleApplicationSubPage {
    background-color: #FDFDFC;
    border: none;
}
QWidget#titleApplicationSubPageContent,
QStackedWidget#titleApplicationSubStack,
QScrollArea#titleApplicationSubScroll,
QScrollArea#titleApplicationSubScroll > QWidget,
QScrollArea#titleApplicationSubScroll > QWidget > QWidget {
    background-color: #FDFDFC;
    border: none;
}
)").arg(cornerRadius);
}

QString customTitleBarStyleSheet(bool dark)
{
    if (dark)
    {
        return QStringLiteral(R"(
QWidget#customTitleBar {
    background-color: #0D0D0D;
    border-bottom: 1px solid #202020;
}
QLabel#customTitleLabel {
    color: #d8dee9;
    font-size: 15px;
    font-weight: 600;
    padding: 0px 8px;
}
QLabel#customTitleLogo {
    background-color: transparent;
}
QToolButton#titleBarButton,
QToolButton#titleBarMenuButton,
QToolButton#windowMinimizeButton,
QToolButton#windowMaximizeButton,
QToolButton#windowCloseButton {
    background-color: transparent;
    border: none;
    border-radius: 6px;
    padding: 0px;
    margin: 0px;
}
QToolButton#titleBarButton:hover,
QToolButton#titleBarMenuButton:hover,
QToolButton#windowMinimizeButton:hover,
QToolButton#windowMaximizeButton:hover {
    background-color: rgb(18, 18, 18);
}
QToolButton#windowCloseButton:hover {
    background-color: rgb(18, 18, 18);
}
QWidget#customTitleBar QToolButton::menu-indicator {
    image: none;
    width: 0px;
    height: 0px;
}
QFrame#titleBarSeparator {
    background-color: #202020;
    border: none;
}
)");
    }

    return QStringLiteral(R"(
QWidget#customTitleBar {
    background-color: #FDFDFC;
    border-bottom: 1px solid #EAEAE9;
}
QLabel#customTitleLabel {
    color: #000000;
    font-size: 15px;
    font-weight: 600;
    padding: 0px 8px;
}
QLabel#customTitleLogo {
    background-color: transparent;
}
QToolButton#titleBarButton,
QToolButton#titleBarMenuButton,
QToolButton#windowMinimizeButton,
QToolButton#windowMaximizeButton,
QToolButton#windowCloseButton {
    background-color: transparent;
    border: none;
    border-radius: 6px;
    padding: 0px;
    margin: 0px;
}
QToolButton#titleBarButton:hover,
QToolButton#titleBarMenuButton:hover,
QToolButton#windowMinimizeButton:hover,
QToolButton#windowMaximizeButton:hover {
    background-color: #EFEEEB;
}
QToolButton#titleBarButton:pressed,
QToolButton#titleBarMenuButton:pressed,
QToolButton#windowMinimizeButton:pressed,
QToolButton#windowMaximizeButton:pressed,
QToolButton#titleBarButton:checked,
QToolButton#titleBarMenuButton:checked,
QToolButton#windowMinimizeButton:checked,
QToolButton#windowMaximizeButton:checked {
    background-color: #EFEEEB;
}
QToolButton#windowCloseButton:hover {
    background-color: #fee2e2;
}
QWidget#customTitleBar QToolButton::menu-indicator {
    image: none;
    width: 0px;
    height: 0px;
}
QFrame#titleBarSeparator {
    background-color: #EAEAE9;
    border: none;
}
)");
}

#ifdef Q_OS_WIN
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFF
#endif

void setWindowsTitleBarDark(QWidget *window, bool dark)
{
    if (!window)
    {
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
    {
        return;
    }

    const BOOL useDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    const COLORREF captionColor = dark ? RGB(18, 18, 18) : DWMWA_COLOR_DEFAULT;
    const COLORREF textColor = dark ? RGB(229, 231, 235) : DWMWA_COLOR_DEFAULT;
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
}
#else
void setWindowsTitleBarDark(QWidget *, bool)
{
}
#endif

QString darkThemeStyleSheet()
{
    return QStringLiteral(R"(
QMainWindow {
    background-color: #0D0D0D;
}
QWidget#appCentralWidget,
QWidget#mainCardsPane,
QMainWindow#sessionViewerWindow,
QWidget#sessionViewerCentralWidget,
QWidget#sessionViewerViewport,
QWidget#sessionViewerContentPane,
QScrollArea#sessionViewerScrollArea,
QScrollArea#sessionViewerScrollArea > QWidget,
QScrollArea#sessionViewerScrollArea > QWidget > QWidget,
QSplitter#sessionViewerContentSplitter,
QScrollArea,
QScrollArea > QWidget,
QScrollArea > QWidget > QWidget,
QScrollArea#mainCardsScrollArea,
QWidget#mainCardsViewport,
QScrollArea#mainCardsScrollArea > QWidget,
QScrollArea#mainCardsScrollArea > QWidget > QWidget,
QAbstractScrollArea,
QSplitter {
    background-color: #0D0D0D;
}
QSplitter#mainContentSplitter,
QSplitter#mainContentSplitter > QWidget {
    background-color: #0D0D0D;
}
QMenuBar,
QToolBar,
QStatusBar,
QMenu,
QMessageBox {
    background-color: #121212;
    color: #d8dee9;
    border-color: #202020;
}
QMenuBar::item,
QMenu::item,
QToolBar QToolButton {
    color: #d8dee9;
}
QToolBar QToolButton {
    background-color: rgb(217, 119, 87);
}
QMenuBar::item:selected,
QMenu::item:selected,
QToolBar QToolButton:hover {
    background-color: rgb(217, 119, 87);
    color: #ffffff;
}
QToolTip {
    background-color: #121212;
    color: #ffffff;
    border: 1px solid #202020;
}
QMenuBar::item:pressed,
QToolBar QToolButton:pressed {
    background-color: rgb(217, 119, 87);
    color: #ffffff;
}
QToolBar::separator {
    background-color: #202020;
}
QGroupBox {
    background-color: #121212;
    border: 1px solid #202020;
    border-top: 40px solid #121212;
    color: #d8dee9;
}
QDialog#rtkConfigDialog,
QWidget#rtkConfigViewport,
QWidget#rtkConfigContent,
QScrollArea#rtkConfigScrollArea {
    background-color: #0D0D0D;
}
QDialog#rtkConfigDialog QGroupBox#rtkCardGroup {
    background-color: #121212;
    border: 1px solid #202020;
    border-radius: 8px;
    margin-top: 0px;
    padding: 0px;
    color: #e5e7eb;
}
QDialog#rtkConfigDialog QGroupBox#rtkCardGroup::title {
    color: transparent;
}
QDialog#rtkConfigDialog QWidget#sectionTitleBar {
    background-color: #121212;
    border: none;
    border-bottom: 1px solid #202020;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QDialog#rtkConfigDialog QLabel#sectionTitleLabel {
    background-color: transparent;
    border: none;
    color: #e5e7eb;
}
QGroupBox#sensorGroupBox {
    background-color: #121212;
    border: 1px solid #202020;
    border-radius: 8px;
    margin-top: 0px;
    padding: 0px;
    color: #e5e7eb;
}
QFrame#logPanelFrame {
    background-color: #121212;
    border: 1px solid #202020;
    border-radius: 8px;
}
QWidget#logSidePanel {
    background-color: #0D0D0D;
    border: none;
}
QFrame#logPanelFrame QWidget#sectionTitleBar {
    background-color: #121212;
    border: none;
    border-bottom: 1px solid #202020;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QFrame#logPanelFrame QToolButton#titleBarButton:hover {
    background-color: #202020;
}
QFrame#logPanelFrame QLabel#sectionTitleLabel {
    background-color: transparent;
    border: none;
    color: #ffffff;
}
QFrame#recordingStatusCard {
    background-color: #121212;
    border: 1px solid #202020;
    border-radius: 8px;
}
QFrame#recordingStatusCard QWidget#sectionTitleBar {
    background-color: #121212;
    border: none;
    border-bottom: 1px solid #202020;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QFrame#recordingStatusCard QLabel#sectionTitleLabel {
    background-color: transparent;
    border: none;
    color: #ffffff;
}
QWidget#recordingStatusBody {
    background-color: #121212;
    border: none;
    border-bottom-left-radius: 7px;
    border-bottom-right-radius: 7px;
}
QLabel#recordingStatusLabel {
    background-color: transparent;
    border: none;
    color: #ffffff;
    font-size: 14px;
    font-weight: 600;
}
QWidget#sectionTitleBar,
QLabel#sectionTitleLabel {
    background-color: #121212;
    border-color: #202020;
    color: #ffffff;
}
QLabel {
    color: #ffffff;
}
QLabel#fieldLabel,
QLabel#rateLabel,
QLabel#separatorLabel {
    color: #ffffff;
}
QLabel#rtkStatusLabel {
    color: #ffffff;
    font-weight: bold;
}
QFrame#epsilonSectionCard {
    background-color: #121212;
    border: 1px solid #202020;
}
QLabel#epsilonSectionLabel {
    color: #ffffff;
    background-color: #121212;
    border: none;
    border-right: 1px solid #202020;
    font-weight: 700;
}
QLabel#valueLabel {
    color: #ffffff;
    background-color: transparent;
    font-family: "Cascadia Mono", "Consolas", "Courier New", monospace;
}
QLabel#highlightedValue {
    color: #ffffff;
    background-color: #202020;
    font-family: "Cascadia Mono", "Consolas", "Courier New", monospace;
}
PtbPanel QLabel#highlightedValue,
HmpPanel QLabel#highlightedValue,
LidarPanel QLabel#highlightedValue {
    background-color: transparent;
}
QLabel#rateLabel {
    font-family: "Cascadia Mono", "Consolas", "Courier New", monospace;
}
QComboBox,
QLineEdit,
QSpinBox,
QDoubleSpinBox,
QTextEdit {
    background-color: #121212;
    border: 1px solid #202020;
    color: #e5e7eb;
    selection-background-color: #245b8f;
    selection-color: #ffffff;
}
QTextEdit#logTextEdit {
    background-color: #121212;
    border: none;
    border-radius: 0px;
}
QWidget#logTextViewport {
    background-color: #121212;
    border: none;
}
QComboBox:hover,
QLineEdit:hover,
QSpinBox:hover,
QDoubleSpinBox:hover {
    border-color: #202020;
}
QComboBox:focus,
QLineEdit:focus,
QSpinBox:focus,
QDoubleSpinBox:focus {
    border-color: #3b82f6;
}
QComboBox:disabled,
QLineEdit:disabled,
QSpinBox:disabled,
QDoubleSpinBox:disabled {
    background-color: #202020;
    color: #64748b;
}
QComboBox QAbstractItemView {
    background-color: #121212;
    border: 1px solid #202020;
    color: #e5e7eb;
    selection-background-color: #1f3f66;
    selection-color: #ffffff;
}
QPushButton {
    background-color: rgb(217, 119, 87);
    color: #ffffff;
    border: none;
}
QPushButton:hover,
QPushButton:pressed,
QPushButton:checked {
    background-color: rgb(217, 119, 87);
    color: #ffffff;
}
QPushButton:disabled {
    background-color: #202020;
    color: #cbd5e1;
}
QScrollBar:vertical,
QScrollBar:horizontal {
    background-color: #0C0C0C;
}
QScrollArea#mainCardsScrollArea QScrollBar:horizontal,
QScrollArea#mainCardsScrollArea QScrollBar:vertical {
    background-color: #0D0D0D;
}
QScrollBar::handle:vertical,
QScrollBar::handle:horizontal {
    background-color: #202020;
}
QScrollBar::handle:vertical:hover,
QScrollBar::handle:horizontal:hover {
    background-color: #202020;
}
QSplitter::handle,
QSplitter#mainContentSplitter::handle:horizontal {
    background-color: #0D0D0D;
}
QWidget#mainCardResizeHandle {
    min-height: 3px;
    max-height: 3px;
    background-color: #0D0D0D;
}
QSplitter#mainContentSplitter::handle:horizontal:hover {
    background-color: #202020;
}
QWidget#mainCardResizeHandle:hover {
    background-color: #202020;
}
QSplitter#mainContentSplitter::handle:horizontal:pressed {
    background-color: #202020;
}
QWidget#mainCardResizeHandle[dragging="true"] {
    background-color: #202020;
}
QCheckBox,
QRadioButton {
    color: #d8dee9;
}
QCheckBox::indicator,
QRadioButton::indicator {
    background-color: #121212;
    border-color: #202020;
}
QLabel[data-valid="true"] {
    color: #ffffff;
}
QLabel[data-valid="false"] {
    color: #ffffff;
}
QLabel#statusIndicator[status="connected"] {
    background-color: #123423;
    color: #68d391;
}
QLabel#statusIndicator[status="disconnected"] {
    background-color: #3a171b;
    color: #f87171;
}
QLabel#statusIndicator[status="warning"] {
    background-color: #3a2a12;
    color: #f6ad55;
}
)");
}

QPalette themedPalette(bool dark)
{
    QPalette palette = qApp->style() ? qApp->style()->standardPalette() : qApp->palette();
    if (!dark)
    {
        return palette;
    }

    palette.setColor(QPalette::Window, QColor("#0D0D0D"));
    palette.setColor(QPalette::WindowText, QColor("#d8dee9"));
    palette.setColor(QPalette::Base, QColor("#121212"));
    palette.setColor(QPalette::AlternateBase, QColor("#202020"));
    palette.setColor(QPalette::Text, QColor("#e5e7eb"));
    palette.setColor(QPalette::Button, QColor("#121212"));
    palette.setColor(QPalette::ButtonText, QColor("#e5e7eb"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Light, QColor("#202020"));
    palette.setColor(QPalette::Midlight, QColor("#202020"));
    palette.setColor(QPalette::Mid, QColor("#202020"));
    palette.setColor(QPalette::Dark, QColor("#0C0C0C"));
    palette.setColor(QPalette::Shadow, QColor("#0C0C0C"));
    palette.setColor(QPalette::Highlight, QColor("#245b8f"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::ToolTipBase, QColor("#121212"));
    palette.setColor(QPalette::ToolTipText, QColor("#e5e7eb"));
    palette.setColor(QPalette::Link, QColor("#7db7ff"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#64748b"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#64748b"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#94a3b8"));
    return palette;
}

#pragma pack(push, 1)
struct UnifiedRawFileHeader
{
    char magic[8];
    quint32 version;
    quint32 header_size;
    quint16 source_id;
    quint16 reserved;
};

struct UnifiedRawRecordHeader
{
    quint32 marker;
    quint32 header_size;
    quint64 host_timestamp_us;
    quint32 payload_size;
    quint16 source_id;
    quint16 record_type;
    quint32 flags;
    quint64 sequence;
};
#pragma pack(pop)

QString recordingTimestampUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString recordingSessionDirectoryTimestamp()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
}

QString csvEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace("\"", "\"\"");
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n') || escaped.contains('\r'))
    {
        escaped = QString("\"%1\"").arg(escaped);
    }
    return escaped;
}

QString csvBool(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString imuFrameTypeName(VaporView::ImuFrameType type)
{
    switch (type)
    {
    case VaporView::ImuFrameType::HI81:
        return QStringLiteral("HI81");
    case VaporView::ImuFrameType::HI83:
        return QStringLiteral("HI83");
    case VaporView::ImuFrameType::HI91:
        return QStringLiteral("HI91");
    case VaporView::ImuFrameType::HI92:
        return QStringLiteral("HI92");
    case VaporView::ImuFrameType::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

QString imuRatePeriodText(int hz)
{
    switch (hz)
    {
    case 1: return QStringLiteral("1");
    case 2: return QStringLiteral("0.5");
    case 5: return QStringLiteral("0.2");
    case 10: return QStringLiteral("0.1");
    case 20: return QStringLiteral("0.05");
    case 50: return QStringLiteral("0.02");
    case 100: return QStringLiteral("0.01");
    case 200: return QStringLiteral("0.005");
    case 250: return QStringLiteral("0.004");
    case 500: return QStringLiteral("0.002");
    case 1000: return QStringLiteral("0.001");
    default: return QString();
    }
}

void applyComboText(QComboBox *combo, const QString& value)
{
    if (!combo || value.isEmpty())
    {
        return;
    }
    const QSignalBlocker blocker(combo);
    const int idx = combo->findText(value);
    if (idx >= 0)
    {
        combo->setCurrentIndex(idx);
        return;
    }
    if (combo->isEditable())
    {
        combo->setEditText(value);
    }
    else
    {
        combo->setCurrentText(value);
    }
}

QString sourceModeDisplayText(bool english, int index)
{
    return index == 1
        ? (english ? QStringLiteral("Sky-Ground Receive Mode") : QStringLiteral("天空-地面接收模式"))
        : (english ? QStringLiteral("Local") : QStringLiteral("本地"));
}

QString sourceModeStorageValue(int index)
{
    return index == 1 ? QStringLiteral("remote") : QStringLiteral("local");
}

int sourceModeIndexFromStoredValue(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("remote") ||
        normalized == QStringLiteral("remote sky") ||
        normalized == QStringLiteral("1") ||
        normalized.contains(QStringLiteral("天空")))
    {
        return 1;
    }
    if (normalized == QStringLiteral("local") ||
        normalized == QStringLiteral("0") ||
        normalized.contains(QStringLiteral("本地")))
    {
        return 0;
    }
    return -1;
}

bool shouldMirrorToErrorLog(const QString& message)
{
    static const QStringList keywords = {
        QStringLiteral("error"),
        QStringLiteral("failed"),
        QStringLiteral("timeout"),
        QStringLiteral("exception"),
        QStringLiteral("disconnect"),
        QStringLiteral("异常"),
        QStringLiteral("失败"),
        QStringLiteral("错误"),
        QStringLiteral("超时"),
        QStringLiteral("掉线"),
        QStringLiteral("断开"),
    };
    const QString lower = message.toLower();
    for (const QString& keyword : keywords)
    {
        if (lower.contains(keyword.toLower()))
        {
            return true;
        }
    }
    return false;
}
void rememberBaseMetric(QObject *object, const char *propertyName, int value)
{
    if (!object->property(propertyName).isValid())
    {
        object->setProperty(propertyName, value);
    }
}

struct EpsilonPacketConfigOption
{
    quint8 packet_id = 0;
    const char *message_name = nullptr;
    const char *title_zh = nullptr;
    const char *title_en = nullptr;
    std::vector<int> supported_rates_hz;
};

constexpr int kEpsilonPacketConfigApplyVersion = 2;

const std::vector<EpsilonPacketConfigOption>& epsilonPacketConfigOptions()
{
    static const std::vector<EpsilonPacketConfigOption> kOptions = {
        {0x40, "MSG_IMU", "IMU原始数据", "IMU Raw Data", {0, 1, 2, 5, 10, 20, 50, 100, 200, 250, 500, 1000}},
        {0x41, "MSG_AHRS", "AHRS姿态解", "AHRS Attitude", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x42, "MSG_INSGPS", "INS/GPS融合解", "INS/GPS Navigation", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x50, "MSG_SYS_STATE", "系统状态", "System State", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x59, "MSG_RAW_GNSS", "原始GNSS", "Raw GNSS", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5A, "MSG_SATELLITE", "卫星汇总", "Satellite Summary", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5C, "MSG_GEODETIC_POS", "大地坐标", "Geodetic Position", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5D, "MSG_ECEF_POS", "ECEF坐标", "ECEF Position", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
    };
    return kOptions;
}

QString epsilonPacketRateSettingsKey(quint8 packetId)
{
    return QStringLiteral("epsilon_custom_packet_rate_%1")
        .arg(packetId, 2, 16, QLatin1Char('0'))
        .toUpper();
}

int nearestSupportedEpsilonPacketRate(const EpsilonPacketConfigOption& option, int desiredRateHz)
{
    int fallbackRateHz = 0;
    for (int rateHz : option.supported_rates_hz)
    {
        if (rateHz == desiredRateHz)
        {
            return rateHz;
        }
        if (rateHz <= desiredRateHz)
        {
            fallbackRateHz = rateHz;
        }
    }
    return fallbackRateHz;
}

std::map<uint8_t, int> groupedEpsilonPacketRates(int baseRateHz)
{
    const int lowRateHz = std::min(baseRateHz, 20);
    std::map<uint8_t, int> rates;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const int desiredRateHz =
            (option.packet_id == 0x59 || option.packet_id == 0x5A ||
             option.packet_id == 0x5C || option.packet_id == 0x5D)
                ? lowRateHz
                : baseRateHz;
        rates[option.packet_id] = nearestSupportedEpsilonPacketRate(option, desiredRateHz);
    }
    return rates;
}

std::map<uint8_t, int> defaultEpsilonPacketRates()
{
    return {
        {0x40, 250},
        {0x41, 50},
        {0x42, 100},
        {0x50, 100},
        {0x59, 10},
        {0x5A, 1},
        {0x5C, 10},
        {0x5D, 10},
    };
}

bool epsilonPacketRateSupported(const EpsilonPacketConfigOption& option, int rateHz)
{
    return std::find(option.supported_rates_hz.cbegin(), option.supported_rates_hz.cend(), rateHz) != option.supported_rates_hz.cend();
}

std::map<uint8_t, int> loadCustomEpsilonPacketRates(QSettings& settings, int fallbackBaseRateHz)
{
    std::map<uint8_t, int> packetRates = groupedEpsilonPacketRates(fallbackBaseRateHz);
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const int fallbackRate = packetRates[option.packet_id];
        const int storedRate = settings.value(epsilonPacketRateSettingsKey(option.packet_id), fallbackRate).toInt();
        packetRates[option.packet_id] = epsilonPacketRateSupported(option, storedRate) ? storedRate : fallbackRate;
    }
    return packetRates;
}

std::map<uint8_t, int> effectiveEpsilonPacketRates(QSettings& settings, int baseRateHz, bool *usingCustomProfile = nullptr)
{
    bool useCustomProfile = settings.value("epsilon_custom_packet_rates_enabled", false).toBool();
    if (useCustomProfile &&
        !settings.value("epsilon_custom_packet_rates_user_saved", false).toBool() &&
        loadCustomEpsilonPacketRates(settings, baseRateHz) == defaultEpsilonPacketRates())
    {
        useCustomProfile = false;
    }
    if (usingCustomProfile)
    {
        *usingCustomProfile = useCustomProfile;
    }
    return useCustomProfile ? loadCustomEpsilonPacketRates(settings, baseRateHz) : groupedEpsilonPacketRates(baseRateHz);
}

QString epsilonPacketRatesSignature(const std::map<uint8_t, int>& packetRates)
{
    QStringList parts;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = packetRates.find(option.packet_id);
        const int rateHz = (it != packetRates.end()) ? it->second : -1;
        parts << QStringLiteral("%1=%2")
                     .arg(option.packet_id, 2, 16, QLatin1Char('0'))
                     .toUpper()
                     .arg(rateHz);
    }
    return parts.join(';');
}

QString epsilonPacketRatesSummary(const std::map<uint8_t, int>& packetRates)
{
    QStringList parts;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = packetRates.find(option.packet_id);
        if (it == packetRates.end())
        {
            continue;
        }
        parts << QStringLiteral("%1=%2Hz")
                     .arg(option.packet_id, 2, 16, QLatin1Char('0'))
                     .toUpper()
                     .arg(it->second);
    }
    return parts.join(QStringLiteral(", "));
}

int epsilonPacketCallbackRate(const std::map<uint8_t, int>& packetRates, int fallbackRateHz)
{
    int maxRateHz = 0;
    for (const auto& entry : packetRates)
    {
        maxRateHz = std::max(maxRateHz, entry.second);
    }
    return maxRateHz > 0 ? maxRateHz : fallbackRateHz;
}

QString epsilonPacketDialogRowLabel(const EpsilonPacketConfigOption& option, bool english)
{
    const int maxRateHz = option.supported_rates_hz.empty() ? 0 : option.supported_rates_hz.back();
    if (english)
    {
        return QStringLiteral("%1 [%2]  (Max %3 Hz)")
            .arg(QString::fromLatin1(option.message_name))
            .arg(option.packet_id, 2, 16, QLatin1Char('0'))
            .arg(maxRateHz);
    }
    return QStringLiteral("%1 [%2]（最大 %3 Hz）")
        .arg(QString::fromUtf8(option.title_zh))
        .arg(option.packet_id, 2, 16, QLatin1Char('0'))
        .arg(maxRateHz);
}
}

class EpsilonPanel : public QWidget
{
public:
    explicit EpsilonPanel(QLabel *rateLabel = nullptr, QWidget *parent = nullptr)
        : QWidget(parent)
        , rate_label_(rateLabel)
        , is_english_(false)
        , total_rate_hz_(0.0)
        , imu_packet_rate_hz_(0.0)
        , ahrs_packet_rate_hz_(0.0)
        , insgps_packet_rate_hz_(0.0)
        , sys_state_packet_rate_hz_(0.0)
        , raw_gnss_packet_rate_hz_(0.0)
        , satellite_packet_rate_hz_(0.0)
        , geodetic_packet_rate_hz_(0.0)
        , ecef_packet_rate_hz_(0.0)
    {
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        setupUi();
        setEnglish(false);
    }

    void updateRate(double hz)
    {
        total_rate_hz_ = hz;
        refreshRateLabel();
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        refreshRateLabel();
        for (auto it = title_labels_.cbegin(); it != title_labels_.cend(); ++it)
        {
            it.value()->setText(english ? title_en_.value(it.key()) : title_zh_.value(it.key()));
        }
        for (auto it = section_labels_.cbegin(); it != section_labels_.cend(); ++it)
        {
            const QString text = english ? section_en_.value(it.key()) : section_zh_.value(it.key());
            it.value()->setText(formatSectionTitle(text, english));
        }
    }

    void updateData(const VaporView::EpsilonData& epsilon_data)
    {
        auto setValue = [this](const QString& key, const QString& value) {
            if (QLabel *label = value_labels_.value(key, nullptr))
            {
                const QString display_text = value.isEmpty() ? QStringLiteral("--") : value;
                label->setText(display_text);
                label->setToolTip(display_text);
            }
        };
        if (!epsilon_data.valid)
        {
            total_rate_hz_ = 0.0;
            imu_packet_rate_hz_ = 0.0;
            ahrs_packet_rate_hz_ = 0.0;
            insgps_packet_rate_hz_ = 0.0;
            sys_state_packet_rate_hz_ = 0.0;
            raw_gnss_packet_rate_hz_ = 0.0;
            satellite_packet_rate_hz_ = 0.0;
            geodetic_packet_rate_hz_ = 0.0;
            ecef_packet_rate_hz_ = 0.0;
            refreshRateLabel();
            for (QLabel *label : value_labels_)
            {
                if (label)
                {
                    label->setText(QStringLiteral("--"));
                    label->setToolTip(QStringLiteral("--"));
                }
            }
            return;
        }
        auto scalar = [](double value, int decimals) {
            if (!std::isfinite(value))
            {
                return QString();
            }
            return QString::number(value, 'f', decimals);
        };
        auto axisTriple = [&](const QString& firstLabel,
                              const QString& secondLabel,
                              const QString& thirdLabel,
                              double a,
                              double b,
                              double c,
                              int decimals) {
            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
            {
                return QString();
            }
            return QStringLiteral("%1 %2 / %3 %4 / %5 %6")
                .arg(firstLabel)
                .arg(a, 0, 'f', decimals)
                .arg(secondLabel)
                .arg(b, 0, 'f', decimals)
                .arg(thirdLabel)
                .arg(c, 0, 'f', decimals);
        };
        const bool gnss_fix_valid = epsilon_data.gnss_fix_code >= 2;
        const bool utc_valid = epsilon_data.utc_unix_s > 0;

        const QString utcText = utc_valid
            ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epsilon_data.utc_unix_s), QTimeZone::UTC)
                  .addMSecs(static_cast<qint64>(epsilon_data.utc_microseconds / 1000U))
                  .toString(Qt::ISODateWithMs)
            : QString();

        setValue(QStringLiteral("time_utc"), utcText);
        setValue(QStringLiteral("device_ts"), epsilon_data.device_timestamp_us > 0
            ? QStringLiteral("%1 us").arg(epsilon_data.device_timestamp_us)
            : QString());
        imu_packet_rate_hz_ = epsilon_data.imu_packet_rate_hz;
        ahrs_packet_rate_hz_ = epsilon_data.ahrs_packet_rate_hz;
        insgps_packet_rate_hz_ = epsilon_data.insgps_packet_rate_hz;
        sys_state_packet_rate_hz_ = epsilon_data.sys_state_packet_rate_hz;
        raw_gnss_packet_rate_hz_ = epsilon_data.raw_gnss_packet_rate_hz;
        satellite_packet_rate_hz_ = epsilon_data.satellite_packet_rate_hz;
        geodetic_packet_rate_hz_ = epsilon_data.geodetic_packet_rate_hz;
        ecef_packet_rate_hz_ = epsilon_data.ecef_packet_rate_hz;
        refreshRateLabel();
        setValue(QStringLiteral("fix"), QString::fromStdString(epsilon_data.gnss_fix_text));
        setValue(QStringLiteral("sat"), epsilon_data.gnss_satellites > 0 ? QString::number(epsilon_data.gnss_satellites) : QString());
        setValue(QStringLiteral("lat"), gnss_fix_valid ? scalar(epsilon_data.latitude_deg, 8) : QString());
        setValue(QStringLiteral("lon"), gnss_fix_valid ? scalar(epsilon_data.longitude_deg, 8) : QString());
        setValue(QStringLiteral("height"), gnss_fix_valid ? scalar(epsilon_data.height_m, 3) : QString());
        setValue(QStringLiteral("ned_vel"), gnss_fix_valid
            ? axisTriple(QStringLiteral("N"), QStringLiteral("E"), QStringLiteral("D"),
                  epsilon_data.vel_n_mps, epsilon_data.vel_e_mps, epsilon_data.vel_d_mps, 3)
            : QString());
        setValue(QStringLiteral("imu_acc"),
                 axisTriple(QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
                     epsilon_data.imu_acc_x_mps2, epsilon_data.imu_acc_y_mps2, epsilon_data.imu_acc_z_mps2, 3));
        setValue(QStringLiteral("imu_gyr"),
                 axisTriple(QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
                     epsilon_data.imu_gyr_x_radps, epsilon_data.imu_gyr_y_radps, epsilon_data.imu_gyr_z_radps, 4));
        setValue(QStringLiteral("rpy"),
                 axisTriple(QStringLiteral("Roll"), QStringLiteral("Pitch"), QStringLiteral("Yaw"),
                     epsilon_data.roll_deg, epsilon_data.pitch_deg, epsilon_data.yaw_deg, 2));
        setValue(QStringLiteral("acc"),
                 gnss_fix_valid && std::isfinite(epsilon_data.hacc_m) && std::isfinite(epsilon_data.vacc_m)
                     ? QStringLiteral("hAcc %1 m / vAcc %2 m")
                           .arg(epsilon_data.hacc_m, 0, 'f', 3)
                           .arg(epsilon_data.vacc_m, 0, 'f', 3)
                     : QString());
        setValue(QStringLiteral("heading_valid"), boolText(epsilon_data.heading_valid));
        setValue(QStringLiteral("status_bits"), formatSystemStatus(epsilon_data.system_status_bits));
        setValue(QStringLiteral("filter_bits"), formatFilterStatus(epsilon_data.filter_status_bits, gnss_fix_valid));
        setValue(QStringLiteral("frames"),
                 is_english_
                     ? QStringLiteral("raw %1 / dropped %2")
                           .arg(epsilon_data.raw_frame_count)
                           .arg(epsilon_data.dropped_frame_count)
                     : QStringLiteral("原始 %1 / 丢帧 %2")
                           .arg(epsilon_data.raw_frame_count)
                           .arg(epsilon_data.dropped_frame_count));
    }

private:
    QString formatRateValue(double hz) const
    {
        if (!(hz > 0.0) || !std::isfinite(hz))
        {
            return QStringLiteral("-- Hz");
        }
        return QStringLiteral("%1 Hz").arg(hz, 0, 'f', hz >= 100.0 ? 0 : 1);
    }

    QString boolText(bool value) const
    {
        if (is_english_)
        {
            return value ? QStringLiteral("Yes") : QStringLiteral("No");
        }
        return value ? QStringLiteral("是") : QStringLiteral("否");
    }

    QString formatHex16(quint16 value) const
    {
        return QStringLiteral("0x%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
    }

    QString formatSystemStatus(quint16 bits) const
    {
        if (bits == 0)
        {
            return is_english_
                ? QStringLiteral("%1 OK").arg(formatHex16(bits))
                : QStringLiteral("%1 正常").arg(formatHex16(bits));
        }
        return is_english_
            ? QStringLiteral("%1 Check").arg(formatHex16(bits))
            : QStringLiteral("%1 需检查").arg(formatHex16(bits));
    }

    QString formatFilterStatus(quint16 bits, bool fusionActive) const
    {
        QStringList states;
        if (bits == 0)
        {
            states << (is_english_ ? QStringLiteral("not initialized") : QStringLiteral("未初始化"));
        }
        else
        {
            states << (is_english_ ? QStringLiteral("initialized") : QStringLiteral("已初始化"));
            if (fusionActive)
            {
                states << (is_english_ ? QStringLiteral("position fusion active") : QStringLiteral("定位融合中"));
            }
        }
        return QStringLiteral("%1 %2").arg(formatHex16(bits), states.join(QStringLiteral(" / ")));
    }

    void refreshRateLabel()
    {
        if (!rate_label_)
        {
            return;
        }

        const QString totalText = is_english_
            ? QStringLiteral("Total Rate: %1").arg(formatRateValue(total_rate_hz_))
            : QStringLiteral("总频率：%1").arg(formatRateValue(total_rate_hz_));
        const QStringList parts = {
            totalText,
            QStringLiteral("40 %1").arg(formatRateValue(imu_packet_rate_hz_)),
            QStringLiteral("41 %1").arg(formatRateValue(ahrs_packet_rate_hz_)),
            QStringLiteral("42 %1").arg(formatRateValue(insgps_packet_rate_hz_)),
            QStringLiteral("50 %1").arg(formatRateValue(sys_state_packet_rate_hz_)),
            QStringLiteral("59 %1").arg(formatRateValue(raw_gnss_packet_rate_hz_)),
            QStringLiteral("5A %1").arg(formatRateValue(satellite_packet_rate_hz_)),
            QStringLiteral("5C %1").arg(formatRateValue(geodetic_packet_rate_hz_)),
            QStringLiteral("5D %1").arg(formatRateValue(ecef_packet_rate_hz_)),
        };
        const QString text = parts.join(QStringLiteral("   |   "));
        rate_label_->setText(text);
        rate_label_->setToolTip(text);
    }

    static QString formatSectionTitle(const QString& text, bool english)
    {
        if (english)
        {
            QString formatted = text;
            formatted.replace(QStringLiteral(" / "), QStringLiteral("\n/\n"));
            formatted.replace(QChar(' '), QChar('\n'));
            return formatted;
        }

        QStringList chars;
        chars.reserve(text.size());
        for (const QChar ch : text)
        {
            if (!ch.isSpace())
            {
                chars.append(QString(ch));
            }
        }
        return chars.join(QChar('\n'));
    }

    void registerSectionLabel(QLabel *label,
                              const QString& key,
                              const QString& zhTitle,
                              const QString& enTitle)
    {
        label->setObjectName(QStringLiteral("epsilonSectionLabel"));
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumWidth(kEpsilonSideTitleWidth);
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        section_labels_.insert(key, label);
        section_zh_.insert(key, zhTitle);
        section_en_.insert(key, enTitle);
    }

    QGridLayout *addSectionCard(QVBoxLayout *columnLayout,
                                const QString& key,
                                const QString& zhTitle,
                                const QString& enTitle,
                                int titleColumnWidth,
                                int valueColumnWidth)
    {
        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("epsilonSectionCard"));
        card->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

        auto *outerLayout = new QHBoxLayout(card);
        outerLayout->setContentsMargins(2, 2, 2, 2);
        outerLayout->setSpacing(2);

        auto *sectionLabel = new QLabel(card);
        registerSectionLabel(sectionLabel, key, zhTitle, enTitle);
        outerLayout->addWidget(sectionLabel);

        auto *cardLayout = new QGridLayout();
        cardLayout->setContentsMargins(2, 2, 2, 2);
        cardLayout->setHorizontalSpacing(6);
        cardLayout->setVerticalSpacing(2);
        cardLayout->setColumnMinimumWidth(0, titleColumnWidth);
        cardLayout->setColumnMinimumWidth(1, valueColumnWidth);
        cardLayout->setColumnStretch(0, 0);
        cardLayout->setColumnStretch(1, 0);
        outerLayout->addLayout(cardLayout, 0);
        columnLayout->addWidget(card, 0, Qt::AlignLeft);
        return cardLayout;
    }

    void addField(QGridLayout *layout,
                  int row,
                  int column,
                  const QString& key,
                  const QString& zhTitle,
                  const QString& enTitle,
                  int valueColumnWidth)
    {
        QLabel *title = new QLabel(this);
        title->setObjectName(QStringLiteral("fieldLabel"));
        title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        QLabel *value = new QLabel(QStringLiteral("--"), this);
        value->setObjectName(QStringLiteral("valueLabel"));
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(false);
        value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        value->setMinimumHeight(20);
        value->setMinimumWidth(valueColumnWidth);
        layout->addWidget(title, row, column * 2);
        layout->addWidget(value, row, column * 2 + 1);
        title_labels_.insert(key, title);
        value_labels_.insert(key, value);
        title_zh_.insert(key, zhTitle);
        title_en_.insert(key, enTitle);
    }

    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(2);

        if (!rate_label_)
        {
            rate_label_ = new QLabel(this);
            rate_label_->setObjectName(QStringLiteral("rateLabel"));
            layout->addWidget(rate_label_, 0, Qt::AlignLeft);
        }

        auto *columnsLayout = new QHBoxLayout();
        columnsLayout->setContentsMargins(0, 0, 0, 0);
        columnsLayout->setSpacing(2);

        auto createColumn = [columnsLayout]() {
            auto *columnLayout = new QVBoxLayout();
            columnLayout->setContentsMargins(0, 0, 0, 0);
            columnLayout->setSpacing(2);
            columnsLayout->addLayout(columnLayout, 0);
            return columnLayout;
        };

        QVBoxLayout *leftColumn = createColumn();
        QVBoxLayout *middleColumn = createColumn();
        QVBoxLayout *rightColumn = createColumn();

        QGridLayout *statusGrid = addSectionCard(leftColumn,
                                                 QStringLiteral("status"),
                                                 QStringLiteral("总体状态"),
                                                 QStringLiteral("Overall Status"),
                                                 kEpsilonTitleColumnWidth,
                                                 kEpsilonLeftValueColumnWidth);
        int row = 0;
        addField(statusGrid, row++, 0, QStringLiteral("time_utc"), QStringLiteral("UTC时间:"), QStringLiteral("UTC Time:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("device_ts"), QStringLiteral("设备时间戳:"), QStringLiteral("Device Timestamp:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("frames"), QStringLiteral("原始帧/丢帧:"), QStringLiteral("Raw/Dropped Frames:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("status_bits"), QStringLiteral("系统状态:"), QStringLiteral("System Status:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("filter_bits"), QStringLiteral("滤波状态:"), QStringLiteral("Filter Status:"), kEpsilonLeftValueColumnWidth);
        addField(statusGrid, row++, 0, QStringLiteral("heading_valid"), QStringLiteral("航向有效:"), QStringLiteral("Heading Valid:"), kEpsilonLeftValueColumnWidth);

        QGridLayout *positionGrid = addSectionCard(middleColumn,
                                                   QStringLiteral("position"),
                                                   QStringLiteral("定位状态"),
                                                   QStringLiteral("Position Status"),
                                                   kEpsilonTitleColumnWidth,
                                                   kEpsilonPositionValueColumnWidth);
        row = 0;
        addField(positionGrid, row++, 0, QStringLiteral("fix"), QStringLiteral("GNSS状态:"), QStringLiteral("GNSS Fix:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("sat"), QStringLiteral("卫星数:"), QStringLiteral("Satellites:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("lat"), QStringLiteral("纬度[deg]:"), QStringLiteral("Latitude [deg]:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("lon"), QStringLiteral("经度[deg]:"), QStringLiteral("Longitude [deg]:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("height"), QStringLiteral("高度[m]:"), QStringLiteral("Height [m]:"), kEpsilonPositionValueColumnWidth);
        addField(positionGrid, row++, 0, QStringLiteral("acc"), QStringLiteral("hAcc / vAcc:"), QStringLiteral("hAcc / vAcc:"), kEpsilonPositionValueColumnWidth);

        QGridLayout *motionGrid = addSectionCard(rightColumn,
                                                 QStringLiteral("motion"),
                                                 QStringLiteral("姿态与运动"),
                                                 QStringLiteral("Attitude / Motion"),
                                                 kEpsilonMotionTitleColumnWidth,
                                                 kEpsilonMotionValueColumnWidth);
        row = 0;
        addField(motionGrid, row++, 0, QStringLiteral("ned_vel"), QStringLiteral("NED速度[m/s]:"), QStringLiteral("NED Velocity [m/s]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("imu_acc"), QStringLiteral("IMU加速度[m/s²]:"), QStringLiteral("IMU Accel [m/s²]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("imu_gyr"), QStringLiteral("IMU角速度[rad/s]:"), QStringLiteral("IMU Gyro [rad/s]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("rpy"), QStringLiteral("姿态角[deg]:"), QStringLiteral("Attitude [deg]:"), kEpsilonMotionValueColumnWidth);

        for (QVBoxLayout *columnLayout : {leftColumn, middleColumn, rightColumn})
        {
            columnLayout->addStretch(1);
        }
        columnsLayout->addStretch(1);
        layout->addLayout(columnsLayout, 0);
        layout->addStretch(1);
    }

    QLabel *rate_label_;
    QHash<QString, QLabel*> section_labels_;
    QHash<QString, QLabel*> title_labels_;
    QHash<QString, QLabel*> value_labels_;
    QHash<QString, QString> section_zh_;
    QHash<QString, QString> section_en_;
    QHash<QString, QString> title_zh_;
    QHash<QString, QString> title_en_;
    bool is_english_;
    double total_rate_hz_;
    double imu_packet_rate_hz_;
    double ahrs_packet_rate_hz_;
    double insgps_packet_rate_hz_;
    double sys_state_packet_rate_hz_;
    double raw_gnss_packet_rate_hz_;
    double satellite_packet_rate_hz_;
    double geodetic_packet_rate_hz_;
    double ecef_packet_rate_hz_;
};

GnssPanel::GnssPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , status_label_(nullptr)
    , time_label_(nullptr)
    , lat_label_(nullptr)
    , lon_label_(nullptr)
    , alt_label_(nullptr)
    , vel_n_label_(nullptr)
    , vel_e_label_(nullptr)
    , vel_ground_label_(nullptr)
    , heading_label_(nullptr)
    , pitch_label_(nullptr)
    , heading_len_label_(nullptr)
    , heading_type_label_(nullptr)
    , heading_sats_label_(nullptr)
    , sats_label_(nullptr)
    , gdop_label_(nullptr)
    , pdop_label_(nullptr)
    , hdop_label_(nullptr)
    , htdop_label_(nullptr)
    , tdop_label_(nullptr)
    , diff_age_label_(nullptr)
    , undulation_label_(nullptr)
    , sigma_lat_label_(nullptr)
    , sigma_lon_label_(nullptr)
    , sigma_alt_label_(nullptr)
    , cutoff_label_(nullptr)
    , status_lbl_(nullptr)
    , time_lbl_(nullptr)
    , lat_lbl_(nullptr)
    , lon_lbl_(nullptr)
    , alt_lbl_(nullptr)
    , vel_n_lbl_(nullptr)
    , vel_e_lbl_(nullptr)
    , vel_ground_lbl_(nullptr)
    , heading_lbl_(nullptr)
    , pitch_lbl_(nullptr)
    , heading_type_lbl_(nullptr)
    , heading_len_lbl_(nullptr)
    , heading_sats_lbl_(nullptr)
    , sats_lbl_(nullptr)
    , diff_lbl_(nullptr)
    , gdop_lbl_(nullptr)
    , pdop_lbl_(nullptr)
    , hdop_lbl_(nullptr)
    , htdop_lbl_(nullptr)
    , tdop_lbl_(nullptr)
    , cutoff_lbl_(nullptr)
    , undulation_lbl_(nullptr)
    , sigma_lat_lbl_(nullptr)
    , sigma_lon_lbl_(nullptr)
    , sigma_alt_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void GnssPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mainLayout->addWidget(rate_label_);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(1);

    auto *midLayout = new QGridLayout();
    midLayout->setVerticalSpacing(4);
    midLayout->setHorizontalSpacing(1);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(1);

    auto createRow = [](QGridLayout* grid, int row, QLabel*& lbl, QLabel*& valueLabel, QWidget* parent) {
        lbl = new QLabel(parent);
        lbl->setObjectName("fieldLabel");
        lbl->setMinimumHeight(22);
        valueLabel = new QLabel("---", parent);
        valueLabel->setObjectName("valueLabel");
        valueLabel->setMinimumHeight(22);
        grid->addWidget(lbl, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };

    createRow(leftLayout, 0, status_lbl_, status_label_, this);
    createRow(leftLayout, 1, time_lbl_, time_label_, this);
    createRow(leftLayout, 2, lat_lbl_, lat_label_, this);
    createRow(leftLayout, 3, lon_lbl_, lon_label_, this);
    createRow(leftLayout, 4, alt_lbl_, alt_label_, this);
    createRow(leftLayout, 5, sigma_lat_lbl_, sigma_lat_label_, this);
    createRow(leftLayout, 6, sigma_lon_lbl_, sigma_lon_label_, this);
    createRow(leftLayout, 7, sigma_alt_lbl_, sigma_alt_label_, this);
    createRow(leftLayout, 8, undulation_lbl_, undulation_label_, this);

    createRow(midLayout, 0, vel_n_lbl_, vel_n_label_, this);
    createRow(midLayout, 1, vel_e_lbl_, vel_e_label_, this);
    createRow(midLayout, 2, vel_ground_lbl_, vel_ground_label_, this);
    createRow(midLayout, 3, heading_lbl_, heading_label_, this);
    createRow(midLayout, 4, pitch_lbl_, pitch_label_, this);
    createRow(midLayout, 5, heading_type_lbl_, heading_type_label_, this);
    createRow(midLayout, 6, heading_len_lbl_, heading_len_label_, this);
    createRow(midLayout, 7, heading_sats_lbl_, heading_sats_label_, this);
    createRow(midLayout, 8, sats_lbl_, sats_label_, this);
    createRow(midLayout, 9, diff_lbl_, diff_age_label_, this);

    createRow(rightLayout, 0, gdop_lbl_, gdop_label_, this);
    createRow(rightLayout, 1, pdop_lbl_, pdop_label_, this);
    createRow(rightLayout, 2, hdop_lbl_, hdop_label_, this);
    createRow(rightLayout, 3, htdop_lbl_, htdop_label_, this);
    createRow(rightLayout, 4, tdop_lbl_, tdop_label_, this);
    createRow(rightLayout, 5, cutoff_lbl_, cutoff_label_, this);

    if (time_label_)
    {
        time_label_->setWordWrap(true);
        time_label_->setMinimumHeight(40);
        time_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    leftLayout->setColumnStretch(1, 1);
    midLayout->setColumnStretch(1, 1);
    rightLayout->setColumnStretch(1, 1);

    colsLayout->addLayout(leftLayout, 1);
    colsLayout->addLayout(midLayout, 1);
    colsLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(colsLayout);
    mainLayout->addStretch();
    setEnglish(false);
}

void GnssPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(QString::asprintf("%.1f Hz", hz));
    }
}

void GnssPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        status_lbl_->setText("Status:");
        time_lbl_->setText("Time:");
        lat_lbl_->setText("Lat:");
        lon_lbl_->setText("Lon:");
        alt_lbl_->setText("Alt:");
        sigma_lat_lbl_->setText("σ Lat:");
        sigma_lon_lbl_->setText("σ Lon:");
        sigma_alt_lbl_->setText("σ Alt:");
        undulation_lbl_->setText("Undul:");
        vel_n_lbl_->setText("Vel N:");
        vel_e_lbl_->setText("Vel E:");
        vel_ground_lbl_->setText("Vel Gnd:");
        heading_lbl_->setText("Heading:");
        pitch_lbl_->setText("Pitch:");
        heading_type_lbl_->setText("Hd Type:");
        heading_len_lbl_->setText("Base L:");
        heading_sats_lbl_->setText("Hd Sats:");
        sats_lbl_->setText("Sats:");
        diff_lbl_->setText("Diff:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        htdop_lbl_->setText("HTDOP:");
        tdop_lbl_->setText("TDOP:");
        cutoff_lbl_->setText("Cutoff:");
    }
    else
    {
        status_lbl_->setText("状态:");
        time_lbl_->setText("时间:");
        lat_lbl_->setText("纬度:");
        lon_lbl_->setText("经度:");
        alt_lbl_->setText("高度:");
        sigma_lat_lbl_->setText("纬度σ:");
        sigma_lon_lbl_->setText("经度σ:");
        sigma_alt_lbl_->setText("高度σ:");
        undulation_lbl_->setText("异常高:");
        vel_n_lbl_->setText("北速:");
        vel_e_lbl_->setText("东速:");
        vel_ground_lbl_->setText("地速:");
        heading_lbl_->setText("航向:");
        pitch_lbl_->setText("俯仰:");
        heading_type_lbl_->setText("定向类型:");
        heading_len_lbl_->setText("基线长:");
        heading_sats_lbl_->setText("定向卫星:");
        sats_lbl_->setText("卫星:");
        diff_lbl_->setText("差分龄:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        htdop_lbl_->setText("HTDOP:");
        tdop_lbl_->setText("TDOP:");
        cutoff_lbl_->setText("截止角:");
    }
}

void GnssPanel::updateData(const VaporView::GnssData& gnss_data, quint64 timestamp_us)
{
    if (gnss_data.valid)
    {
        status_label_->setText(QString::fromStdString(gnss_data.position_status));
        status_label_->setProperty("data-valid", true);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);

        const QString formattedText = timestamp_us > 0
            ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp_us / 1000ULL), QTimeZone::UTC)
                  .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")
            : QStringLiteral("---");
        const QString rawText = timestamp_us > 0 ? QString::number(timestamp_us) + "us" : QStringLiteral("---");
        time_label_->setText(QString("%1\n%2").arg(formattedText, rawText));

        lat_label_->setText(QString::asprintf("%.8f°", gnss_data.latitude));
        lon_label_->setText(QString::asprintf("%.8f°", gnss_data.longitude));
        alt_label_->setText(QString::asprintf("%.3f m", gnss_data.altitude));
        sigma_lat_label_->setText(QString::asprintf("%.3f m", gnss_data.sigma_lat));
        sigma_lon_label_->setText(QString::asprintf("%.3f m", gnss_data.sigma_lon));
        sigma_alt_label_->setText(QString::asprintf("%.3f m", gnss_data.sigma_alt));
        undulation_label_->setText(QString::asprintf("%.3f m", gnss_data.undulation));
        vel_n_label_->setText(QString::asprintf("%.3f m/s", gnss_data.vel_north));
        vel_e_label_->setText(QString::asprintf("%.3f m/s", gnss_data.vel_east));
        vel_ground_label_->setText(QString::asprintf("%.3f m/s", gnss_data.vel_ground));
        heading_label_->setText(QString::asprintf("%.2f°", gnss_data.heading));
        pitch_label_->setText(QString::asprintf("%.2f°", gnss_data.heading_pitch));
        heading_type_label_->setText(QString::fromStdString(gnss_data.heading_type));
        heading_len_label_->setText(QString::asprintf("%.3f m", gnss_data.heading_length));
        heading_sats_label_->setText(QString("%1/%2").arg(gnss_data.heading_solnsvs).arg(gnss_data.heading_trackedsvs));
        sats_label_->setText(QString("%1/%2").arg(gnss_data.num_satellites_used).arg(gnss_data.num_satellites_tracked));
        diff_age_label_->setText(QString::asprintf("%.1f s", gnss_data.diff_age));
        gdop_label_->setText(QString::asprintf("%.2f", gnss_data.gdop));
        pdop_label_->setText(QString::asprintf("%.2f", gnss_data.pdop));
        hdop_label_->setText(QString::asprintf("%.2f", gnss_data.hdop));
        htdop_label_->setText(QString::asprintf("%.2f", gnss_data.htdop));
        tdop_label_->setText(QString::asprintf("%.2f", gnss_data.tdop));
        cutoff_label_->setText(QString::asprintf("%.1f°", gnss_data.elevation_cutoff));
    }
    else
    {
        status_label_->setText(QString::fromStdString(gnss_data.error_message));
        status_label_->setProperty("data-valid", false);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
        time_label_->setText(QStringLiteral("---\n---"));
    }
}

ImuPanel::ImuPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , acc_x_label_(nullptr)
    , acc_y_label_(nullptr)
    , acc_z_label_(nullptr)
    , gyr_x_label_(nullptr)
    , gyr_y_label_(nullptr)
    , gyr_z_label_(nullptr)
    , roll_label_(nullptr)
    , pitch_label_(nullptr)
    , yaw_label_(nullptr)
    , quat_w_label_(nullptr)
    , quat_x_label_(nullptr)
    , quat_y_label_(nullptr)
    , quat_z_label_(nullptr)
    , temp_label_(nullptr)
    , press_label_(nullptr)
    , source_label_(nullptr)
    , time_label_(nullptr)
    , pps_label_(nullptr)
    , source_lbl_(nullptr)
    , time_lbl_(nullptr)
    , pps_lbl_(nullptr)
    , accel_sep_(nullptr)
    , gyro_sep_(nullptr)
    , attitude_sep_(nullptr)
    , quat_sep_(nullptr)
    , env_sep_(nullptr)
    , temp_lbl_(nullptr)
    , press_lbl_(nullptr)
    , acc_x_lbl_(nullptr)
    , acc_y_lbl_(nullptr)
    , acc_z_lbl_(nullptr)
    , gyr_x_lbl_(nullptr)
    , gyr_y_lbl_(nullptr)
    , gyr_z_lbl_(nullptr)
    , roll_lbl_(nullptr)
    , pitch_lbl_(nullptr)
    , yaw_lbl_(nullptr)
    , quat_w_lbl_(nullptr)
    , quat_x_lbl_(nullptr)
    , quat_y_lbl_(nullptr)
    , quat_z_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void ImuPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setFixedHeight(24);
    mainLayout->addWidget(rate_label_);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(1);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(1);

    auto createRow = [](QGridLayout* grid, int row, QLabel*& lbl, QLabel*& valueLabel, QWidget* parent) {
        lbl = new QLabel(parent);
        lbl->setObjectName("fieldLabel");
        lbl->setMinimumHeight(22);
        valueLabel = new QLabel("---", parent);
        valueLabel->setObjectName("valueLabel");
        valueLabel->setMinimumHeight(22);
        grid->addWidget(lbl, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };

    auto createSeparator = [](QGridLayout* grid, int row, QLabel*& sep, QWidget* parent) {
        sep = new QLabel(parent);
        sep->setObjectName("separatorLabel");
        sep->setMinimumHeight(26);
        grid->addWidget(sep, row, 0, 1, 2);
    };

    createRow(leftLayout, 0, source_lbl_, source_label_, this);
    createRow(leftLayout, 1, time_lbl_, time_label_, this);
    createRow(leftLayout, 2, pps_lbl_, pps_label_, this);
    createSeparator(leftLayout, 3, accel_sep_, this);
    createRow(leftLayout, 4, acc_x_lbl_, acc_x_label_, this);
    createRow(leftLayout, 5, acc_y_lbl_, acc_y_label_, this);
    createRow(leftLayout, 6, acc_z_lbl_, acc_z_label_, this);
    createSeparator(leftLayout, 7, gyro_sep_, this);
    createRow(leftLayout, 8, gyr_x_lbl_, gyr_x_label_, this);
    createRow(leftLayout, 9, gyr_y_lbl_, gyr_y_label_, this);
    createRow(leftLayout, 10, gyr_z_lbl_, gyr_z_label_, this);

    createSeparator(rightLayout, 0, attitude_sep_, this);
    createRow(rightLayout, 1, roll_lbl_, roll_label_, this);
    createRow(rightLayout, 2, pitch_lbl_, pitch_label_, this);
    createRow(rightLayout, 3, yaw_lbl_, yaw_label_, this);
    createSeparator(rightLayout, 4, quat_sep_, this);
    createRow(rightLayout, 5, quat_w_lbl_, quat_w_label_, this);
    createRow(rightLayout, 6, quat_x_lbl_, quat_x_label_, this);
    createRow(rightLayout, 7, quat_y_lbl_, quat_y_label_, this);
    createRow(rightLayout, 8, quat_z_lbl_, quat_z_label_, this);

    if (time_label_)
    {
        time_label_->setWordWrap(true);
        time_label_->setMinimumHeight(40);
        time_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    leftLayout->setColumnStretch(1, 1);
    rightLayout->setColumnStretch(1, 1);

    colsLayout->addLayout(leftLayout, 1);
    colsLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(colsLayout);
    mainLayout->addStretch();
    setEnglish(false);
}

void ImuPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(QString::asprintf("%.1f Hz", hz));
    }
}

void ImuPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        source_lbl_->setText("Source:");
        time_lbl_->setText("Time:");
        pps_lbl_->setText("PPS:");
        accel_sep_->setText("— Accel —");
        gyro_sep_->setText("— Gyro —");
        attitude_sep_->setText("— Attitude —");
        quat_sep_->setText("— Quaternion —");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("Roll:");
        pitch_lbl_->setText("Pitch:");
        yaw_lbl_->setText("Yaw:");
        quat_w_lbl_->setText("W:");
        quat_x_lbl_->setText("X:");
        quat_y_lbl_->setText("Y:");
        quat_z_lbl_->setText("Z:");
    }
    else
    {
        source_lbl_->setText("数据源:");
        time_lbl_->setText("时间:");
        pps_lbl_->setText("PPS有效:");
        accel_sep_->setText("— 加速度 —");
        gyro_sep_->setText("— 陀螺仪 —");
        attitude_sep_->setText("— 姿态 —");
        quat_sep_->setText("— 四元数 —");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("横滚:");
        pitch_lbl_->setText("俯仰:");
        yaw_lbl_->setText("航向:");
        quat_w_lbl_->setText("W:");
        quat_x_lbl_->setText("X:");
        quat_y_lbl_->setText("Y:");
        quat_z_lbl_->setText("Z:");
    }
}

void ImuPanel::updateData(const VaporView::ImuData& imu_data, quint64 gnss_timestamp_us)
{
    if (imu_data.valid)
    {
        source_label_->setText(imuFrameTypeName(imu_data.frame_type));
        source_label_->setProperty("data-valid", true);
        source_label_->style()->unpolish(source_label_);
        source_label_->style()->polish(source_label_);
        quint64 imuTimestampUs = 0;
        if (imu_data.from_hi83 && imu_data.system_time_us > 0)
        {
            imuTimestampUs = static_cast<quint64>(imu_data.system_time_us);
        }
        else if (imu_data.system_time_ms > 0)
        {
            imuTimestampUs = static_cast<quint64>(imu_data.system_time_ms) * 1000ULL;
        }

        bool ppsValid = false;
        quint64 deltaUs = 0;
        if (imuTimestampUs > 0 && gnss_timestamp_us > 0)
        {
            deltaUs = (imuTimestampUs > gnss_timestamp_us) ? (imuTimestampUs - gnss_timestamp_us)
                                                           : (gnss_timestamp_us - imuTimestampUs);
            ppsValid = deltaUs <= kImuPpsSyncWindowUs;
        }

        const QString formattedText = (imuTimestampUs > 0 && ppsValid)
            ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(imuTimestampUs / 1000ULL), QTimeZone::UTC)
                  .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")
            : QStringLiteral("---");
        QString rawText = QStringLiteral("---");
        if (imu_data.from_hi83 && imu_data.system_time_us > 0)
        {
            rawText = QString::number(imu_data.system_time_us) + "us";
        }
        else if (imu_data.system_time_ms > 0)
        {
            rawText = QString::number(imu_data.system_time_ms) + "ms";
        }
        time_label_->setText(QString("%1\n%2").arg(formattedText, rawText));

        if (gnss_timestamp_us == 0 || imuTimestampUs == 0)
        {
            pps_label_->setText(is_english_ ? "Unknown" : "未知");
        }
        else if (ppsValid)
        {
            pps_label_->setText(is_english_
                ? QString("Valid (Δ%1 ms)").arg(QString::number(deltaUs / 1000ULL))
                : QString("有效 (差值%1 ms)").arg(QString::number(deltaUs / 1000ULL)));
        }
        else
        {
            pps_label_->setText(is_english_
                ? QString("Invalid (Δ%1 ms)").arg(QString::number(deltaUs / 1000ULL))
                : QString("无效 (差值%1 ms)").arg(QString::number(deltaUs / 1000ULL)));
        }

        acc_x_label_->setText(QString::asprintf("%.3f", imu_data.acceleration[0]));
        acc_y_label_->setText(QString::asprintf("%.3f", imu_data.acceleration[1]));
        acc_z_label_->setText(QString::asprintf("%.3f", imu_data.acceleration[2]));

        gyr_x_label_->setText(QString::asprintf("%.3f", imu_data.gyroscope[0]));
        gyr_y_label_->setText(QString::asprintf("%.3f", imu_data.gyroscope[1]));
        gyr_z_label_->setText(QString::asprintf("%.3f", imu_data.gyroscope[2]));

        roll_label_->setText(QString::asprintf("%.2f°", imu_data.rpy[0]));
        pitch_label_->setText(QString::asprintf("%.2f°", imu_data.rpy[1]));
        yaw_label_->setText(QString::asprintf("%.2f°", imu_data.rpy[2]));

        quat_w_label_->setText(QString::asprintf("%.4f", imu_data.quaternion[0]));
        quat_x_label_->setText(QString::asprintf("%.4f", imu_data.quaternion[1]));
        quat_y_label_->setText(QString::asprintf("%.4f", imu_data.quaternion[2]));
        quat_z_label_->setText(QString::asprintf("%.4f", imu_data.quaternion[3]));
    }
    else
    {
        source_label_->setText(QString::fromStdString(imu_data.error_message));
        source_label_->setProperty("data-valid", false);
        source_label_->style()->unpolish(source_label_);
        source_label_->style()->polish(source_label_);
        time_label_->setText(QStringLiteral("---\n---"));
        pps_label_->setText(is_english_ ? "Unknown" : "未知");
    }
}

PtbPanel::PtbPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , pressure_label_(nullptr)
    , status_label_(nullptr)
    , pressure_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void PtbPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 1, 6, 4);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(20);
    rate_label_->setMinimumWidth(kEnvironmentRateLabelMinWidth);

    auto *pressLayout = new QHBoxLayout();
    pressLayout->setSpacing(1);
    pressure_lbl_ = new QLabel(this);
    pressure_lbl_->setObjectName("fieldLabel");
    pressure_lbl_->setMinimumHeight(20);
    pressLayout->addWidget(pressure_lbl_);
    pressure_label_ = new QLabel("--- hPa", this);
    pressure_label_->setObjectName("highlightedValue");
    pressure_label_->setMinimumHeight(20);
    pressure_label_->setMinimumWidth(kPtbPressureValueMinWidth);
    pressLayout->addWidget(pressure_label_);
    pressLayout->addStretch();
    pressLayout->addWidget(rate_label_);
    layout->addLayout(pressLayout);

    setEnglish(false);
}

void PtbPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz)) ? QString::asprintf("%.1f Hz", hz) : QStringLiteral("-- Hz"));
    }
}

void PtbPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        pressure_lbl_->setText("Pressure:");
    }
    else
    {
        pressure_lbl_->setText("气压:");
    }
}

void PtbPanel::updateData(const VaporView::PtbData& ptb_data)
{
    if (ptb_data.valid)
    {
        pressure_label_->setText(QString::asprintf("%.2f hPa", ptb_data.pressure_hpa));
        pressure_label_->setProperty("data-valid", true);
        pressure_label_->style()->unpolish(pressure_label_);
        pressure_label_->style()->polish(pressure_label_);
    }
    else
    {
        pressure_label_->setText("--- hPa");
        pressure_label_->setProperty("data-valid", false);
        pressure_label_->style()->unpolish(pressure_label_);
        pressure_label_->style()->polish(pressure_label_);
    }
}

HmpPanel::HmpPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , humidity_label_(nullptr)
    , temperature_label_(nullptr)
    , status_label_(nullptr)
    , temp_lbl_(nullptr)
    , humidity_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void HmpPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 1, 6, 4);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(20);
    rate_label_->setMinimumWidth(kEnvironmentRateLabelMinWidth);

    auto *tempLayout = new QHBoxLayout();
    tempLayout->setSpacing(1);
    temp_lbl_ = new QLabel(this);
    temp_lbl_->setObjectName("fieldLabel");
    temp_lbl_->setMinimumHeight(20);
    tempLayout->addWidget(temp_lbl_);
    temperature_label_ = new QLabel("--- °C", this);
    temperature_label_->setObjectName("highlightedValue");
    temperature_label_->setMinimumHeight(20);
    temperature_label_->setMinimumWidth(kHmpValueMinWidth);
    tempLayout->addWidget(temperature_label_);
    tempLayout->addStretch();
    tempLayout->addWidget(rate_label_);
    layout->addLayout(tempLayout);

    auto *humidLayout = new QHBoxLayout();
    humidLayout->setSpacing(1);
    humidity_lbl_ = new QLabel(this);
    humidity_lbl_->setObjectName("fieldLabel");
    humidity_lbl_->setMinimumHeight(20);
    humidLayout->addWidget(humidity_lbl_);
    humidity_label_ = new QLabel("--- %RH", this);
    humidity_label_->setObjectName("highlightedValue");
    humidity_label_->setMinimumHeight(20);
    humidity_label_->setMinimumWidth(kHmpValueMinWidth);
    humidLayout->addWidget(humidity_label_);
    humidLayout->addStretch();
    layout->addLayout(humidLayout);

    setEnglish(false);
}

void HmpPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz)) ? QString::asprintf("%.1f Hz", hz) : QStringLiteral("-- Hz"));
    }
}

void HmpPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        temp_lbl_->setText("Temp:");
        humidity_lbl_->setText("Humidity:");
    }
    else
    {
        temp_lbl_->setText("温度:");
        humidity_lbl_->setText("湿度:");
    }
}

void HmpPanel::updateData(const VaporView::HmpData& hmp_data)
{
    if (hmp_data.valid)
    {
        temperature_label_->setText(QString::asprintf("%.1f °C", hmp_data.temperature));
        humidity_label_->setText(QString::asprintf("%.1f %%RH", hmp_data.humidity));
        temperature_label_->setProperty("data-valid", true);
        temperature_label_->style()->unpolish(temperature_label_);
        temperature_label_->style()->polish(temperature_label_);
        humidity_label_->setProperty("data-valid", true);
        humidity_label_->style()->unpolish(humidity_label_);
        humidity_label_->style()->polish(humidity_label_);
    }
    else
    {
        temperature_label_->setText("--- °C");
        humidity_label_->setText("--- %RH");
        temperature_label_->setProperty("data-valid", false);
        temperature_label_->style()->unpolish(temperature_label_);
        temperature_label_->style()->polish(temperature_label_);
        humidity_label_->setProperty("data-valid", false);
        humidity_label_->style()->unpolish(humidity_label_);
        humidity_label_->style()->polish(humidity_label_);
    }
}

LidarPanel::LidarPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , distance_label_(nullptr)
    , strength_label_(nullptr)
    , status_label_(nullptr)
    , distance_lbl_(nullptr)
    , strength_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void LidarPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 1, 6, 4);

    rate_label_ = new QLabel("0.0 Hz", this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setMinimumHeight(20);
    rate_label_->setMinimumWidth(kEnvironmentRateLabelMinWidth);

    auto *distanceLayout = new QHBoxLayout();
    distanceLayout->setSpacing(1);
    distance_lbl_ = new QLabel(this);
    distance_lbl_->setObjectName("fieldLabel");
    distance_lbl_->setMinimumHeight(20);
    distanceLayout->addWidget(distance_lbl_);
    distance_label_ = new QLabel("--- m", this);
    distance_label_->setObjectName("highlightedValue");
    distance_label_->setMinimumHeight(20);
    distance_label_->setMinimumWidth(kLidarDistanceValueMinWidth);
    distanceLayout->addWidget(distance_label_);
    distanceLayout->addStretch();
    distanceLayout->addWidget(rate_label_);
    layout->addLayout(distanceLayout);

    auto *strengthLayout = new QHBoxLayout();
    strengthLayout->setSpacing(1);
    strength_lbl_ = new QLabel(this);
    strength_lbl_->setObjectName("fieldLabel");
    strength_lbl_->setMinimumHeight(20);
    strengthLayout->addWidget(strength_lbl_);
    strength_label_ = new QLabel("---", this);
    strength_label_->setObjectName("highlightedValue");
    strength_label_->setMinimumHeight(20);
    strength_label_->setMinimumWidth(kLidarStrengthValueMinWidth);
    strengthLayout->addWidget(strength_label_);
    strengthLayout->addStretch();
    layout->addLayout(strengthLayout);

    setEnglish(false);
}

void LidarPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz)) ? QString::asprintf("%.1f Hz", hz) : QStringLiteral("-- Hz"));
    }
}

void LidarPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        distance_lbl_->setText("Distance:");
        strength_lbl_->setText("Strength:");
    }
    else
    {
        distance_lbl_->setText("距离:");
        strength_lbl_->setText("强度:");
    }
}

void LidarPanel::updateData(const VaporView::LidarData& lidar_data)
{
    if (lidar_data.valid)
    {
        distance_label_->setText(QString::asprintf("%.2f m", lidar_data.distance_m));
        strength_label_->setText(QString::number(lidar_data.signal_strength));
        distance_label_->setProperty("data-valid", true);
        strength_label_->setProperty("data-valid", true);
        distance_label_->style()->unpolish(distance_label_);
        distance_label_->style()->polish(distance_label_);
        strength_label_->style()->unpolish(strength_label_);
        strength_label_->style()->polish(strength_label_);
    }
    else
    {
        distance_label_->setText("--- m");
        strength_label_->setText("---");
        distance_label_->setProperty("data-valid", false);
        strength_label_->setProperty("data-valid", false);
        distance_label_->style()->unpolish(distance_label_);
        distance_label_->style()->polish(distance_label_);
        strength_label_->style()->unpolish(strength_label_);
        strength_label_->style()->polish(strength_label_);
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , central_widget_(nullptr)
    , main_layout_(nullptr)
    , window_border_top_(nullptr)
    , window_border_right_(nullptr)
    , window_border_bottom_(nullptr)
    , window_border_left_(nullptr)
    , custom_title_bar_(nullptr)
    , custom_logo_label_(nullptr)
    , custom_title_label_(nullptr)
    , title_menu_btn_(nullptr)
    , window_minimize_btn_(nullptr)
    , window_maximize_btn_(nullptr)
    , window_close_btn_(nullptr)
    , epsilon_panel_(nullptr)
    , gnss_panel_(nullptr)
    , imu_panel_(nullptr)
    , ptb_panel_(nullptr)
    , hmp_panel_(nullptr)
    , lidar_panel_(nullptr)
    , log_text_edit_(nullptr)
    , log_clear_btn_(nullptr)
    , status_label_(nullptr)
    , status_task_progress_bar_(nullptr)
    , status_task_spinner_label_(nullptr)
    , status_task_spinner_timer_(nullptr)
    , recording_status_card_(nullptr)
    , recording_status_title_lbl_(nullptr)
    , recording_status_label_(nullptr)
    , auto_detect_ports_btn_(nullptr)
    , epsilon_port_combo_(nullptr)
    , gnss_port_combo_(nullptr)
    , imu_port_combo_(nullptr)
    , ptb_port_combo_(nullptr)
    , hmp_port_combo_(nullptr)
    , lidar_port_combo_(nullptr)
    , epsilon_baud_combo_(nullptr)
    , gnss_baud_combo_(nullptr)
    , imu_baud_combo_(nullptr)
    , ptb_baud_combo_(nullptr)
    , hmp_baud_combo_(nullptr)
    , lidar_baud_combo_(nullptr)
    , connect_btn_(nullptr)
    , cancel_connect_btn_(nullptr)
    , disconnect_btn_(nullptr)
    , start_recording_btn_(nullptr)
    , pause_recording_btn_(nullptr)
    , stop_recording_btn_(nullptr)
    , refresh_ports_btn_(nullptr)
    , lang_action_(nullptr)
    , theme_toggle_action_(nullptr)
    , clear_log_action_(nullptr)
    , session_viewer_action_(nullptr)
    , epsilon_reconfigure_action_(nullptr)
    , epsilon_rtcm_port_action_(nullptr)
    , epsilon_packet_rates_action_(nullptr)
    , recording_directory_action_(nullptr)
    , exit_action_(nullptr)
    , about_action_(nullptr)
    , font_scale_group_(nullptr)
    , font_tiny_action_(nullptr)
    , font_extra_small_action_(nullptr)
    , font_small_action_(nullptr)
    , font_normal_action_(nullptr)
    , font_large_action_(nullptr)
    , font_extra_large_action_(nullptr)
    , data_menu_(nullptr)
    , devices_menu_(nullptr)
    , font_menu_(nullptr)
    , language_menu_(nullptr)
    , help_menu_(nullptr)
    , recording_rate_menu_(nullptr)
    , title_application_panel_(nullptr)
    , title_application_sub_panel_(nullptr)
    , config_group_(nullptr)
    , data_group_(nullptr)
    , log_side_panel_(nullptr)
    , log_group_(nullptr)
    , epsilon_group_(nullptr)
    , gnss_group_(nullptr)
    , imu_group_(nullptr)
    , ptb_group_(nullptr)
    , hmp_group_(nullptr)
    , env_group_(nullptr)
    , lidar_group_(nullptr)
    , epsilon_lbl_(nullptr)
    , gnss_lbl_(nullptr)
    , imu_lbl_(nullptr)
    , ptb_lbl_(nullptr)
    , hmp_lbl_(nullptr)
    , lidar_lbl_(nullptr)
    , data_telemetry_summary_card_(nullptr)
    , data_telemetry_summary_lbl_(nullptr)
    , log_inline_title_lbl_(nullptr)
    , epsilon_inline_title_lbl_(nullptr)
    , gnss_inline_title_lbl_(nullptr)
    , imu_inline_title_lbl_(nullptr)
    , env_inline_title_lbl_(nullptr)
    , env_lidar_status_icon_(nullptr)
    , env_ptb_status_icon_(nullptr)
    , env_hmp_status_icon_(nullptr)
    , config_inline_title_lbl_(nullptr)
    , global_rate_lbl_(nullptr)
    , epsilon_rate_lbl_(nullptr)
    , gnss_rate_lbl_(nullptr)
    , imu_rate_lbl_(nullptr)
    , ptb_rate_lbl_(nullptr)
    , hmp_rate_lbl_(nullptr)
    , lidar_rate_lbl_(nullptr)
    , data_source_mode_lbl_(nullptr)
    , sky_telemetry_port_lbl_(nullptr)
    , sky_telemetry_baud_lbl_(nullptr)
    , sky_telemetry_row_widget_(nullptr)
    , global_rate_combo_(nullptr)
    , epsilon_rate_combo_(nullptr)
    , gnss_rate_combo_(nullptr)
    , imu_rate_combo_(nullptr)
    , ptb_rate_combo_(nullptr)
    , hmp_rate_combo_(nullptr)
    , lidar_rate_combo_(nullptr)
    , data_source_mode_combo_(nullptr)
    , sky_telemetry_port_combo_(nullptr)
    , sky_telemetry_baud_combo_(nullptr)
    , imu_format_combo_(nullptr)
    , epsilon_packet_rates_btn_(nullptr)
    , sky_device_config_btn_(nullptr)
    , epsilon_remote_connect_btn_(nullptr)
    , epsilon_remote_disconnect_btn_(nullptr)
    , epsilon_remote_reconnect_btn_(nullptr)
    , epsilon_remote_buttons_widget_(nullptr)
    , ptb_remote_connect_btn_(nullptr)
    , ptb_remote_disconnect_btn_(nullptr)
    , ptb_remote_reconnect_btn_(nullptr)
    , ptb_remote_buttons_widget_(nullptr)
    , hmp_remote_connect_btn_(nullptr)
    , hmp_remote_disconnect_btn_(nullptr)
    , hmp_remote_reconnect_btn_(nullptr)
    , hmp_remote_buttons_widget_(nullptr)
    , lidar_remote_connect_btn_(nullptr)
    , lidar_remote_disconnect_btn_(nullptr)
    , lidar_remote_reconnect_btn_(nullptr)
    , lidar_remote_buttons_widget_(nullptr)
    , imu_apply_btn_(nullptr)
    , imu_hi91_btn_(nullptr)
    , imu_hi92_btn_(nullptr)
    , imu_baud_115200_btn_(nullptr)
    , imu_baud_921600_btn_(nullptr)
    , imu_rate_100_btn_(nullptr)
    , imu_rate_200_btn_(nullptr)
    , imu_rate_500_btn_(nullptr)
    , imu_rate_1000_btn_(nullptr)
    , epsilon_collector_(nullptr)
    , gnss_collector_(nullptr)
    , imu_collector_(nullptr)
    , ptb_collector_(nullptr)
    , hmp_collector_(nullptr)
    , lidar_collector_(nullptr)
    , refresh_timer_(nullptr)
    , is_english_(false)
    , has_inline_progress_log_(false)
    , connection_attempt_in_progress_(false)
    , port_detection_in_progress_(false)
    , epsilon_reconfigure_in_progress_(false)
    , is_connected_(false)
    , remote_sky_mode_(false)
    , remote_sky_online_(false)
    , remote_wave_stream_requested_(false)
    , remote_wave_stream_enable_pending_(false)
    , remote_wave_stream_auto_start_(true)
    , remote_recording_state_(0)
    , remote_last_status_ms_(0)
    , has_last_remote_recording_status_(false)
    , cancel_connection_requested_(false)
    , recording_thread_running_(false)
    , recording_paused_(false)
    , font_scale_percent_(100)
    , dark_theme_enabled_(false)
    , base_font_point_size_(0.0)
    , base_window_size_(kFallbackMainWindowWidth, kFallbackMainWindowHeight)
    , base_minimum_window_size_(800, 600)
    , normal_window_geometry_()
    , epsilon_sample_rate_(kDefaultEpsilonSampleRateHz)
    , gnss_sample_rate_(1)
    , imu_sample_rate_(200)
    , ptb_sample_rate_(kDefaultPtbSampleRateHz)
    , hmp_sample_rate_(kDefaultHmpSampleRateHz)
    , lidar_sample_rate_(kDefaultLidarSampleRateHz)
    , recording_export_rate_hz_(20)
    , imu_recording_rate_hz_(0)
    , waveform_recording_rate_hz_(0)
    , status_task_spinner_index_(0)
    , steady_clock_anchor_(std::chrono::steady_clock::now())
    , system_clock_anchor_(std::chrono::system_clock::now())
    , sensors_file_(nullptr)
    , raw_epsilon_file_(nullptr)
    , raw_ptb_file_(nullptr)
    , raw_hmp_file_(nullptr)
    , raw_lidar_file_(nullptr)
    , raw_tcp_wave_file_(nullptr)
    , event_log_file_(nullptr)
    , error_log_file_(nullptr)
    , recording_directory_()
    , session_directory_()
    , session_name_()
    , session_start_time_utc_()
    , session_start_time_us_(0)
    , sensors_filename_()
    , raw_epsilon_filename_()
    , raw_ptb_filename_()
    , raw_hmp_filename_()
    , raw_lidar_filename_()
    , raw_tcp_wave_filename_()
    , raw_dat_doc_filename_()
    , session_metadata_filename_()
    , event_log_filename_()
    , error_log_filename_()
    , device_config_filename_()
    , last_recording_session_name_()
    , last_recording_entry_count_(0)
    , last_recording_waveform_frame_count_(0)
    , last_raw_epsilon_record_count_(0)
    , last_raw_ptb_record_count_(0)
    , last_raw_hmp_record_count_(0)
    , last_raw_lidar_record_count_(0)
    , last_raw_tcp_wave_record_count_(0)
    , recording_entry_count_(0)
    , waveform_frame_count_(0)
    , waveform_file_count_(0)
    , raw_epsilon_record_count_(0)
    , raw_ptb_record_count_(0)
    , raw_hmp_record_count_(0)
    , raw_lidar_record_count_(0)
    , raw_tcp_wave_record_count_(0)
    , last_imu_record_timestamp_us_(0)
    , rtk_config_action_(nullptr)
    , rtk_config_dialog_(nullptr)
    , tcp_wave_panel_(nullptr)
    , session_viewer_window_(nullptr)
    , ground_telemetry_service_(nullptr)
    , sky_device_config_dialog_(nullptr)
{
    setWindowFlags(Qt::Window |
                   Qt::FramelessWindowHint |
                   Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint |
                   Qt::WindowCloseButtonHint);

    const double currentPointSize = qApp->font().pointSizeF();
    base_font_point_size_ = currentPointSize > 0.0 ? currentPointSize : 10.0;

    QSettings settings("VaporView", "MainWindow");
    font_scale_percent_ = settings.value(
        "font_scale_percent",
        VaporView::defaultFontScalePercentForScreen(this)).toInt();
    if (font_scale_percent_ < 70 || font_scale_percent_ > 150)
    {
        font_scale_percent_ = 100;
    }
    dark_theme_enabled_ = settings.value("dark_theme_enabled", false).toBool();
    recording_directory_ = settings.value("recording_directory", defaultRecordingDirectory()).toString();
    if (recording_directory_.isEmpty())
    {
        recording_directory_ = defaultRecordingDirectory();
    }
    recording_export_rate_hz_ = settings.value("recording_export_rate_hz", 20).toInt();
    if (recording_export_rate_hz_ < 1 || recording_export_rate_hz_ > 200)
    {
        recording_export_rate_hz_ = 20;
    }
    imu_recording_rate_hz_ = settings.value("imu_recording_rate_hz", 0).toInt();
    if (imu_recording_rate_hz_ < 0 || imu_recording_rate_hz_ > 1000)
    {
        imu_recording_rate_hz_ = 0;
    }
    waveform_recording_rate_hz_ = 0;

    loadModernStyleSheet();
    
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupCentralWidget();
    setupWindowBorderFrames();
    setupWindowResizeHandles();
    ground_telemetry_service_ = new VaporView::GroundTelemetryService(this);
    auto currentRemoteEvent = [this](quint64 generation) {
        return isRemoteSkyMode() &&
               ground_telemetry_service_ &&
               ground_telemetry_service_->linkGeneration() == generation;
    };
    auto currentOpenRemoteEvent = [this, currentRemoteEvent](quint64 generation) {
        return currentRemoteEvent(generation) &&
               ground_telemetry_service_ &&
               ground_telemetry_service_->isOpen();
    };
    auto dispatchRemoteUi = [this](auto handler) {
        if (QThread::currentThread() == thread())
        {
            handler();
            return;
        }
        QMetaObject::invokeMethod(this, std::move(handler), Qt::QueuedConnection);
    };
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::linkOpenChanged,
            this, [this, currentRemoteEvent, dispatchRemoteUi](bool open) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentRemoteEvent, generation, open]() {
                    if (!currentRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteLinkOpenChanged(open);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::logMessage,
            this, [this](const QString& message) { log(message); },
            Qt::QueuedConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::basicTelemetryUpdated,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::TelemetryBasic& telemetry) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, telemetry]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteBasicTelemetryUpdated(telemetry);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::waveformUpdated,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::DownsampledWaveform& waveform) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, waveform]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteWaveformUpdated(waveform);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::waveformFeatureUpdated,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::WaveformFeature& feature) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, feature]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteWaveformFeatureUpdated(feature);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::statusUpdated,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::TelemetryStatus& status) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, status]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteTelemetryStatusUpdated(status);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::commandAckReceived,
            this, [this, currentOpenRemoteEvent, dispatchRemoteUi](const VaporView::CommandAck& ack) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentOpenRemoteEvent, generation, ack]() {
                    if (!currentOpenRemoteEvent(generation))
                    {
                        return;
                    }
                    onRemoteCommandAckReceived(ack);
                });
            },
            Qt::DirectConnection);
    connect(ground_telemetry_service_, &VaporView::GroundTelemetryService::commandTimedOut,
            this, [this, currentRemoteEvent, dispatchRemoteUi](VaporView::CommandId commandId, quint16 commandSeq) {
                const quint64 generation = ground_telemetry_service_->linkGeneration();
                dispatchRemoteUi([this, currentRemoteEvent, generation, commandId, commandSeq]() {
                    if (!currentRemoteEvent(generation))
                    {
                        return;
                    }
                    if (isRemoteSkyMode() && commandId == VaporView::CommandId::RequestStatus && !remote_sky_online_ && status_label_)
                    {
                        status_label_->setText(is_english_ ? "Sky handshake timed out" : "天空端握手超时");
                        status_label_->setProperty("status", "disconnected");
                        status_label_->style()->unpolish(status_label_);
                        status_label_->style()->polish(status_label_);
                    }
                    if (commandId == VaporView::CommandId::SetPeakSearchRange)
                    {
                        remote_peak_search_commands_.remove(commandSeq);
                        if (tcp_wave_panel_)
                        {
                            tcp_wave_panel_->rejectRemotePeakSearchRange(is_english_ ? QStringLiteral("ACK timed out") : QStringLiteral("ACK 超时"));
                        }
                    }
                    else if (commandId == VaporView::CommandId::EnableWaveformStreaming ||
                             commandId == VaporView::CommandId::DisableWaveformStreaming)
                    {
                        remote_wave_stream_enable_pending_ = false;
                        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                                     remote_device_states_.value(VaporView::SkyDeviceId::WaveTcp,
                                                                                 VaporView::DeviceState::Disconnected));
                    }
                });
            },
            Qt::DirectConnection);
    loadRememberedInputState();
    bindRememberedInputState();

    const QSize fallbackMainWindowSize(kFallbackMainWindowWidth, kFallbackMainWindowHeight);
    base_minimum_window_size_ = VaporView::screenFractionSize(
        this,
        kMainWindowMinimumScreenFraction,
        fallbackMainWindowSize);
    base_window_size_ = VaporView::defaultWindowSizeWithinScreenFraction(
        this,
        fallbackMainWindowSize,
        kMainWindowDefaultScreenFraction,
        base_minimum_window_size_,
        fallbackMainWindowSize);
    resize(base_window_size_);
    setMinimumSize(base_minimum_window_size_);

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refresh_timer_->start(100);

    setEnglish(false);
    applyStyleConfiguration();
    VaporView::centerWindowOnScreen(this);
    rememberNormalWindowGeometry();

    updateRecordingStatusLabel();
    updateConnectionStatus(false);
    updateSourceModeUi();
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);

    if (session_viewer_window_)
    {
        delete session_viewer_window_;
        session_viewer_window_ = nullptr;
    }

    if (custom_title_bar_)
    {
        const auto buttons = custom_title_bar_->findChildren<QToolButton *>();
        for (QToolButton *button : buttons)
        {
            if (button)
            {
                button->setDefaultAction(nullptr);
                button->setMenu(nullptr);
            }
        }
        custom_title_bar_->removeEventFilter(this);
        if (custom_title_label_)
        {
            custom_title_label_->removeEventFilter(this);
        }
        if (custom_logo_label_)
        {
            custom_logo_label_->removeEventFilter(this);
        }
    }

    saveRememberedInputState();
    cancel_connection_requested_.store(true);
    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }
    if (epsilon_reconfigure_thread_.joinable())
    {
        epsilon_reconfigure_thread_.join();
    }
    stopRecording(false);
    stopAllCollectors();
}

bool MainWindow::shouldStartWindowMove(QObject *watched) const
{
    return watched == custom_title_bar_ ||
           watched == custom_logo_label_ ||
           watched == custom_title_label_ ||
           (watched && watched->objectName() == QStringLiteral("customTitleLogo"));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const bool titleMenuVisible =
        (title_application_panel_ && title_application_panel_->isVisible()) ||
        (title_application_sub_panel_ && title_application_sub_panel_->isVisible());
    if (titleMenuVisible)
    {
        if (event->type() == QEvent::ApplicationDeactivate ||
            event->type() == QEvent::WindowDeactivate)
        {
            if (title_application_panel_)
            {
                title_application_panel_->hide();
            }
            if (title_application_sub_panel_)
            {
                title_application_sub_panel_->hide();
            }
        }
        else if (event->type() == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint globalPos = mouseEvent->globalPosition().toPoint();
            auto containsGlobalPoint = [globalPos](const QWidget *widget) {
                return widget &&
                       widget->isVisible() &&
                       QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()).contains(globalPos);
            };

            const bool insideMenu =
                containsGlobalPoint(title_application_panel_) ||
                containsGlobalPoint(title_application_sub_panel_);
            const bool insideMenuButton = containsGlobalPoint(title_menu_btn_);

            if (!insideMenu && !insideMenuButton)
            {
                if (title_application_panel_)
                {
                    title_application_panel_->hide();
                }
                if (title_application_sub_panel_)
                {
                    title_application_sub_panel_->hide();
                }
            }
        }
    }

    if (shouldStartWindowMove(watched))
    {
        if (event->type() == QEvent::MouseButtonDblClick)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && !isFullScreen())
            {
                toggleWindowMaximized();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && windowHandle())
            {
                windowHandle()->startSystemMove();
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
    {
        if (!isWindowMaximizedForUi())
        {
            rememberNormalWindowGeometry();
        }
        updateWindowControlButtons();
        updateWindowBorderFrames();
        updateWindowResizeHandles();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!isWindowMaximizedForUi())
    {
        rememberNormalWindowGeometry();
    }
    updateWindowControlButtons();
    updateWindowBorderFrames();
    updateWindowResizeHandles();
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);

    auto *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_NCHITTEST || isFullScreen() || isWindowMaximizedForUi())
    {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    const int x = GET_X_LPARAM(msg->lParam);
    const int y = GET_Y_LPARAM(msg->lParam);
    const QPoint pos = mapFromGlobal(QPoint(x, y));
    const int borderWidth = scalePixels(8);
    const bool inWindowX = pos.x() >= 0 && pos.x() < width();
    const bool inWindowY = pos.y() >= 0 && pos.y() < height();
    const bool onLeft = inWindowY && pos.x() >= 0 && pos.x() < borderWidth;
    const bool onRight = inWindowY && pos.x() < width() && pos.x() >= width() - borderWidth;
    const bool onTop = inWindowX && pos.y() >= 0 && pos.y() < borderWidth;
    const bool onBottom = inWindowX && pos.y() < height() && pos.y() >= height() - borderWidth;

    if (onTop && onLeft)
    {
        *result = HTTOPLEFT;
        return true;
    }
    if (onTop && onRight)
    {
        *result = HTTOPRIGHT;
        return true;
    }
    if (onBottom && onLeft)
    {
        *result = HTBOTTOMLEFT;
        return true;
    }
    if (onBottom && onRight)
    {
        *result = HTBOTTOMRIGHT;
        return true;
    }
    if (onTop)
    {
        *result = HTTOP;
        return true;
    }
    if (onBottom)
    {
        *result = HTBOTTOM;
        return true;
    }
    if (onLeft)
    {
        *result = HTLEFT;
        return true;
    }
    if (onRight)
    {
        *result = HTRIGHT;
        return true;
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::loadModernStyleSheet()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList styleCandidates = {
        appDir.filePath("resources/modern_style.qss"),
        appDir.filePath("../resources/modern_style.qss"),
        appDir.filePath("../../resources/modern_style.qss"),
    };

    QString stylePath;
    QFile styleFile;
    for (const QString& candidate : styleCandidates)
    {
        styleFile.setFileName(QDir::cleanPath(candidate));
        if (styleFile.open(QFile::ReadOnly | QFile::Text))
        {
            stylePath = styleFile.fileName();
            break;
        }
    }
    
    if (styleFile.isOpen())
    {
        base_style_sheet_ = QString::fromUtf8(styleFile.readAll());
        styleFile.close();

        const QFileInfo styleInfo(stylePath);
        const QString resourceDir = styleInfo.absolutePath();
        const QString comboArrowPath = QDir(resourceDir).absoluteFilePath("combo_arrow_down.xpm").replace('\\', '/');
        const QString comboArrowUpPath = QDir(resourceDir).absoluteFilePath("combo_arrow_up.xpm").replace('\\', '/');
        base_style_sheet_.replace("url(combo_arrow_down.xpm)", QString("url(%1)").arg(comboArrowPath));
        base_style_sheet_.replace("url(combo_arrow_up.xpm)", QString("url(%1)").arg(comboArrowUpPath));
    }
    else
    {
        base_style_sheet_ =
            "* { font-family: \"Segoe UI\", \"Microsoft YaHei\", \"PingFang SC\", sans-serif; }"
            "QMainWindow { background-color: #FDFDFC; }"
            "QWidget#appCentralWidget, QWidget#mainCardsPane, QWidget#logSidePanel, QMainWindow#sessionViewerWindow, QWidget#sessionViewerCentralWidget, QWidget#sessionViewerViewport, QWidget#sessionViewerContentPane, QScrollArea#mainCardsScrollArea, QScrollArea#sessionViewerScrollArea, QWidget#mainCardsViewport, QScrollArea#mainCardsScrollArea > QWidget, QScrollArea#mainCardsScrollArea > QWidget > QWidget, QScrollArea#sessionViewerScrollArea > QWidget, QScrollArea#sessionViewerScrollArea > QWidget > QWidget, QSplitter#mainContentSplitter, QSplitter#sessionViewerContentSplitter { background-color: #FDFDFC; }"
            "QMenuBar { background-color: #FDFDFC; border-bottom: 1px solid #EAEAE9; padding: 4px 8px; }"
            "QMenuBar::item { background-color: transparent; padding: 6px 12px; border-radius: 4px; color: #000000; }"
            "QMenuBar::item:selected { background-color: #e3f2fd; color: #1976d2; }"
            "QMenu { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 6px; padding: 8px 0px; }"
            "QMenu::item { padding: 8px 32px 8px 16px; color: #000000; }"
            "QMenu::item:selected { background-color: #e3f2fd; color: #1976d2; }"
            "QToolBar { background-color: #FDFDFC; border-bottom: 1px solid #EAEAE9; padding: 8px 12px; spacing: 8px; }"
            "QToolBar QToolButton { background-color: transparent; border: none; border-radius: 6px; padding: 10px 14px; color: #000000; font-size: 15px; }"
            "QToolBar QToolButton:hover { background-color: #F8F8F7; }"
            "QToolBar QToolButton:disabled { color: #000000; }"
            "QStatusBar { background-color: #FDFDFC; border-top: 1px solid #EAEAE9; padding: 4px 12px; color: #000000; font-size: 14px; }"
            "QGroupBox { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-top: 40px solid #FDFDFC; border-radius: 8px; margin-top: 0px; padding: 8px 8px 8px 8px; font-size: 15px; font-weight: bold; color: #000000; }"
            "QGroupBox#sensorGroupBox { margin-top: 0px; background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 8px; padding: 0px 0px 0px 0px; }"
            "QFrame#logPanelFrame { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 8px; }"
            "QFrame#recordingStatusCard { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 8px; }"
            "QFrame#recordingStatusCard QWidget#sectionTitleBar { background-color: #FDFDFC; border: none; border-bottom: 1px solid #EAEAE9; border-top-left-radius: 7px; border-top-right-radius: 7px; }"
            "QFrame#recordingStatusCard QLabel#sectionTitleLabel { background-color: transparent; border: none; }"
            "QWidget#recordingStatusBody { background-color: #FDFDFC; border: none; border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; }"
            "QLabel#recordingStatusLabel { background-color: transparent; border: none; color: #000000; font-size: 14px; font-weight: 600; }"
            "QGroupBox::title { subcontrol-origin: border; subcontrol-position: top left; left: 12px; top: -30px; padding: 0px 2px; background-color: transparent; border: none; border-radius: 0px; color: #000000; }"
            "QDialog#rtkConfigDialog, QWidget#rtkConfigViewport, QWidget#rtkConfigContent, QScrollArea#rtkConfigScrollArea { background-color: #FDFDFC; }"
            "QDialog#rtkConfigDialog QGroupBox#rtkCardGroup { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 8px; margin-top: 0px; padding: 0px; color: #000000; }"
            "QDialog#rtkConfigDialog QGroupBox#rtkCardGroup::title { color: transparent; }"
            "QDialog#rtkConfigDialog QWidget#sectionTitleBar { background-color: #FDFDFC; border: none; border-bottom: 1px solid #EAEAE9; border-top-left-radius: 7px; border-top-right-radius: 7px; }"
            "QDialog#rtkConfigDialog QLabel#sectionTitleLabel { background-color: transparent; border: none; color: #000000; }"
            "QWidget#sectionTitleBar { background-color: #FDFDFC; border-bottom: 1px solid #EAEAE9; border-top-left-radius: 7px; border-top-right-radius: 7px; min-height: 40px; max-height: 40px; }"
            "QWidget#sectionTitleBar QLabel { background-color: transparent; border: none; }"
            "QLabel { color: #000000; background-color: transparent; border: none; }"
            "QLabel#rateLabel { color: #000000; font-size: 13px; font-weight: bold; font-family: \"Cascadia Mono\", \"Consolas\", \"Courier New\", monospace; }"
            "QLabel#fieldLabel { color: #000000; font-size: 14px; font-weight: 600; }"
            "QLabel#separatorLabel { color: #000000; font-size: 14px; font-weight: bold; }"
            "QLabel#rtkStatusLabel { color: #000000; font-weight: bold; }"
            "QLabel#sectionTitleLabel { background-color: #FDFDFC; border: none; border-bottom: 1px solid #EAEAE9; border-radius: 0px; color: #000000; font-size: 16px; font-weight: bold; padding: 0px 10px; min-height: 36px; max-height: 36px; }"
            "QWidget#sectionTitleBar QLabel#sectionTitleLabel { background-color: transparent; border: none; padding: 0px 10px; min-height: 36px; max-height: 36px; }"
            "QFrame#epsilonSectionCard { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 4px; }"
            "QLabel#epsilonSectionLabel { color: #000000; background-color: #F8F8F7; border: none; border-right: 1px solid #EAEAE9; font-size: 14px; font-weight: 700; padding: 2px; }"
            "QLabel#valueLabel, QLabel#highlightedValue { font-family: \"Cascadia Mono\", \"Consolas\", \"Courier New\", monospace; }"
            "QComboBox { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 6px; padding: 4px 10px; min-height: 26px; max-height: 26px; color: #000000; font-size: 14px; }"
            "QComboBox:hover { border-color: #bdbdbd; }"
            "QComboBox:focus { border-color: #1976d2; border-width: 1px; }"
            "QComboBox:disabled { background-color: #F8F8F7; color: #000000; }"
            "QComboBox QAbstractItemView { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 6px; selection-background-color: #e3f2fd; selection-color: #1976d2; padding: 4px; outline: none; }"
            "QLineEdit { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 6px; padding: 4px 10px; min-height: 26px; max-height: 26px; color: #000000; font-size: 14px; }"
            "QLineEdit:hover { border-color: #bdbdbd; }"
            "QLineEdit:focus { border-color: #1976d2; border-width: 1px; }"
            "QLineEdit:disabled { background-color: #F8F8F7; color: #000000; }"
            "QSpinBox, QDoubleSpinBox { background-color: #FDFDFC; border: 1px solid #EAEAE9; border-radius: 6px; padding: 4px 28px 4px 10px; min-height: 26px; max-height: 26px; color: #000000; font-size: 14px; }"
            "QSpinBox:hover, QDoubleSpinBox:hover { border-color: #bdbdbd; }"
            "QSpinBox:focus, QDoubleSpinBox:focus { border-color: #1976d2; border-width: 1px; }"
            "QSpinBox:disabled, QDoubleSpinBox:disabled { background-color: #F8F8F7; color: #000000; }"
            "QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 20px; border: none; background-color: transparent; subcontrol-origin: border; }"
            "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 6px; }"
            "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 6px; }"
            "QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background-color: #F8F8F7; }"
            "QSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0px; height: 0px; margin-right: 6px; border-left: 4px solid transparent; border-right: 4px solid transparent; }"
            "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { border-bottom: 5px solid #757575; }"
            "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { border-top: 5px solid #757575; }"
            "QTextEdit { background-color: #FDFDFC; color: #000000; border: 1px solid #EAEAE9; border-radius: 6px; padding: 10px; font-family: \"Consolas\", \"Monaco\", \"Courier New\", monospace; font-size: 13px; }"
            "QTextEdit#logTextEdit { background-color: transparent; border: none; border-radius: 0px; }"
            "QWidget#logTextViewport { background-color: transparent; border: none; }"
            "QScrollBar:vertical { background-color: #F8F8F7; width: 12px; border-radius: 6px; }"
            "QScrollBar::handle:vertical { background-color: #bdbdbd; min-height: 30px; border-radius: 6px; margin: 2px; }"
            "QScrollBar::handle:vertical:hover { background-color: #9e9e9e; }"
            "QScrollBar:horizontal { background-color: #F8F8F7; height: 12px; border-radius: 6px; }"
            "QScrollBar::handle:horizontal { background-color: #bdbdbd; min-width: 30px; border-radius: 6px; margin: 2px; }"
            "QScrollBar::handle:horizontal:hover { background-color: #9e9e9e; }"
            "QScrollArea#mainCardsScrollArea QScrollBar:horizontal, QScrollArea#mainCardsScrollArea QScrollBar:vertical { background-color: #FCFCFB; }"
            "QSplitter::handle { background-color: transparent; }"
            "QSplitter#mainContentSplitter::handle:horizontal { width: 8px; background-color: transparent; }"
            "QWidget#mainCardResizeHandle { min-height: 3px; max-height: 3px; background-color: transparent; }"
            "QSplitter#mainContentSplitter::handle:horizontal:hover { background-color: rgba(25, 118, 210, 0.18); }"
            "QWidget#mainCardResizeHandle:hover { background-color: rgba(25, 118, 210, 0.18); }"
            "QSplitter#mainContentSplitter::handle:horizontal:pressed { background-color: rgba(25, 118, 210, 0.28); }"
            "QWidget#mainCardResizeHandle[dragging=\"true\"] { background-color: rgba(25, 118, 210, 0.28); }"
            "QSplitter::handle:horizontal { width: 0px; }"
            "QSplitter::handle:vertical { height: 0px; }"
            "QSplitter#mainContentSplitter::handle:horizontal { width: 8px; background-color: #FCFCFB; }"
            "QSplitter#mainContentSplitter::handle:horizontal:hover { background-color: rgba(25, 118, 210, 0.18); }"
            "QSplitter#mainContentSplitter::handle:horizontal:pressed { background-color: rgba(25, 118, 210, 0.28); }"
            "QPushButton { background-color: #1976d2; color: #ffffff; border: none; border-radius: 6px; padding: 0px 18px 2px 18px; font-size: 15px; font-weight: 500; min-height: 36px; max-height: 36px; }"
            "QPushButton:hover { background-color: #1565c0; }"
            "QPushButton:pressed { background-color: #0d47a1; }"
            "QPushButton:disabled { background-color: #bdbdbd; color: #ffffff; }"
            "QPushButton#compactTcpButton { padding: 0px 14px 2px 14px; min-height: 36px; max-height: 36px; font-size: 14px; }"
            "QPushButton#compactTcpStartButton { padding: 0px 14px 2px 14px; min-height: 36px; max-height: 36px; font-size: 14px; }"
            "QToolTip { background-color: #323232; color: #ffffff; border: none; border-radius: 6px; padding: 6px 10px; font-size: 13px; }";
    }

    applyStyleConfiguration();
}

QString MainWindow::themedStyleSheet() const
{
    return dark_theme_enabled_
        ? base_style_sheet_ + darkThemeStyleSheet() + customTitleBarStyleSheet(true)
        : base_style_sheet_ + customTitleBarStyleSheet(false);
}

QString MainWindow::scaledStyleSheet(const QString& styleSheet) const
{
    const QRegularExpression pixelRegex(R"((\d+)px)");
    QString scaled = styleSheet;
    QRegularExpressionMatchIterator it = pixelRegex.globalMatch(styleSheet);
    struct Replacement
    {
        qsizetype start;
        qsizetype length;
        QString text;
    };
    QList<Replacement> replacements;

    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const int originalPx = match.captured(1).toInt();
        const int scaledPx = originalPx == 0 ? 0 : std::max(1, scalePixels(originalPx));
        replacements.append({match.capturedStart(0), match.capturedLength(0), QString("%1px").arg(scaledPx)});
    }

    for (auto replacementIt = replacements.crbegin(); replacementIt != replacements.crend(); ++replacementIt)
    {
        scaled.replace(replacementIt->start, replacementIt->length, replacementIt->text);
    }

    return scaled;
}

int MainWindow::scalePixels(int pixels) const
{
    return static_cast<int>(std::lround(pixels * font_scale_percent_ / 100.0));
}

void MainWindow::applyScaledUiMetrics()
{
    auto applyWidgetMetrics = [this](QWidget *widget) {
        if (!widget)
        {
            return;
        }

        const int minimumWidth = widget->minimumWidth();
        if (minimumWidth > 0)
        {
            rememberBaseMetric(widget, kBaseMinWidthProperty, minimumWidth);
            widget->setMinimumWidth(std::max(1, scalePixels(widget->property(kBaseMinWidthProperty).toInt())));
        }

        const int minimumHeight = widget->minimumHeight();
        if (minimumHeight > 0)
        {
            rememberBaseMetric(widget, kBaseMinHeightProperty, minimumHeight);
            widget->setMinimumHeight(std::max(1, scalePixels(widget->property(kBaseMinHeightProperty).toInt())));
        }

        const int maximumWidth = widget->maximumWidth();
        if (maximumWidth > 0 && maximumWidth < QWIDGETSIZE_MAX)
        {
            rememberBaseMetric(widget, kBaseMaxWidthProperty, maximumWidth);
            widget->setMaximumWidth(std::max(1, scalePixels(widget->property(kBaseMaxWidthProperty).toInt())));
        }

        const int maximumHeight = widget->maximumHeight();
        if (maximumHeight > 0 && maximumHeight < QWIDGETSIZE_MAX)
        {
            rememberBaseMetric(widget, kBaseMaxHeightProperty, maximumHeight);
            widget->setMaximumHeight(std::max(1, scalePixels(widget->property(kBaseMaxHeightProperty).toInt())));
        }
    };

    applyWidgetMetrics(this);
    for (QWidget *widget : findChildren<QWidget*>())
    {
        applyWidgetMetrics(widget);
    }

    auto applyLayoutMetrics = [this](QLayout *layout) {
        if (!layout)
        {
            return;
        }

        if (layout->spacing() >= 0)
        {
            rememberBaseMetric(layout, kBaseSpacingProperty, layout->spacing());
            layout->setSpacing(std::max(0, scalePixels(layout->property(kBaseSpacingProperty).toInt())));
        }

        const QMargins margins = layout->contentsMargins();
        rememberBaseMetric(layout, kBaseMarginsLeftProperty, margins.left());
        rememberBaseMetric(layout, kBaseMarginsTopProperty, margins.top());
        rememberBaseMetric(layout, kBaseMarginsRightProperty, margins.right());
        rememberBaseMetric(layout, kBaseMarginsBottomProperty, margins.bottom());
        layout->setContentsMargins(
            std::max(0, scalePixels(layout->property(kBaseMarginsLeftProperty).toInt())),
            std::max(0, scalePixels(layout->property(kBaseMarginsTopProperty).toInt())),
            std::max(0, scalePixels(layout->property(kBaseMarginsRightProperty).toInt())),
            std::max(0, scalePixels(layout->property(kBaseMarginsBottomProperty).toInt()))
        );
    };

    if (layout())
    {
        applyLayoutMetrics(layout());
    }

    for (QLayout *layout : findChildren<QLayout*>())
    {
        applyLayoutMetrics(layout);
    }
}

void MainWindow::applyStyleConfiguration()
{
    QFont appFont = qApp->font();
    appFont.setPointSizeF(base_font_point_size_ * font_scale_percent_ / 100.0);
    qApp->setPalette(themedPalette(dark_theme_enabled_));
    qApp->setFont(appFont);
    qApp->setStyleSheet(scaledStyleSheet(themedStyleSheet()));
    setWindowsTitleBarDark(this, dark_theme_enabled_);
    applyScaledUiMetrics();
    updateCustomTitleBarStyle();

    if (!isFullScreen() && !isMaximized())
    {
        const QSize targetSize = size().expandedTo(minimumSize()).expandedTo(minimumSizeHint());
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }
}

void MainWindow::setFontScale(int percent)
{
    if (percent < 70 || percent > 150 || font_scale_percent_ == percent)
    {
        return;
    }

    QSize targetSize = size();
    if (!isFullScreen() && !isMaximized())
    {
        targetSize = QSize(
            std::max(1, static_cast<int>(std::lround(base_window_size_.width() * percent / 100.0))),
            std::max(1, static_cast<int>(std::lround(base_window_size_.height() * percent / 100.0)))
        );
    }

    font_scale_percent_ = percent;
    discardTitleApplicationMenuPanel();
    applyStyleConfiguration();
    updateSourceModeUi();
    if (!isFullScreen() && !isMaximized())
    {
        targetSize = targetSize.expandedTo(minimumSize()).expandedTo(minimumSizeHint());
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }
    if (rtk_config_dialog_)
    {
        rtk_config_dialog_->setFontScale(font_scale_percent_);
    }
    if (sky_device_config_dialog_)
    {
        sky_device_config_dialog_->setFontScale(font_scale_percent_);
    }

    QSettings settings("VaporView", "MainWindow");
    settings.setValue("font_scale_percent", font_scale_percent_);
}

void MainWindow::rebuildRecordingRateMenu()
{
    if (!recording_rate_menu_)
    {
        return;
    }

    recording_rate_menu_->clear();
    auto buildSubmenu = [this](QMenu *parent,
                               const QString& title,
                               const QVector<int>& standardRates,
                               int currentRate,
                               bool allowUnlimited,
                               const QString& unlimitedEnglish,
                               const QString& unlimitedChinese,
                               auto setter) {
        QMenu *submenu = parent->addMenu(title);

        auto addAction = [this, submenu, currentRate, &setter](int rate, const QString& text) {
            QAction *action = submenu->addAction(text);
            action->setCheckable(true);
            action->setChecked(rate == currentRate);
            connect(action, &QAction::triggered, this, [this, rate, setter]() {
                setter(rate);
            });
        };

        if (allowUnlimited)
        {
            addAction(0, is_english_ ? unlimitedEnglish : unlimitedChinese);
        }

        if (currentRate > 0 && !standardRates.contains(currentRate))
        {
            addAction(currentRate,
                      QStringLiteral("%1 Hz%2").arg(currentRate).arg(is_english_ ? QStringLiteral(" (Custom)") : QStringLiteral("（当前）")));
        }

        for (int rate : standardRates)
        {
            addAction(rate, QStringLiteral("%1 Hz").arg(rate));
        }
    };

    buildSubmenu(recording_rate_menu_,
                 is_english_ ? QStringLiteral("TCP Wave Raw") : QStringLiteral("TCP波形原始帧"),
                 {},
                 0,
                 true,
                 QStringLiteral("Complete TCP frames"),
                 QStringLiteral("完整TCP帧"),
                 [this](int) { setWaveformRecordingRateHz(0); });

    buildSubmenu(recording_rate_menu_,
                 is_english_ ? QStringLiteral("EPSILON Raw") : QStringLiteral("EPSILON原始帧"),
                 {},
                 0,
                 true,
                 QStringLiteral("Verified FDILink frames"),
                 QStringLiteral("已校验FDILink原始帧"),
                 [this](int) { setImuRecordingRateHz(0); });

    buildSubmenu(recording_rate_menu_,
                 is_english_ ? QStringLiteral("Other devices") : QStringLiteral("其余设备"),
                 QVector<int>{1, 2, 5, 10, 20, 50, 100, 200},
                 std::clamp(recording_export_rate_hz_, 1, 200),
                 false,
                 QString(),
                 QString(),
                 [this](int rate) { setRecordingExportRateHz(rate); });
}

void MainWindow::setRecordingExportRateHz(int rate, bool should_log)
{
    const int normalizedRate = std::clamp(rate, 1, 200);
    const bool changed = recording_export_rate_hz_ != normalizedRate;
    recording_export_rate_hz_ = normalizedRate;
    rebuildRecordingRateMenu();
    discardTitleApplicationMenuPanel();
    saveRememberedInputState();

    if (changed && should_log)
    {
        log(QString(is_english_ ? "Other-devices recording rate set to %1 Hz" : "其余设备记录频率已设置为 %1 Hz").arg(recording_export_rate_hz_));
    }
}

void MainWindow::setImuRecordingRateHz(int rate, bool should_log)
{
    Q_UNUSED(rate);
    const int normalizedRate = 0;
    const bool changed = imu_recording_rate_hz_ != normalizedRate;
    imu_recording_rate_hz_ = normalizedRate;
    rebuildRecordingRateMenu();
    discardTitleApplicationMenuPanel();
    saveRememberedInputState();

    if (changed && should_log)
    {
        log(is_english_
            ? QStringLiteral("EPSILON raw recording keeps full verified FDILink frames")
            : QStringLiteral("EPSILON 原始记录固定保存完整已校验 FDILink 帧"));
    }
}

void MainWindow::setWaveformRecordingRateHz(int rate, bool should_log)
{
    Q_UNUSED(rate);
    const int normalizedRate = 0;
    const bool changed = waveform_recording_rate_hz_ != normalizedRate;
    waveform_recording_rate_hz_ = normalizedRate;
    rebuildRecordingRateMenu();
    discardTitleApplicationMenuPanel();
    saveRememberedInputState();

    if (changed && should_log)
    {
        log(is_english_
            ? QStringLiteral("TCP wave raw recording keeps every complete TCP frame")
            : QStringLiteral("TCP 波形原始记录固定保存每组完整 TCP 帧"));
    }
}

void MainWindow::loadRememberedInputState()
{
    QSettings settings("VaporView", "MainWindow");

    auto loadCombo = [&settings](QComboBox *combo, const QString& key, const QString& fallbackKey = QString()) {
        if (!combo)
        {
            return;
        }
        QVariant fallback = combo->currentText();
        if (!fallbackKey.isEmpty())
        {
            fallback = settings.value(fallbackKey, fallback);
        }
        applyComboText(combo, settings.value(key, fallback).toString());
    };

    loadCombo(epsilon_port_combo_, QStringLiteral("serial/epsilon_port"), QStringLiteral("serial/gnss_port"));
    loadCombo(ptb_port_combo_, QStringLiteral("serial/ptb_port"));
    loadCombo(hmp_port_combo_, QStringLiteral("serial/hmp_port"));
    loadCombo(lidar_port_combo_, QStringLiteral("serial/lidar_port"));

    loadCombo(epsilon_baud_combo_, QStringLiteral("serial/epsilon_baud"), QStringLiteral("serial/gnss_baud"));
    loadCombo(ptb_baud_combo_, QStringLiteral("serial/ptb_baud"));
    loadCombo(hmp_baud_combo_, QStringLiteral("serial/hmp_baud"));
    loadCombo(lidar_baud_combo_, QStringLiteral("serial/lidar_baud"));

    loadCombo(global_rate_combo_, QStringLiteral("rate/global"));
    loadCombo(epsilon_rate_combo_, QStringLiteral("rate/epsilon"), QStringLiteral("rate/gnss"));
    loadCombo(ptb_rate_combo_, QStringLiteral("rate/ptb"));
    loadCombo(hmp_rate_combo_, QStringLiteral("rate/hmp"));
    loadCombo(lidar_rate_combo_, QStringLiteral("rate/lidar"));
    if (data_source_mode_combo_)
    {
        const QString value = settings.value(
            QStringLiteral("source/mode"),
            sourceModeStorageValue(data_source_mode_combo_->currentIndex())).toString();
        const int index = sourceModeIndexFromStoredValue(value);
        if (index >= 0)
        {
            const QSignalBlocker blocker(data_source_mode_combo_);
            data_source_mode_combo_->setCurrentIndex(index);
        }
    }
    loadCombo(sky_telemetry_port_combo_, QStringLiteral("telemetry/sky_port"));
    loadCombo(sky_telemetry_baud_combo_, QStringLiteral("telemetry/sky_baud"));

    recording_export_rate_hz_ = std::clamp(settings.value("recording_export_rate_hz", recording_export_rate_hz_).toInt(), 1, 200);
    imu_recording_rate_hz_ = std::clamp(settings.value("imu_recording_rate_hz", imu_recording_rate_hz_).toInt(), 0, 1000);
    waveform_recording_rate_hz_ = 0;
    rebuildRecordingRateMenu();

    const QStringList args = QCoreApplication::arguments();
    const int sourceIndex = args.indexOf(QStringLiteral("--source"));
    if (sourceIndex >= 0 && sourceIndex + 1 < args.size() &&
        args.at(sourceIndex + 1).compare(QStringLiteral("remote"), Qt::CaseInsensitive) == 0 &&
        data_source_mode_combo_)
    {
        data_source_mode_combo_->setCurrentIndex(1);
    }
    const int portIndex = args.indexOf(QStringLiteral("--telemetry-port"));
    if (portIndex >= 0 && portIndex + 1 < args.size() && sky_telemetry_port_combo_)
    {
        sky_telemetry_port_combo_->setEditText(args.at(portIndex + 1));
    }
    const int baudIndex = args.indexOf(QStringLiteral("--telemetry-baud"));
    if (baudIndex >= 0 && baudIndex + 1 < args.size() && sky_telemetry_baud_combo_)
    {
        sky_telemetry_baud_combo_->setCurrentText(args.at(baudIndex + 1));
    }
    onDataSourceModeChanged(data_source_mode_combo_ ? data_source_mode_combo_->currentIndex() : 0);
}

void MainWindow::saveRememberedInputState() const
{
    QSettings settings("VaporView", "MainWindow");

    auto saveCombo = [&settings](const QString& key, QComboBox *combo) {
        if (combo)
        {
            settings.setValue(key, combo->currentText());
        }
    };

    saveCombo(QStringLiteral("serial/epsilon_port"), epsilon_port_combo_);
    saveCombo(QStringLiteral("serial/ptb_port"), ptb_port_combo_);
    saveCombo(QStringLiteral("serial/hmp_port"), hmp_port_combo_);
    saveCombo(QStringLiteral("serial/lidar_port"), lidar_port_combo_);

    saveCombo(QStringLiteral("serial/epsilon_baud"), epsilon_baud_combo_);
    saveCombo(QStringLiteral("serial/ptb_baud"), ptb_baud_combo_);
    saveCombo(QStringLiteral("serial/hmp_baud"), hmp_baud_combo_);
    saveCombo(QStringLiteral("serial/lidar_baud"), lidar_baud_combo_);

    saveCombo(QStringLiteral("rate/global"), global_rate_combo_);
    saveCombo(QStringLiteral("rate/epsilon"), epsilon_rate_combo_);
    saveCombo(QStringLiteral("rate/ptb"), ptb_rate_combo_);
    saveCombo(QStringLiteral("rate/hmp"), hmp_rate_combo_);
    saveCombo(QStringLiteral("rate/lidar"), lidar_rate_combo_);
    if (data_source_mode_combo_)
    {
        settings.setValue(QStringLiteral("source/mode"), sourceModeStorageValue(data_source_mode_combo_->currentIndex()));
    }
    saveCombo(QStringLiteral("telemetry/sky_port"), sky_telemetry_port_combo_);
    saveCombo(QStringLiteral("telemetry/sky_baud"), sky_telemetry_baud_combo_);
    settings.setValue("recording_export_rate_hz", recording_export_rate_hz_);
    settings.setValue("imu_recording_rate_hz", imu_recording_rate_hz_);
    settings.setValue("waveform_recording_rate_hz", waveform_recording_rate_hz_);
}

void MainWindow::bindRememberedInputState()
{
    auto bindCombo = [this](QComboBox *combo) {
        if (!combo)
        {
            return;
        }
        connect(combo, &QComboBox::currentTextChanged, this, [this](const QString&) {
            saveRememberedInputState();
        });
    };

    bindCombo(epsilon_port_combo_);
    bindCombo(ptb_port_combo_);
    bindCombo(hmp_port_combo_);
    bindCombo(lidar_port_combo_);
    bindCombo(epsilon_baud_combo_);
    bindCombo(ptb_baud_combo_);
    bindCombo(hmp_baud_combo_);
    bindCombo(lidar_baud_combo_);
    bindCombo(global_rate_combo_);
    bindCombo(epsilon_rate_combo_);
    bindCombo(ptb_rate_combo_);
    bindCombo(hmp_rate_combo_);
    bindCombo(lidar_rate_combo_);
    bindCombo(data_source_mode_combo_);
    bindCombo(sky_telemetry_port_combo_);
    bindCombo(sky_telemetry_baud_combo_);

}

bool MainWindow::isRemoteSkyMode() const
{
    return remote_sky_mode_;
}

void MainWindow::onDataSourceModeChanged(int index)
{
    remote_sky_mode_ = index == 1;
    clearRemoteSkyDataUi();
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->setRemoteSkyMode(remote_sky_mode_);
    }
    updateSourceModeUi();
    updateRecordingActionStates();
}

void MainWindow::updateSourceModeUi()
{
    const bool remote = isRemoteSkyMode();
    const bool localInputsEnabled = !remote && !is_connected_ &&
        !connection_attempt_in_progress_ && !port_detection_in_progress_ && !epsilon_reconfigure_in_progress_;
    const QList<QWidget*> localWidgets = {epsilon_port_combo_, epsilon_baud_combo_, ptb_port_combo_, ptb_baud_combo_,
                                          hmp_port_combo_, hmp_baud_combo_, lidar_port_combo_, lidar_baud_combo_,
                                          epsilon_packet_rates_btn_, ptb_rate_combo_, hmp_rate_combo_, lidar_rate_combo_};
    for (QWidget *widget : localWidgets)
    {
        if (widget)
        {
            widget->setEnabled(localInputsEnabled);
        }
    }
    if (auto_detect_ports_btn_)
    {
        auto_detect_ports_btn_->setEnabled(!remote && !is_connected_ && !connection_attempt_in_progress_);
    }
    if (sky_telemetry_port_combo_) sky_telemetry_port_combo_->setEnabled(remote && !is_connected_ && !connection_attempt_in_progress_);
    if (sky_telemetry_baud_combo_) sky_telemetry_baud_combo_->setEnabled(remote && !is_connected_ && !connection_attempt_in_progress_);
    if (sky_telemetry_row_widget_) sky_telemetry_row_widget_->setVisible(remote);
    if (sky_telemetry_port_lbl_) sky_telemetry_port_lbl_->setVisible(remote);
    if (sky_telemetry_port_combo_) sky_telemetry_port_combo_->setVisible(remote);
    if (sky_telemetry_baud_lbl_) sky_telemetry_baud_lbl_->setVisible(remote);
    if (sky_telemetry_baud_combo_) sky_telemetry_baud_combo_->setVisible(remote);
    if (sky_device_config_btn_) sky_device_config_btn_->setEnabled(remote && ground_telemetry_service_ && ground_telemetry_service_->isOpen());
    setRemoteDeviceButtonsEnabled(remote && ground_telemetry_service_ && ground_telemetry_service_->isOpen());
    updateRemoteTelemetrySummaryLabel();
    updateConfigCardHeightForSourceMode();
}

int MainWindow::scaledConfiguredHeight(QWidget *widget, int baseHeight) const
{
    if (widget && widget->property(kBaseMinHeightProperty).isValid())
    {
        return scalePixels(baseHeight);
    }
    return baseHeight;
}

void MainWindow::updateConfigCardHeightForSourceMode()
{
    if (!config_group_)
    {
        return;
    }

    const bool remote = isRemoteSkyMode();
    int minimumHeight = scaledConfiguredHeight(config_group_,
                                              remote ? kConfigRemoteCardMinHeight : kConfigCardMinHeight);
    if (remote && data_telemetry_summary_card_ && data_telemetry_summary_lbl_)
    {
        int summaryHeight = data_telemetry_summary_lbl_->sizeHint().height();
        if (QLayout *summaryLayout = data_telemetry_summary_card_->layout())
        {
            const QMargins margins = summaryLayout->contentsMargins();
            summaryHeight += margins.top() + margins.bottom();
        }
        summaryHeight = std::max(summaryHeight, scaledConfiguredHeight(data_telemetry_summary_lbl_, kMainPageInputHeight));
        data_telemetry_summary_card_->setMinimumHeight(summaryHeight);
        minimumHeight += std::max(0, summaryHeight - scaledConfiguredHeight(config_group_, kConfigRowsHeight));
    }

    const int previousMinimum = config_group_->property(kMainCardMinimumHeightProperty).toInt();
    const bool minimumChanged = previousMinimum != minimumHeight;
    config_group_->setProperty(kMainCardMinimumHeightProperty, minimumHeight);
    config_group_->setMinimumHeight(minimumHeight);
    if (minimumChanged || config_group_->height() < minimumHeight)
    {
        config_group_->setFixedHeight(minimumHeight);
    }
}

void MainWindow::clearRemoteSkyDataUi()
{
    remote_device_states_.clear();
    remote_last_data_ms_.clear();
    remote_packet_arrivals_ms_.clear();
    remote_last_status_ms_ = 0;
    remote_sky_online_ = false;
    remote_wave_stream_requested_ = false;
    remote_wave_stream_enable_pending_ = false;
    remote_wave_stream_auto_start_ = true;
    remote_status_ = VaporView::TelemetryStatus();
    remote_recording_state_ = 0;

    current_epsilon_ = VaporView::EpsilonData();
    current_gnss_ = VaporView::GnssData();
    current_imu_ = VaporView::ImuData();
    current_ptb_ = VaporView::PtbData();
    current_hmp_ = VaporView::HmpData();
    current_lidar_ = VaporView::LidarData();

    current_ptb_.error_message = remoteNoDataText(is_english_).toStdString();
    current_hmp_.error_message = remoteNoDataText(is_english_).toStdString();
    current_lidar_.error_message = remoteNoDataText(is_english_).toStdString();
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
    }

    if (epsilon_panel_) epsilon_panel_->updateRate(0.0);
    if (ptb_panel_) ptb_panel_->updateRate(0.0);
    if (hmp_panel_) hmp_panel_->updateRate(0.0);
    if (lidar_panel_) lidar_panel_->updateRate(0.0);
    if (epsilon_panel_) epsilon_panel_->updateData(current_epsilon_);
    if (gnss_panel_) gnss_panel_->updateData(current_gnss_, 0);
    if (imu_panel_) imu_panel_->updateData(current_imu_, 0);
    if (ptb_panel_) ptb_panel_->updateData(current_ptb_);
    if (hmp_panel_) hmp_panel_->updateData(current_hmp_);
    if (lidar_panel_) lidar_panel_->updateData(current_lidar_);
    updateEnvironmentStatusIcons(false, false, false);
    updateSourceModeUi();
    updateRemoteTelemetrySummaryLabel();
    updateRecordingStatusLabel();
}

void MainWindow::markRemoteSkyLinkClosed()
{
    remote_last_status_ms_ = 0;
    remote_sky_online_ = false;
    remote_wave_stream_requested_ = false;
    remote_wave_stream_enable_pending_ = false;
    remote_packet_arrivals_ms_.clear();
    remote_recording_state_ = 0;
    remote_status_.recording_state = 0;
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
    }
    refreshRemoteSkyDataUi();
    updateSourceModeUi();
    updateRecordingStatusLabel();
}

bool MainWindow::remoteDeviceDataValid(VaporView::SkyDeviceId device, qint64 timeout_ms) const
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        return false;
    }
    if (remote_last_status_ms_ <= 0 || QDateTime::currentMSecsSinceEpoch() - remote_last_status_ms_ > 3000)
    {
        return false;
    }
    if (remote_device_states_.value(device, VaporView::DeviceState::Disconnected) != VaporView::DeviceState::Connected)
    {
        return false;
    }
    const qint64 lastDataMs = remote_last_data_ms_.value(device, 0);
    return lastDataMs > 0 && QDateTime::currentMSecsSinceEpoch() - lastDataMs <= timeout_ms;
}

QString MainWindow::remoteDeviceInvalidText(VaporView::SkyDeviceId device, qint64 timeout_ms) const
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        return remoteDisconnectedText(is_english_);
    }
    if (remote_last_status_ms_ <= 0)
    {
        return remoteNoDataText(is_english_);
    }
    if (QDateTime::currentMSecsSinceEpoch() - remote_last_status_ms_ > 3000)
    {
        return remoteStaleText(is_english_);
    }
    if (remote_device_states_.value(device, VaporView::DeviceState::Disconnected) != VaporView::DeviceState::Connected)
    {
        return remoteDisconnectedText(is_english_);
    }
    const qint64 lastDataMs = remote_last_data_ms_.value(device, 0);
    if (lastDataMs <= 0)
    {
        return remoteNoDataText(is_english_);
    }
    if (QDateTime::currentMSecsSinceEpoch() - lastDataMs > timeout_ms)
    {
        return remoteStaleText(is_english_);
    }
    return remoteNoDataText(is_english_);
}

void MainWindow::noteRemotePacket(VaporView::MsgType type)
{
    const int key = static_cast<int>(type);
    QVector<qint64>& arrivals = remote_packet_arrivals_ms_[key];
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    arrivals.push_back(now);
    while (!arrivals.isEmpty() && now - arrivals.front() > 5000)
    {
        arrivals.removeFirst();
    }
}

double MainWindow::remotePacketRate(VaporView::MsgType type) const
{
    const QVector<qint64> arrivals = remote_packet_arrivals_ms_.value(static_cast<int>(type));
    if (arrivals.size() < 2)
    {
        return 0.0;
    }
    const qint64 elapsedMs = arrivals.back() - arrivals.front();
    if (elapsedMs <= 0)
    {
        return 0.0;
    }
    return (arrivals.size() - 1) * 1000.0 / static_cast<double>(elapsedMs);
}

QString MainWindow::remoteTelemetrySummaryText() const
{
    if (!isRemoteSkyMode())
    {
        return QString();
    }
    const bool connected = ground_telemetry_service_ && ground_telemetry_service_->isOpen();

    auto hasText = [this, connected](VaporView::SkyDeviceId device, qint64 timeoutMs) {
        return connected && remoteDeviceDataValid(device, timeoutMs)
            ? (is_english_ ? QStringLiteral("data") : QStringLiteral("有数据"))
            : (is_english_ ? QStringLiteral("none") : QStringLiteral("无数据"));
    };

    const QString actualWaveRate = (connected && remote_status_.wave_tcp_actual_rate_hz > 0.0f)
        ? QStringLiteral("%1 Hz").arg(remote_status_.wave_tcp_actual_rate_hz, 0, 'f', 1)
        : QStringLiteral("-- Hz");
    const double rxBps = connected ? ground_telemetry_service_->receiveBitsPerSecond() : 0.0;
    const double txBps = connected ? ground_telemetry_service_->transmitBitsPerSecond() : 0.0;

    const QString textColor = dark_theme_enabled_ ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
    const QString borderColor = dark_theme_enabled_ ? QStringLiteral("#202020") : QStringLiteral("#EAEAE9");
    const QString cardBg = dark_theme_enabled_ ? QStringLiteral("#121212") : QStringLiteral("#FDFDFC");
    const QString titleBg = dark_theme_enabled_ ? QStringLiteral("#121212") : QStringLiteral("#F8F8F7");

    auto rowHtml = [&](const QString& label, const QString& value, int labelWidth, int valueWidth) {
        return QStringLiteral("<tr>"
                              "<td width=\"%1\" style=\"width:%1px;padding:1px 6px;white-space:nowrap;color:%5;font-weight:600;\">%2</td>"
                              "<td width=\"%3\" style=\"width:%3px;min-width:%3px;padding:1px 6px;white-space:nowrap;color:%5;font-weight:600;"
                              "font-family:'Cascadia Mono','Consolas','Courier New',monospace;\">%4</td>"
                              "</tr>")
            .arg(scalePixels(labelWidth))
            .arg(label.toHtmlEscaped())
            .arg(scalePixels(valueWidth))
            .arg(value.toHtmlEscaped())
            .arg(textColor);
    };

    auto verticalTitleHtml = [this](const QString& title) {
        QStringList parts;
        if (is_english_)
        {
            const QString normalized = QString(title).replace(QStringLiteral(" / "), QStringLiteral(" "));
            const QStringList words = normalized.split(QChar(' '), Qt::SkipEmptyParts);
            parts.reserve(words.size());
            for (const QString& word : words)
            {
                parts << word.toHtmlEscaped();
            }
        }
        else
        {
            parts.reserve(title.size());
            for (const QChar ch : title)
            {
                if (!ch.isSpace())
                {
                    parts << QString(ch).toHtmlEscaped();
                }
            }
        }
        return parts.join(QStringLiteral("<br/>"));
    };

    auto sectionHtml = [&](const QString& title, const QString& rows, int width) {
        return QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" width=\"%1\" "
                              "style=\"width:%1px;border:1px solid %2;background:%3;\">"
                              "<tr>"
                              "<td width=\"%6\" valign=\"middle\" align=\"center\" "
                              "style=\"width:%6px;background:%4;border-right:1px solid %2;"
                              "padding:2px;color:%5;font-weight:700;\">%7</td>"
                              "<td valign=\"top\"><table cellspacing=\"0\" cellpadding=\"0\">%8</table></td>"
                              "</tr></table>")
            .arg(scalePixels(width))
            .arg(borderColor, cardBg, titleBg, textColor)
            .arg(scalePixels(kTelemetrySummaryTitleColumnWidth))
            .arg(verticalTitleHtml(title), rows);
    };

    QString rateRows;
    QString linkRows;
    QString deviceRows;
    if (is_english_)
    {
        rateRows += rowHtml(QStringLiteral("Basic:"), QStringLiteral("%1 Hz").arg(remotePacketRate(VaporView::MsgType::TelemetryBasic), 0, 'f', 1), kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        rateRows += rowHtml(QStringLiteral("Feature:"), QStringLiteral("%1 Hz").arg(remotePacketRate(VaporView::MsgType::WaveformFeature), 0, 'f', 1), kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        rateRows += rowHtml(QStringLiteral("Wave packets:"), QStringLiteral("%1 Hz").arg(remotePacketRate(VaporView::MsgType::WaveformDownsampled), 0, 'f', 1), kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        rateRows += rowHtml(QStringLiteral("Status:"), QStringLiteral("%1 Hz").arg(remotePacketRate(VaporView::MsgType::TelemetryStatus), 0, 'f', 1), kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        rateRows += rowHtml(QStringLiteral("Wave TCP actual:"), actualWaveRate, kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        linkRows += rowHtml(QStringLiteral("Sky->Ground:"), formatBitRate(rxBps), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        linkRows += rowHtml(QStringLiteral("Ground->Sky:"), formatBitRate(txBps), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        linkRows += rowHtml(QStringLiteral("Total:"), formatBitRate(rxBps + txBps), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("EPSILON:"), hasText(VaporView::SkyDeviceId::Epsilon, 2000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("PTB:"), hasText(VaporView::SkyDeviceId::Ptb, 3000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("HMP:"), hasText(VaporView::SkyDeviceId::Hmp, 3000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("Lidar:"), hasText(VaporView::SkyDeviceId::Lidar, 2000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("Wave:"), hasText(VaporView::SkyDeviceId::WaveTcp, 3000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
    }
    else
    {
        rateRows += rowHtml(QStringLiteral("基础:"), QStringLiteral("%1 Hz").arg(remotePacketRate(VaporView::MsgType::TelemetryBasic), 0, 'f', 1), kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        rateRows += rowHtml(QStringLiteral("特征值:"), QStringLiteral("%1 Hz").arg(remotePacketRate(VaporView::MsgType::WaveformFeature), 0, 'f', 1), kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        rateRows += rowHtml(QStringLiteral("波形包:"), QStringLiteral("%1 Hz").arg(remotePacketRate(VaporView::MsgType::WaveformDownsampled), 0, 'f', 1), kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        rateRows += rowHtml(QStringLiteral("状态:"), QStringLiteral("%1 Hz").arg(remotePacketRate(VaporView::MsgType::TelemetryStatus), 0, 'f', 1), kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        rateRows += rowHtml(QStringLiteral("波形 TCP 实际:"), actualWaveRate, kTelemetrySummaryRateLabelWidth, kTelemetrySummaryRateValueWidth);
        linkRows += rowHtml(QStringLiteral("天空→地面:"), formatBitRate(rxBps), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        linkRows += rowHtml(QStringLiteral("地面→天空:"), formatBitRate(txBps), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        linkRows += rowHtml(QStringLiteral("合计:"), formatBitRate(rxBps + txBps), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("EPSILON:"), hasText(VaporView::SkyDeviceId::Epsilon, 2000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("PTB:"), hasText(VaporView::SkyDeviceId::Ptb, 3000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("HMP:"), hasText(VaporView::SkyDeviceId::Hmp, 3000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("Lidar:"), hasText(VaporView::SkyDeviceId::Lidar, 2000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
        deviceRows += rowHtml(QStringLiteral("波形:"), hasText(VaporView::SkyDeviceId::WaveTcp, 3000), kTelemetrySummaryInfoLabelWidth, kTelemetrySummaryInfoValueWidth);
    }

    const QString rateTitle = is_english_ ? QStringLiteral("Telemetry packet rates") : QStringLiteral("数传数据包频率");
    const QString linkTitle = is_english_ ? QStringLiteral("Link rate") : QStringLiteral("链路速率");
    const QString deviceTitle = is_english_ ? QStringLiteral("Device data") : QStringLiteral("设备数据");
    const QString rateCard = sectionHtml(rateTitle, rateRows, kTelemetrySummaryRateCardWidth);
    const QString linkCard = sectionHtml(linkTitle, linkRows, kTelemetrySummaryInfoCardWidth);
    const QString deviceCard = sectionHtml(deviceTitle, deviceRows, kTelemetrySummaryInfoCardWidth);
    return QStringLiteral("<table cellspacing=\"%4\" cellpadding=\"0\">"
                          "<tr><td valign=\"top\" rowspan=\"2\">%1</td><td valign=\"top\">%2</td></tr>"
                          "<tr><td valign=\"top\">%3</td></tr>"
                          "</table>")
        .arg(rateCard, linkCard, deviceCard)
        .arg(scalePixels(kTelemetrySummaryGapWidth));
}

void MainWindow::updateRemoteTelemetrySummaryLabel()
{
    if (!data_telemetry_summary_card_ || !data_telemetry_summary_lbl_)
    {
        return;
    }
    const QString text = remoteTelemetrySummaryText();
    data_telemetry_summary_card_->setVisible(isRemoteSkyMode());
    data_telemetry_summary_lbl_->setText(text);
    data_telemetry_summary_lbl_->setToolTip(text);
}

void MainWindow::updateEnvironmentStatusIcons(bool lidarValid, bool ptbValid, bool hmpValid)
{
    auto updateIcon = [this](QLabel *label, bool valid, const QString& zhName, const QString& enName) {
        if (!label)
        {
            return;
        }
        const QIcon icon = createLucideIcon(valid ? QStringLiteral("check") : QStringLiteral("circle-x"),
                                            valid ? kToolbarGreen : kToolbarRed);
        label->setPixmap(icon.pixmap(QSize(kEnvStatusIconSize, kEnvStatusIconSize)));
        const QString name = is_english_ ? enName : zhName;
        const QString state = valid
            ? (is_english_ ? QStringLiteral("valid") : QStringLiteral("有效"))
            : (is_english_ ? QStringLiteral("no data") : QStringLiteral("无数据"));
        label->setToolTip(is_english_
            ? QStringLiteral("%1: %2").arg(name, state)
            : QStringLiteral("%1：%2").arg(name, state));
    };

    updateIcon(env_lidar_status_icon_, lidarValid, QStringLiteral("Lidar"), QStringLiteral("Lidar"));
    updateIcon(env_ptb_status_icon_, ptbValid, QStringLiteral("PTB"), QStringLiteral("PTB"));
    updateIcon(env_hmp_status_icon_, hmpValid, QStringLiteral("HMP"), QStringLiteral("HMP"));
}

void MainWindow::refreshRemoteSkyDataUi()
{
    VaporView::EpsilonData epsilon = current_epsilon_;
    VaporView::PtbData ptb = current_ptb_;
    VaporView::HmpData hmp = current_hmp_;
    VaporView::LidarData lidar = current_lidar_;

    const bool epsilonValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Epsilon, 2000);
    const bool ptbValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Ptb, 3000);
    const bool hmpValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Hmp, 3000);
    const bool lidarValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Lidar, 2000);

    epsilon.valid = epsilonValid;
    ptb.valid = ptbValid;
    hmp.valid = hmpValid;
    lidar.valid = lidarValid;
    if (!ptbValid) ptb.error_message = remoteDeviceInvalidText(VaporView::SkyDeviceId::Ptb, 3000).toStdString();
    if (!hmpValid) hmp.error_message = remoteDeviceInvalidText(VaporView::SkyDeviceId::Hmp, 3000).toStdString();
    if (!lidarValid) lidar.error_message = remoteDeviceInvalidText(VaporView::SkyDeviceId::Lidar, 2000).toStdString();

    const double basicRate = (remote_last_status_ms_ > 0 &&
                              QDateTime::currentMSecsSinceEpoch() - remote_last_status_ms_ <= 3000)
        ? remote_status_.telemetry_basic_rate_hz
        : 0.0;
    if (status_label_ && ground_telemetry_service_ && ground_telemetry_service_->isOpen())
    {
        if (remote_last_status_ms_ <= 0)
        {
            status_label_->setText(is_english_ ? "Remote Sky connected, waiting for status" : "天空端数传已连接，等待状态");
            status_label_->setProperty("status", "connecting");
            status_label_->style()->unpolish(status_label_);
            status_label_->style()->polish(status_label_);
        }
        else if (QDateTime::currentMSecsSinceEpoch() - remote_last_status_ms_ > 3000)
        {
            status_label_->setText(is_english_ ? "Remote Sky status timeout" : "天空端状态超时");
            status_label_->setProperty("status", "disconnected");
            status_label_->style()->unpolish(status_label_);
            status_label_->style()->polish(status_label_);
        }
    }
    if (epsilon_panel_) epsilon_panel_->updateRate(epsilonValid ? basicRate : 0.0);
    if (ptb_panel_) ptb_panel_->updateRate(ptbValid ? basicRate : 0.0);
    if (hmp_panel_) hmp_panel_->updateRate(hmpValid ? basicRate : 0.0);
    if (lidar_panel_) lidar_panel_->updateRate(lidarValid ? basicRate : 0.0);
    if (epsilon_panel_) epsilon_panel_->updateData(epsilon);
    if (ptb_panel_) ptb_panel_->updateData(ptb);
    if (hmp_panel_) hmp_panel_->updateData(hmp);
    if (lidar_panel_) lidar_panel_->updateData(lidar);
    updateEnvironmentStatusIcons(lidarValid, ptbValid, hmpValid);
    updateRemoteTelemetrySummaryLabel();
}

QPushButton *MainWindow::createRemoteDeviceButton(const QString& text, VaporView::CommandId command, VaporView::SkyDeviceId device)
{
    auto *button = new QPushButton(text, this);
    button->setFixedHeight(kMainPageInputHeight);
    button->setMinimumWidth(60);
    const QString action = command == VaporView::CommandId::ConnectDevice
        ? QStringLiteral("连接")
        : command == VaporView::CommandId::DisconnectDevice
            ? QStringLiteral("断开")
            : QStringLiteral("重连");
    button->setToolTip(QStringLiteral("请求天空端%1 %2").arg(action, skyDeviceDisplayName(device)));
    connect(button, &QPushButton::clicked, this, [this, command, device]() {
        sendRemoteDeviceCommand(command, device);
    });
    return button;
}

void MainWindow::setRemoteDeviceButtonsEnabled(bool enabled)
{
    for (QPushButton *button : {epsilon_remote_connect_btn_, epsilon_remote_disconnect_btn_, epsilon_remote_reconnect_btn_,
                               ptb_remote_connect_btn_, ptb_remote_disconnect_btn_, ptb_remote_reconnect_btn_,
                               hmp_remote_connect_btn_, hmp_remote_disconnect_btn_, hmp_remote_reconnect_btn_,
                               lidar_remote_connect_btn_, lidar_remote_disconnect_btn_, lidar_remote_reconnect_btn_})
    {
        if (button)
        {
            button->setEnabled(enabled);
        }
    }
    for (QWidget *widget : {epsilon_remote_buttons_widget_, ptb_remote_buttons_widget_, hmp_remote_buttons_widget_, lidar_remote_buttons_widget_})
    {
        if (widget)
        {
            widget->setVisible(isRemoteSkyMode());
        }
    }
}

void MainWindow::sendRemoteDeviceCommand(VaporView::CommandId command, VaporView::SkyDeviceId device)
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        log(is_english_ ? "Remote Sky telemetry link is not connected" : "天空端数传链路未连接");
        return;
    }
    ground_telemetry_service_->sendDeviceCommand(command, device);
}

void MainWindow::sendRemotePeakSearchRange(quint32 startIndex, quint32 endIndex)
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        log(is_english_ ? "Remote Sky telemetry link is not connected" : "天空端数传链路未连接");
        if (tcp_wave_panel_)
        {
            tcp_wave_panel_->rejectRemotePeakSearchRange(is_english_ ? QStringLiteral("link is not connected") : QStringLiteral("数传链路未连接"));
        }
        return;
    }
    const quint16 seq = ground_telemetry_service_->sendPeakSearchRangeCommand(startIndex, endIndex);
    VaporView::PeakSearchRange range;
    range.start_index = startIndex;
    range.end_index = endIndex;
    remote_peak_search_commands_.insert(seq, range);
    log(QString(is_english_
            ? "Peak search range sent to sky: [%1, %2), seq=%3"
            : "峰值搜索区间已下发到天空端：[%1, %2)，序号=%3")
            .arg(startIndex)
            .arg(endIndex == 0 ? QStringLiteral("end") : QString::number(endIndex))
            .arg(seq));
}

void MainWindow::updateRemoteDeviceButtonText(VaporView::SkyDeviceId device, VaporView::DeviceState state)
{
    QPushButton *connectButton = nullptr;
    QPushButton *disconnectButton = nullptr;
    QPushButton *reconnectButton = nullptr;
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        connectButton = epsilon_remote_connect_btn_; disconnectButton = epsilon_remote_disconnect_btn_; reconnectButton = epsilon_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::Ptb:
        connectButton = ptb_remote_connect_btn_; disconnectButton = ptb_remote_disconnect_btn_; reconnectButton = ptb_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::Hmp:
        connectButton = hmp_remote_connect_btn_; disconnectButton = hmp_remote_disconnect_btn_; reconnectButton = hmp_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::Lidar:
        connectButton = lidar_remote_connect_btn_; disconnectButton = lidar_remote_disconnect_btn_; reconnectButton = lidar_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::WaveTcp:
        if (tcp_wave_panel_)
        {
            tcp_wave_panel_->setRemoteWaveTcpState(remote_wave_stream_requested_ && state == VaporView::DeviceState::Connected
                ? VaporView::DeviceState::Connected
                : VaporView::DeviceState::Disconnected);
        }
        return;
    case VaporView::SkyDeviceId::All:
        return;
    }
    const QString stateText = VaporView::deviceStateName(state);
    if (connectButton) connectButton->setToolTip(QStringLiteral("请求天空端连接 %1（当前：%2）").arg(skyDeviceDisplayName(device), stateText));
    if (disconnectButton) disconnectButton->setToolTip(QStringLiteral("请求天空端断开 %1（当前：%2）").arg(skyDeviceDisplayName(device), stateText));
    if (reconnectButton) reconnectButton->setToolTip(QStringLiteral("请求天空端重连 %1（当前：%2）").arg(skyDeviceDisplayName(device), stateText));
}

void MainWindow::setImuFormatSelection(const QString& format)
{
    applyComboText(imu_format_combo_, format);
}

void MainWindow::setImuBaudSelection(int baud)
{
    applyComboText(imu_baud_combo_, QString::number(baud));
}

void MainWindow::setImuRateSelection(int rate)
{
    applyComboText(imu_rate_combo_, QString::number(rate));
    imu_sample_rate_ = parseRate(imu_rate_combo_->currentText());
}

bool MainWindow::restartImuCollector(const std::shared_ptr<VaporView::ImuCollector>& collector, const QString& port, int baud, int rate)
{
    if (!collector)
    {
        return false;
    }

    collector->setSampleRate(rate);
    if (!collector->start(port.toStdString(), VaporView::SerialConfig::N81(baud)))
    {
        log(QString(is_english_ ? "[IMU] Failed to reopen IMU port: %1" : "[IMU] 重新打开 IMU 串口失败: %1")
                .arg(QString::fromStdString(collector->getLastError())));
        return false;
    }
    collector->setOutputMessageType(imu_format_combo_ ? imu_format_combo_->currentText().toStdString() : std::string("HI91"));
    if (!collector->checkDeviceResponse())
    {
        log(is_english_ ? "[IMU] No response after reopening IMU port" : "[IMU] 重新打开 IMU 串口后未收到设备响应");
        collector->stop();
        return false;
    }
    if (!collector->startStreaming())
    {
        log(is_english_ ? "[IMU] Failed to restart IMU data stream" : "[IMU] 重新启动 IMU 数据流失败");
        collector->stop();
        return false;
    }
    log(QString(is_english_ ? "[IMU] Reconnected at %1 baud, %2 Hz, %3" : "[IMU] 已按 %1 波特率、%2 Hz、%3 重新连接")
            .arg(baud)
            .arg(rate)
            .arg(imu_format_combo_ ? imu_format_combo_->currentText() : QStringLiteral("HI91")));
    return true;
}

bool MainWindow::applyImuDeviceProfile(const QString& requestedFormat, int requestedBaud, int requestedRate)
{
    if (connection_attempt_in_progress_ || port_detection_in_progress_)
    {
        return false;
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString port = imu_port_combo_ ? imu_port_combo_->currentText().trimmed() : QString();
    if (port.isEmpty() || port == selectText)
    {
        log(is_english_ ? "Select an IMU serial port first" : "请先选择 IMU 串口");
        return false;
    }

    bool baudOk = false;
    const int currentBaud = (imu_baud_combo_ ? imu_baud_combo_->currentText() : QStringLiteral("921600")).toInt(&baudOk);
    const int effectiveCurrentBaud = baudOk && currentBaud > 0 ? currentBaud : 921600;
    const QString currentFormat = imu_format_combo_ ? imu_format_combo_->currentText().trimmed().toUpper() : QStringLiteral("HI91");
    const int currentRate = parseRate(imu_rate_combo_ ? imu_rate_combo_->currentText() : QStringLiteral("200"));

    const QString targetFormat = requestedFormat.isEmpty() ? currentFormat : requestedFormat.trimmed().toUpper();
    const int targetBaud = requestedBaud > 0 ? requestedBaud : effectiveCurrentBaud;
    const int targetRate = requestedRate > 0 ? requestedRate : currentRate;
    const QString targetPeriod = imuRatePeriodText(targetRate);

    if ((targetFormat != QStringLiteral("HI91") && targetFormat != QStringLiteral("HI92")) || targetPeriod.isEmpty())
    {
        log(is_english_ ? "Unsupported IMU format or rate" : "IMU 输出格式或频率不受支持");
        return false;
    }

    setImuFormatSelection(targetFormat);
    setImuBaudSelection(targetBaud);
    setImuRateSelection(targetRate);
    saveRememberedInputState();

    const CollectorSnapshot collectors = snapshotCollectors();
    const auto imuCollector = collectors.imu;
    const bool collectorRunning = imuCollector && imuCollector->isRunning();

    auto sendCommand = [this](auto&& sender, const QString& command, int waitMs = 80) -> bool {
        const std::string stdCommand = command.toStdString();
        if (!sender(stdCommand, waitMs))
        {
            return false;
        }
        log(QString("[IMU 发送] %1").arg(command.trimmed()));
        return true;
    };

    bool configured = false;
    bool needRestart = false;

    if (collectorRunning)
    {
        imuCollector->setOutputMessageType(targetFormat.toStdString());
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); }, QStringLiteral("LOG HI91 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); }, QStringLiteral("LOG HI92 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); },
                         QStringLiteral("LOG %1 ONTIME %2\r\n").arg(targetFormat, targetPeriod)))
        {
            return false;
        }
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); },
                         QStringLiteral("SAVECONFIG\r\n"), 120))
        {
            return false;
        }
        configured = true;

        if (targetBaud != effectiveCurrentBaud)
        {
            if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); },
                             QStringLiteral("SERIALCONFIG %1\r\n").arg(targetBaud), 150))
            {
                return false;
            }
            needRestart = true;
        }
    }
    else
    {
        VaporView::SerialPort tempPort;
        if (!tempPort.open(port.toStdString(), VaporView::SerialConfig::N81(effectiveCurrentBaud)))
        {
            log(QString(is_english_
                ? "[IMU] Unable to open %1 for direct configuration, saved for next connection"
                : "[IMU] 无法打开 %1 直接配置，已保存到下次连接时应用").arg(port));
            return true;
        }

        auto directSend = [&](const std::string& cmd, int waitMs) -> bool {
            const bool ok = tempPort.write(cmd.c_str(), cmd.size()) == static_cast<ssize_t>(cmd.size());
            if (ok && waitMs > 0)
            {
                QThread::msleep(waitMs);
            }
            return ok;
        };

        if (!sendCommand(directSend, QStringLiteral("LOG HI91 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand(directSend, QStringLiteral("LOG HI92 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand(directSend, QStringLiteral("LOG %1 ONTIME %2\r\n").arg(targetFormat, targetPeriod)))
        {
            return false;
        }
        if (!sendCommand(directSend, QStringLiteral("SAVECONFIG\r\n"), 120))
        {
            return false;
        }
        configured = true;
        if (targetBaud != effectiveCurrentBaud)
        {
            if (!sendCommand(directSend, QStringLiteral("SERIALCONFIG %1\r\n").arg(targetBaud), 150))
            {
                return false;
            }
            tempPort.close();
            if (tempPort.open(port.toStdString(), VaporView::SerialConfig::N81(targetBaud)))
            {
                if (!sendCommand(directSend, QStringLiteral("SAVECONFIG\r\n"), 120))
                {
                    return false;
                }
            }
        }
        tempPort.close();
    }

    if (collectorRunning && needRestart)
    {
        imuCollector->stop();
        if (!restartImuCollector(imuCollector, port, targetBaud, targetRate))
        {
            return false;
        }
        if (!imuCollector->sendAsciiCommand("SAVECONFIG\r\n", 120))
        {
            log(is_english_ ? "[IMU] Failed to persist baud rate after reconnect" : "[IMU] 重连后保存波特率配置失败");
        }
    }
    else if (collectorRunning)
    {
        imuCollector->setSampleRate(targetRate);
    }

    if (configured)
    {
        log(QString(is_english_
            ? "IMU profile applied: %1, %2 baud, %3 Hz"
            : "IMU 配置已应用: %1, %2 波特率, %3 Hz")
            .arg(targetFormat)
            .arg(targetBaud)
            .arg(targetRate));
    }
    return configured;
}

void MainWindow::setupMenuBar()
{
    data_menu_ = menuBar()->addMenu("");

    recording_directory_action_ = new QAction(this);
    connect(recording_directory_action_, &QAction::triggered, this, &MainWindow::onChooseRecordingDirectoryClicked);
    data_menu_->addAction(recording_directory_action_);

    recording_rate_menu_ = data_menu_->addMenu("");
    rebuildRecordingRateMenu();

    devices_menu_ = menuBar()->addMenu("");

    epsilon_packet_rates_action_ = new QAction(this);
    connect(epsilon_packet_rates_action_, &QAction::triggered, this, &MainWindow::onConfigureEpsilonPacketRatesClicked);
    devices_menu_->addAction(epsilon_packet_rates_action_);

    epsilon_rtcm_port_action_ = new QAction(this);
    connect(epsilon_rtcm_port_action_, &QAction::triggered, this, &MainWindow::onConfigureEpsilonRtcmPortClicked);
    devices_menu_->addAction(epsilon_rtcm_port_action_);

    epsilon_reconfigure_action_ = new QAction(this);
    connect(epsilon_reconfigure_action_, &QAction::triggered, this, &MainWindow::onReconfigureEpsilonClicked);
    devices_menu_->addAction(epsilon_reconfigure_action_);

    session_viewer_action_ = new QAction(this);
    connect(session_viewer_action_, &QAction::triggered, this, &MainWindow::onOpenSessionViewerClicked);

    exit_action_ = new QAction(this);
    exit_action_->setShortcut(QKeySequence::Quit);
    connect(exit_action_, &QAction::triggered, this, &QMainWindow::close);
    data_menu_->addAction(exit_action_);

    font_menu_ = menuBar()->addMenu("");
    font_scale_group_ = new QActionGroup(this);
    font_scale_group_->setExclusive(true);

    font_tiny_action_ = new QAction(this);
    font_tiny_action_->setCheckable(true);
    font_tiny_action_->setData(70);
    font_scale_group_->addAction(font_tiny_action_);
    font_menu_->addAction(font_tiny_action_);

    font_extra_small_action_ = new QAction(this);
    font_extra_small_action_->setCheckable(true);
    font_extra_small_action_->setData(80);
    font_scale_group_->addAction(font_extra_small_action_);
    font_menu_->addAction(font_extra_small_action_);

    font_small_action_ = new QAction(this);
    font_small_action_->setCheckable(true);
    font_small_action_->setData(90);
    font_scale_group_->addAction(font_small_action_);
    font_menu_->addAction(font_small_action_);

    font_normal_action_ = new QAction(this);
    font_normal_action_->setCheckable(true);
    font_normal_action_->setData(100);
    font_scale_group_->addAction(font_normal_action_);
    font_menu_->addAction(font_normal_action_);

    font_large_action_ = new QAction(this);
    font_large_action_->setCheckable(true);
    font_large_action_->setData(115);
    font_scale_group_->addAction(font_large_action_);
    font_menu_->addAction(font_large_action_);

    font_extra_large_action_ = new QAction(this);
    font_extra_large_action_->setCheckable(true);
    font_extra_large_action_->setData(130);
    font_scale_group_->addAction(font_extra_large_action_);
    font_menu_->addAction(font_extra_large_action_);

    connect(font_scale_group_, &QActionGroup::triggered, this, &MainWindow::onFontScaleTriggered);

    if (font_scale_percent_ <= 75)
    {
        font_tiny_action_->setChecked(true);
    }
    else if (font_scale_percent_ <= 85)
    {
        font_extra_small_action_->setChecked(true);
    }
    else if (font_scale_percent_ <= 95)
    {
        font_small_action_->setChecked(true);
    }
    else if (font_scale_percent_ <= 107)
    {
        font_normal_action_->setChecked(true);
    }
    else if (font_scale_percent_ <= 122)
    {
        font_large_action_->setChecked(true);
    }
    else
    {
        font_extra_large_action_->setChecked(true);
    }

    language_menu_ = menuBar()->addMenu("");

    lang_action_ = new QAction(this);
    lang_action_->setIcon(createLanguageIcon());
    connect(lang_action_, &QAction::triggered, this, &MainWindow::onSwitchLanguage);
    language_menu_->addAction(lang_action_);

    help_menu_ = menuBar()->addMenu("");

    about_action_ = new QAction(this);
    connect(about_action_, &QAction::triggered, this, &MainWindow::showAboutDialog);
    help_menu_->addAction(about_action_);
}

void MainWindow::setupToolBar()
{
    refresh_ports_btn_ = new QAction(this);
    refresh_ports_btn_->setIcon(createRefreshIcon());
    connect(refresh_ports_btn_, &QAction::triggered, this, &MainWindow::onRefreshPortsClicked);

    connect_btn_ = new QAction(this);
    connect_btn_->setIcon(createConnectIcon());
    connect(connect_btn_, &QAction::triggered, this, &MainWindow::onConnectClicked);

    cancel_connect_btn_ = new QAction(this);
    cancel_connect_btn_->setIcon(createCancelIcon());
    cancel_connect_btn_->setEnabled(false);
    connect(cancel_connect_btn_, &QAction::triggered, this, &MainWindow::onCancelConnectClicked);

    disconnect_btn_ = new QAction(this);
    disconnect_btn_->setIcon(createDisconnectIcon());
    disconnect_btn_->setEnabled(false);
    connect(disconnect_btn_, &QAction::triggered, this, &MainWindow::onDisconnectClicked);

    start_recording_btn_ = new QAction(this);
    start_recording_btn_->setIcon(createPlayIcon());
    start_recording_btn_->setEnabled(false);
    connect(start_recording_btn_, &QAction::triggered, this, &MainWindow::onStartRecordingClicked);

    pause_recording_btn_ = new QAction(this);
    pause_recording_btn_->setIcon(createPauseIcon());
    pause_recording_btn_->setEnabled(false);
    connect(pause_recording_btn_, &QAction::triggered, this, &MainWindow::onPauseRecordingClicked);

    stop_recording_btn_ = new QAction(this);
    stop_recording_btn_->setIcon(createStopIcon());
    stop_recording_btn_->setEnabled(false);
    connect(stop_recording_btn_, &QAction::triggered, this, &MainWindow::onStopRecordingClicked);

    rtk_config_action_ = new QAction(this);
    rtk_config_action_->setIcon(createRtkSatelliteIcon());
    connect(rtk_config_action_, &QAction::triggered, this, &MainWindow::onRtkConfigClicked);
    if (devices_menu_)
    {
        devices_menu_->addAction(rtk_config_action_);
    }

    clear_log_action_ = new QAction(this);
    clear_log_action_->setIcon(createClearLogIcon());
    connect(clear_log_action_, &QAction::triggered, this, &MainWindow::onClearLogClicked);

    session_viewer_action_->setIcon(createWaveformViewerIcon());

    theme_toggle_action_ = new QAction(this);
    connect(theme_toggle_action_, &QAction::triggered, this, &MainWindow::onToggleTheme);

    setupCustomTitleBar();
    updateThemeAction();
}

void MainWindow::setupCustomTitleBar()
{
    custom_title_bar_ = new QWidget(this);
    custom_title_bar_->setObjectName(QStringLiteral("customTitleBar"));
    custom_title_bar_->installEventFilter(this);

    auto *titleLayout = new QHBoxLayout(custom_title_bar_);
    titleLayout->setContentsMargins(10, 0, 8, 0);
    titleLayout->setSpacing(6);

    custom_logo_label_ = new QLabel(custom_title_bar_);
    custom_logo_label_->setObjectName(QStringLiteral("customTitleLogo"));
    custom_logo_label_->setFixedSize(24, 24);
    custom_logo_label_->setAlignment(Qt::AlignCenter);
    custom_logo_label_->installEventFilter(this);
    titleLayout->addWidget(custom_logo_label_, 0, Qt::AlignVCenter);

    title_menu_btn_ = createTitleBarIconButton(QStringLiteral("titleBarMenuButton"), custom_title_bar_);
    connect(title_menu_btn_, &QToolButton::clicked, this, &MainWindow::showTitleApplicationMenu);
    titleLayout->addWidget(title_menu_btn_, 0, Qt::AlignVCenter);

    custom_title_label_ = new QLabel(QStringLiteral("VaporView"), custom_title_bar_);
    custom_title_label_->setObjectName(QStringLiteral("customTitleLabel"));
    custom_title_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    custom_title_label_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    custom_title_label_->installEventFilter(this);
    titleLayout->addWidget(custom_title_label_, 0);

    titleLayout->addWidget(createTitleBarActionButton(refresh_ports_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(connect_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(cancel_connect_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(disconnect_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(start_recording_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(pause_recording_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(stop_recording_btn_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(rtk_config_action_, custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(session_viewer_action_, custom_title_bar_), 0, Qt::AlignVCenter);
    addTitleBarSeparator(titleLayout);
    titleLayout->addWidget(createTitleBarActionButton(lang_action_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addWidget(createTitleBarActionButton(theme_toggle_action_, custom_title_bar_), 0, Qt::AlignVCenter);
    titleLayout->addStretch(1);
    addTitleBarSeparator(titleLayout);

    window_minimize_btn_ = createTitleBarIconButton(QStringLiteral("windowMinimizeButton"), custom_title_bar_);
    connect(window_minimize_btn_, &QToolButton::clicked, this, &QWidget::showMinimized);
    titleLayout->addWidget(window_minimize_btn_, 0, Qt::AlignVCenter);

    window_maximize_btn_ = createTitleBarIconButton(QStringLiteral("windowMaximizeButton"), custom_title_bar_);
    connect(window_maximize_btn_, &QToolButton::clicked, this, &MainWindow::toggleWindowMaximized);
    titleLayout->addWidget(window_maximize_btn_, 0, Qt::AlignVCenter);

    window_close_btn_ = createTitleBarIconButton(QStringLiteral("windowCloseButton"), custom_title_bar_);
    connect(window_close_btn_, &QToolButton::clicked, this, &QWidget::close);
    titleLayout->addWidget(window_close_btn_, 0, Qt::AlignVCenter);

    menuBar()->hide();
    setMenuWidget(custom_title_bar_);
    updateCustomTitleBarTexts();
    updateCustomTitleBarStyle();
}

QToolButton *MainWindow::createTitleBarActionButton(QAction *action, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("titleBarButton"));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    if (!action)
    {
        button->setEnabled(false);
        return button;
    }

    auto syncFromAction = [button, action]() {
        button->setIcon(action->icon());
        button->setEnabled(action->isEnabled());
        button->setVisible(action->isVisible());
        button->setCheckable(action->isCheckable());
        button->setChecked(action->isChecked());
        button->setToolTip(action->toolTip());
        button->setStatusTip(action->statusTip());
        button->setWhatsThis(action->whatsThis());
    };
    syncFromAction();
    connect(action, &QAction::changed, button, syncFromAction);
    connect(button, &QToolButton::clicked, action, [action]() {
        if (action && action->isEnabled())
        {
            action->trigger();
        }
    });
    return button;
}

QToolButton *MainWindow::createTitleBarIconButton(const QString& objectName, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

void MainWindow::addTitleBarSeparator(QHBoxLayout *layout)
{
    auto *separator = new QFrame(custom_title_bar_);
    separator->setObjectName(QStringLiteral("titleBarSeparator"));
    separator->setFixedWidth(1);
    separator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(separator, 0, Qt::AlignVCenter);
}

void MainWindow::discardTitleApplicationMenuPanel()
{
    if (!title_application_panel_ && !title_application_sub_panel_)
    {
        return;
    }

    QFrame *panel = title_application_panel_;
    QFrame *subPanel = title_application_sub_panel_;
    title_application_panel_ = nullptr;
    title_application_sub_panel_ = nullptr;
    if (panel)
    {
        panel->hide();
        panel->deleteLater();
    }
    if (subPanel)
    {
        subPanel->hide();
        subPanel->deleteLater();
    }
}

void MainWindow::createTitleApplicationMenuPanel()
{
    if (title_application_panel_ || !central_widget_)
    {
        return;
    }

    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("titleApplicationPanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setFocusPolicy(Qt::NoFocus);
    panel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    panel->hide();
    title_application_panel_ = panel;
    panel->raise();

    auto *subPanel = new QFrame(this);
    subPanel->setObjectName(QStringLiteral("titleApplicationSubPanel"));
    subPanel->setAttribute(Qt::WA_StyledBackground, true);
    subPanel->setFocusPolicy(Qt::NoFocus);
    subPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    subPanel->hide();
    title_application_sub_panel_ = subPanel;
    subPanel->raise();

    auto closePanel = [this]() {
        if (title_application_panel_)
        {
            title_application_panel_->hide();
        }
        if (title_application_sub_panel_)
        {
            title_application_sub_panel_->hide();
        }
    };

    const bool uiBusy = connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_;

    struct TitleMenuCommand
    {
        QString text;
        QString shortcut;
        bool enabled = true;
        bool checked = false;
        bool separatorBefore = false;
        std::function<void()> handler;
    };
    struct TitleMenuSection
    {
        QString title;
        QVector<TitleMenuCommand> commands;
    };

    QFont menuFont = qApp->font();
    menuFont.setPixelSize(std::max(1, scalePixels(14)));
    panel->setFont(menuFont);
    const QFontMetrics menuMetrics(menuFont);
    const int rowVerticalPadding = scalePixels(2);
    const int rowHeight = std::max(scalePixels(22), menuMetrics.height() + rowVerticalPadding * 2);
    const int menuVerticalPadding = std::max(scalePixels(4), rowHeight / 3);
    const int menuCornerRadius = std::min(scalePixels(8), menuVerticalPadding);
    panel->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_, menuCornerRadius));
    const int rowLeftPadding = scalePixels(16);
    const int rowRightPadding = scalePixels(12);
    const int rowSpacing = scalePixels(6);
    const int checkColumnWidth = scalePixels(10);
    const int arrowColumnWidth = scalePixels(14);
    const int shortcutGap = scalePixels(24);
    const int mainMenuMinWidth = scalePixels(72);
    const int subMenuMinWidth = scalePixels(72);
    subPanel->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_, menuCornerRadius));
    auto commandRowsHeight = [&](const QVector<TitleMenuCommand>& commands) {
        return menuVerticalPadding * 2 + rowHeight * static_cast<int>(commands.size());
    };

    QVector<TitleMenuSection> sections;
    TitleMenuSection fileSection{
        is_english_ ? QStringLiteral("File") : QStringLiteral("文件"),
        {
            {is_english_ ? QStringLiteral("Recording Folder...") : QStringLiteral("记录目录..."),
             QStringLiteral("Ctrl+R"),
             true,
             false,
             false,
             [this]() { onChooseRecordingDirectoryClicked(); }},
            {is_english_ ? QStringLiteral("Data Viewer...") : QStringLiteral("数据查看器..."),
             QString(),
             true,
             false,
             false,
             [this]() { onOpenSessionViewerClicked(); }},
            {is_english_ ? QStringLiteral("Exit") : QStringLiteral("退出"),
             QStringLiteral("Ctrl+Q"),
             true,
             false,
             true,
             [this]() { close(); }}
        }
    };

    TitleMenuSection viewSection{
        is_english_ ? QStringLiteral("View") : QStringLiteral("视图"),
        {
            {is_english_ ? QStringLiteral("Tiny (70%)") : QStringLiteral("超小 (70%)"),
             QString(),
             true,
             font_scale_percent_ == 70,
             true,
             [this]() { setFontScale(70); }},
            {is_english_ ? QStringLiteral("Extra Small (80%)") : QStringLiteral("特小 (80%)"),
             QString(),
             true,
             font_scale_percent_ == 80,
             false,
             [this]() { setFontScale(80); }},
            {is_english_ ? QStringLiteral("Small (90%)") : QStringLiteral("小号 (90%)"),
             QString(),
             true,
             font_scale_percent_ == 90,
             false,
             [this]() { setFontScale(90); }},
            {is_english_ ? QStringLiteral("Normal (100%)") : QStringLiteral("标准 (100%)"),
             QString(),
             true,
             font_scale_percent_ == 100,
             false,
             [this]() { setFontScale(100); }},
            {is_english_ ? QStringLiteral("Large (115%)") : QStringLiteral("大号 (115%)"),
             QString(),
             true,
             font_scale_percent_ == 115,
             false,
             [this]() { setFontScale(115); }},
            {is_english_ ? QStringLiteral("Extra Large (130%)") : QStringLiteral("超大 (130%)"),
             QString(),
             true,
             font_scale_percent_ == 130,
             false,
             [this]() { setFontScale(130); }}
        }
    };

    TitleMenuSection developerSection{is_english_ ? QStringLiteral("Developer") : QStringLiteral("开发者"), {}};
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("TCP Wave Raw") : QStringLiteral("TCP波形原始帧"),
         QString(),
         true,
         waveform_recording_rate_hz_ == 0,
         false,
         [this]() { setWaveformRecordingRateHz(0); }});
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("EPSILON Raw") : QStringLiteral("EPSILON原始帧"),
         QString(),
         true,
         imu_recording_rate_hz_ == 0,
         false,
         [this]() { setImuRecordingRateHz(0); }});
    for (int rate : QVector<int>{1, 2, 5, 10, 20, 50, 100, 200})
    {
        developerSection.commands.push_back({
            QStringLiteral("%1 Hz").arg(rate),
            QString(),
            true,
            rate == std::clamp(recording_export_rate_hz_, 1, 200),
            rate == 1,
            [this, rate]() { setRecordingExportRateHz(rate); }
        });
    }
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("EPSILON Packet Rates...") : QStringLiteral("设置EPSILON包频率..."),
         QString(),
         !uiBusy,
         false,
         true,
         [this]() { onConfigureEpsilonPacketRatesClicked(); }});
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("Configure EPSILON RTCM Port...") : QStringLiteral("配置EPSILON RTCM串口..."),
         QString(),
         !uiBusy,
         false,
         false,
         [this]() { onConfigureEpsilonRtcmPortClicked(); }});
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("Reconfigure EPSILON Output...") : QStringLiteral("重新配置EPSILON输出..."),
         QString(),
         !uiBusy,
         false,
         false,
         [this]() { onReconfigureEpsilonClicked(); }});
    developerSection.commands.push_back(
        {is_english_ ? QStringLiteral("RTK Config") : QStringLiteral("RTK配置"),
         QString(),
         true,
         false,
         true,
         [this]() { onRtkConfigClicked(); }});

    TitleMenuSection helpSection{
        is_english_ ? QStringLiteral("Help") : QStringLiteral("帮助"),
        {
            {is_english_ ? QStringLiteral("About") : QStringLiteral("关于"),
             QString(),
             true,
             false,
             false,
             [this]() { showAboutDialog(); }}
        }
    };

    sections.push_back(fileSection);
    sections.push_back(viewSection);
    sections.push_back(developerSection);
    sections.push_back(helpSection);

    int mainMenuWidth = mainMenuMinWidth;
    QVector<int> subMenuWidths;
    subMenuWidths.reserve(sections.size());
    QVector<bool> subMenuNeedsCheckColumn;
    subMenuNeedsCheckColumn.reserve(sections.size());
    for (const TitleMenuSection& section : sections)
    {
        mainMenuWidth = std::max(mainMenuWidth,
                                 rowLeftPadding +
                                      menuMetrics.horizontalAdvance(section.title) +
                                      rowSpacing +
                                      arrowColumnWidth +
                                      rowRightPadding);

        bool needsCheckColumn = false;
        for (const TitleMenuCommand& command : section.commands)
        {
            needsCheckColumn = needsCheckColumn || command.checked;
        }
        subMenuNeedsCheckColumn.push_back(needsCheckColumn);

        int sectionWidth = subMenuMinWidth;
        for (const TitleMenuCommand& command : section.commands)
        {
            int commandWidth = rowLeftPadding + menuMetrics.horizontalAdvance(command.text) + rowRightPadding;
            if (needsCheckColumn)
            {
                commandWidth += checkColumnWidth + rowSpacing;
            }
            if (!command.shortcut.isEmpty())
            {
                commandWidth += shortcutGap + menuMetrics.horizontalAdvance(command.shortcut);
            }
            sectionWidth = std::max(sectionWidth, commandWidth);
        }
        subMenuWidths.push_back(sectionWidth);
    }

    auto *mainMenu = new QFrame(panel);
    mainMenu->setObjectName(QStringLiteral("titleApplicationMainMenu"));
    mainMenu->setAttribute(Qt::WA_StyledBackground, true);
    mainMenu->setFixedSize(mainMenuWidth, menuVerticalPadding * 2 + rowHeight * sections.size());
    mainMenu->move(0, 0);

    auto *mainLayout = new QVBoxLayout(mainMenu);
    mainLayout->setContentsMargins(0, menuVerticalPadding, 0, menuVerticalPadding);
    mainLayout->setSpacing(0);

    auto *subMenu = new QFrame(subPanel);
    subMenu->setObjectName(QStringLiteral("titleApplicationSubMenu"));
    subMenu->setAttribute(Qt::WA_StyledBackground, true);
    subMenu->setFixedWidth(subMenuWidths.value(0, subMenuMinWidth));
    subMenu->move(0, 0);
    subMenu->hide();

    auto *subLayout = new QVBoxLayout(subMenu);
    subLayout->setContentsMargins(0, 0, 0, 0);
    subLayout->setSpacing(0);
    auto *stack = new QStackedWidget(subMenu);
    stack->setObjectName(QStringLiteral("titleApplicationSubStack"));
    stack->setAttribute(Qt::WA_StyledBackground, true);
    subLayout->addWidget(stack);

    auto createRow = [&](QWidget *parent,
                         const QString& text,
                          const QString& trailingText,
                          bool enabled,
                          bool checked,
                          bool reserveCheckColumn,
                          const QString& arrow,
                          const std::function<void()>& clickHandler) {
        auto *row = new QFrame(parent);
        row->setObjectName(QStringLiteral("titleApplicationMenuItem"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setEnabled(enabled);
        row->setFixedHeight(rowHeight);
        row->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
        row->setMouseTracking(true);

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(rowLeftPadding, 0, rowRightPadding, 0);
        rowLayout->setSpacing(rowSpacing);

        if (reserveCheckColumn || checked)
        {
            auto *checkLabel = new QLabel(checked ? QStringLiteral("✓") : QString(), row);
            checkLabel->setObjectName(QStringLiteral("titleApplicationMenuCheck"));
            checkLabel->setEnabled(enabled);
            checkLabel->setFixedWidth(checkColumnWidth);
            checkLabel->setAlignment(Qt::AlignCenter);
            checkLabel->setMargin(0);
            checkLabel->setIndent(0);
            checkLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            rowLayout->addWidget(checkLabel);
        }

        auto *textLabel = new QLabel(text, row);
        textLabel->setObjectName(QStringLiteral("titleApplicationMenuText"));
        textLabel->setEnabled(enabled);
        textLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        textLabel->setMargin(0);
        textLabel->setIndent(0);
        textLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        textLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        rowLayout->addWidget(textLabel, 1);

        if (!trailingText.isEmpty())
        {
            auto *shortcutLabel = new QLabel(trailingText, row);
            shortcutLabel->setObjectName(QStringLiteral("titleApplicationMenuShortcut"));
            shortcutLabel->setEnabled(enabled);
            shortcutLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            shortcutLabel->setMargin(0);
            shortcutLabel->setIndent(0);
            shortcutLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            rowLayout->addWidget(shortcutLabel);
        }

        if (!arrow.isEmpty())
        {
            auto *arrowLabel = new QLabel(arrow, row);
            arrowLabel->setObjectName(QStringLiteral("titleApplicationMenuArrow"));
            arrowLabel->setEnabled(enabled);
            arrowLabel->setFixedWidth(arrowColumnWidth);
            arrowLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            arrowLabel->setMargin(0);
            arrowLabel->setIndent(0);
            arrowLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            rowLayout->addWidget(arrowLabel);
        }

        if (clickHandler)
        {
            row->installEventFilter(new MenuItemEventFilter({}, [closePanel, clickHandler]() {
                closePanel();
                clickHandler();
            }, row));
        }
        return row;
    };

    auto sectionRows = std::make_shared<QVector<QFrame *>>();

    for (int sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex)
    {
        QWidget *page = new QWidget(stack);
        page->setObjectName(QStringLiteral("titleApplicationSubPage"));
        page->setAttribute(Qt::WA_StyledBackground, true);
        page->setFixedSize(subMenuWidths.value(sectionIndex, subMenuMinWidth),
                           commandRowsHeight(sections[sectionIndex].commands));
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(0);
        auto *pageContent = new QWidget(page);
        pageContent->setObjectName(QStringLiteral("titleApplicationSubPageContent"));
        pageContent->setAttribute(Qt::WA_StyledBackground, true);
        auto *contentLayout = new QVBoxLayout(pageContent);
        contentLayout->setContentsMargins(0, menuVerticalPadding, 0, menuVerticalPadding);
        contentLayout->setSpacing(0);
        pageLayout->addWidget(pageContent);

        for (const TitleMenuCommand& command : sections[sectionIndex].commands)
        {
            contentLayout->addWidget(createRow(pageContent,
                                               command.text,
                                               command.shortcut,
                                               command.enabled,
                                               command.checked,
                                               subMenuNeedsCheckColumn.value(sectionIndex, false),
                                               QString(),
                                               command.enabled ? command.handler : std::function<void()>{}));
        }
        stack->addWidget(page);

        auto *sectionRow = createRow(mainMenu,
                                     sections[sectionIndex].title,
                                      QString(),
                                      true,
                                      false,
                                      false,
                                      QStringLiteral("›"),
                                      {});
        sectionRow->setProperty("selected", false);
        sectionRows->push_back(sectionRow);
        mainLayout->addWidget(sectionRow);
        sectionRow->installEventFilter(new MenuItemEventFilter([this, stack, subMenu, mainMenu, panel, subPanel, mainMenuWidth, menuVerticalPadding, subMenuWidths, sectionRows, sectionRow, sectionIndex]() {
            stack->setCurrentIndex(sectionIndex);
            if (QWidget *currentPage = stack->currentWidget())
            {
                const int subMenuWidth = subMenuWidths.value(sectionIndex, currentPage->width());
                const int subMenuBorderWidth = std::max(1, subMenu->frameWidth());
                const int subMenuTop = std::max(0, sectionRow->y() - menuVerticalPadding - subMenuBorderWidth);
                subMenu->setFixedSize(subMenuWidth, currentPage->height());
                subPanel->setFixedSize(subMenu->size());
                const QPoint subMenuPos = panel->mapTo(this, QPoint(mainMenuWidth, subMenuTop));
                const int popupMargin = scalePixels(4);
                int subMenuX = subMenuPos.x();
                if (subMenuX + subPanel->width() > width() - popupMargin)
                {
                    subMenuX = panel->x() - subPanel->width();
                }
                subMenuX = std::clamp(subMenuX,
                                      popupMargin,
                                      std::max(popupMargin, width() - subPanel->width() - popupMargin));
                const int subMenuY = std::clamp(subMenuPos.y(),
                                                popupMargin,
                                                std::max(popupMargin, height() - subPanel->height() - popupMargin));
                subPanel->move(subMenuX, subMenuY);
                subMenu->move(0, 0);
                subMenu->raise();
                subMenu->show();
                subPanel->show();
                subPanel->raise();
            }
            for (QFrame *row : *sectionRows)
            {
                if (row)
                {
                    row->setProperty("selected", row == sectionRow);
                    row->style()->unpolish(row);
                    row->style()->polish(row);
                    row->update();
                }
            }
        }, {}, sectionRow));
    }
    mainLayout->addStretch(1);
    stack->setCurrentIndex(0);
    panel->setFixedSize(mainMenuWidth, mainMenu->height());
    subPanel->setFixedSize(subMenu->width(), 0);
}

void MainWindow::showTitleApplicationMenu()
{
    if (!title_menu_btn_)
    {
        return;
    }

    createTitleApplicationMenuPanel();
    if (!title_application_panel_)
    {
        return;
    }

    if (title_application_panel_->isVisible())
    {
        title_application_panel_->hide();
        if (title_application_sub_panel_)
        {
            title_application_sub_panel_->hide();
        }
        return;
    }

    const QPoint anchor = title_menu_btn_->mapTo(this, QPoint(0, title_menu_btn_->height() + scalePixels(4)));
    const int x = std::clamp(anchor.x(),
                             scalePixels(4),
                             std::max(scalePixels(4), width() - title_application_panel_->width() - scalePixels(4)));
    const int y = std::max(anchor.y(), scalePixels(4));
    title_application_panel_->move(x, y);
    if (title_application_sub_panel_)
    {
        title_application_sub_panel_->hide();
    }
    title_application_panel_->show();
    title_application_panel_->raise();
}

void MainWindow::setupStatusBar()
{
    status_label_ = new QLabel(this);
    statusBar()->addWidget(status_label_);

    status_task_progress_bar_ = new QProgressBar(this);
    status_task_progress_bar_->setVisible(false);
    status_task_progress_bar_->setTextVisible(true);
    status_task_progress_bar_->setFixedHeight(18);
    status_task_progress_bar_->setMinimumWidth(180);
    status_task_progress_bar_->setMaximumWidth(260);
    status_task_progress_bar_->setRange(0, 100);
    status_task_progress_bar_->setValue(0);
    status_task_progress_bar_->setFormat(QString());
    statusBar()->addWidget(status_task_progress_bar_);

    status_task_spinner_label_ = new QLabel(this);
    status_task_spinner_label_->setObjectName("statusTaskSpinner");
    status_task_spinner_label_->setAlignment(Qt::AlignCenter);
    status_task_spinner_label_->setFixedSize(18, 18);
    status_task_spinner_label_->setVisible(false);
    statusBar()->addWidget(status_task_spinner_label_);

    status_task_spinner_timer_ = new QTimer(this);
    status_task_spinner_timer_->setInterval(120);
    connect(status_task_spinner_timer_, &QTimer::timeout, this, [this]() {
        if (!status_task_spinner_label_ || !status_task_spinner_label_->isVisible())
        {
            return;
        }
        static const QStringList frames = {
            QStringLiteral("◐"),
            QStringLiteral("◓"),
            QStringLiteral("◑"),
            QStringLiteral("◒"),
        };
        status_task_spinner_label_->setText(frames.at(status_task_spinner_index_ % frames.size()));
        status_task_spinner_index_ = (status_task_spinner_index_ + 1) % frames.size();
    });

}

void MainWindow::startStatusTaskSpinner()
{
    if (!status_task_spinner_label_ || !status_task_spinner_timer_)
    {
        return;
    }

    status_task_spinner_label_->setVisible(true);
    if (!status_task_spinner_timer_->isActive())
    {
        status_task_spinner_index_ = 0;
        status_task_spinner_label_->setText(QStringLiteral("◐"));
        status_task_spinner_timer_->start();
    }
}

void MainWindow::stopStatusTaskSpinner()
{
    if (status_task_spinner_timer_)
    {
        status_task_spinner_timer_->stop();
    }
    if (status_task_spinner_label_)
    {
        status_task_spinner_label_->clear();
        status_task_spinner_label_->setToolTip(QString());
        status_task_spinner_label_->setVisible(false);
    }
    status_task_spinner_index_ = 0;
}

void MainWindow::showStatusTaskProgress(const QString& label, int value, int maximum)
{
    if (!status_task_progress_bar_)
    {
        return;
    }

    const int normalizedMaximum = std::max(1, maximum);
    const int normalizedValue = std::clamp(value, 0, normalizedMaximum);
    status_task_progress_bar_->setVisible(true);
    status_task_progress_bar_->setRange(0, normalizedMaximum);
    status_task_progress_bar_->setValue(normalizedValue);
    status_task_progress_bar_->setFormat(QStringLiteral("%1 %p%").arg(label));
    status_task_progress_bar_->setToolTip(label);
    if (status_task_spinner_label_)
    {
        status_task_spinner_label_->setToolTip(label);
    }
    startStatusTaskSpinner();
}

void MainWindow::showBusyStatusTaskProgress(const QString& label)
{
    if (!status_task_progress_bar_)
    {
        return;
    }

    status_task_progress_bar_->setVisible(true);
    status_task_progress_bar_->setRange(0, 100);
    status_task_progress_bar_->setValue(0);
    status_task_progress_bar_->setFormat(label);
    status_task_progress_bar_->setToolTip(label);
    if (status_task_spinner_label_)
    {
        status_task_spinner_label_->setToolTip(label);
    }
    startStatusTaskSpinner();
}

void MainWindow::hideStatusTaskProgress()
{
    if (!status_task_progress_bar_)
    {
        return;
    }

    status_task_progress_bar_->setVisible(false);
    status_task_progress_bar_->setRange(0, 100);
    status_task_progress_bar_->setValue(0);
    status_task_progress_bar_->setFormat(QString());
    status_task_progress_bar_->setToolTip(QString());
    stopStatusTaskSpinner();
}

void MainWindow::setupCentralWidget()
{
    central_widget_ = new QWidget(this);
    central_widget_->setObjectName("appCentralWidget");
    central_widget_->setAttribute(Qt::WA_StyledBackground, true);
    central_widget_->setAutoFillBackground(true);
    setCentralWidget(central_widget_);

    auto *main_h_layout = new QHBoxLayout(central_widget_);
    main_h_layout->setSpacing(0);
    main_h_layout->setContentsMargins(8, 8, 8, 8);

    auto *left_widget = new QWidget(this);
    left_widget->setObjectName("mainCardsPane");
    left_widget->setAttribute(Qt::WA_StyledBackground, true);
    left_widget->setAutoFillBackground(true);
    main_layout_ = new QVBoxLayout(left_widget);
    main_layout_->setSpacing(0);
    main_layout_->setContentsMargins(0, 0, 0, 0);

    setupConfigPanel();
    setupDataPanels();

    auto *left_scroll_area = new QScrollArea(this);
    left_scroll_area->setObjectName("mainCardsScrollArea");
    left_scroll_area->setAttribute(Qt::WA_StyledBackground, true);
    left_scroll_area->setAutoFillBackground(true);
    left_scroll_area->viewport()->setObjectName("mainCardsViewport");
    left_scroll_area->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    left_scroll_area->viewport()->setAutoFillBackground(true);
    left_scroll_area->setWidgetResizable(true);
    left_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    left_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    left_scroll_area->setFrameShape(QFrame::NoFrame);
    left_scroll_area->setWidget(left_widget);

    setupLogPanel();

    auto *main_splitter = new QSplitter(Qt::Horizontal, central_widget_);
    main_splitter->setObjectName("mainContentSplitter");
    main_splitter->setAttribute(Qt::WA_StyledBackground, true);
    main_splitter->setAutoFillBackground(true);
    main_splitter->setChildrenCollapsible(false);
    main_splitter->setHandleWidth(8);
    main_splitter->addWidget(left_scroll_area);
    main_splitter->addWidget(log_side_panel_);
    main_splitter->setStretchFactor(0, 6);
    main_splitter->setStretchFactor(1, 1);
    main_splitter->setSizes({1120, 260});
    main_h_layout->addWidget(main_splitter);
}

QStringList MainWindow::getAvailablePorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : infos)
    {
#ifdef _WIN32
        ports.append(info.portName());
#else
        const QString path = info.systemLocation();
        ports.append(path.isEmpty() ? info.portName() : path);
#endif
    }

    ports.removeDuplicates();
    ports.sort();
    return ports;
}

void MainWindow::setupConfigPanel()
{
    config_group_ = new QGroupBox(this);
    config_group_->setObjectName("sensorGroupBox");
    config_group_->setMinimumWidth(860);
    config_group_->setMinimumHeight(kConfigCardMinHeight);
    config_group_->setFixedHeight(kConfigCardMinHeight);
    config_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *config_root_layout = new QVBoxLayout(config_group_);
    config_root_layout->setSpacing(4);
    config_root_layout->setContentsMargins(0, 0, 0, kConfigCardBottomPadding);

    auto *config_form_widget = new QWidget(config_group_);
    config_form_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *config_layout = new QGridLayout(config_form_widget);
    config_layout->setVerticalSpacing(4);
    config_layout->setHorizontalSpacing(8);
    config_layout->setContentsMargins(8, 0, 8, kConfigFormBottomPadding);
    config_layout->setColumnStretch(0, 0);
    config_layout->setColumnStretch(1, 0);
    config_layout->setColumnStretch(2, 0);
    config_layout->setColumnStretch(3, 0);
    config_layout->setColumnStretch(4, 0);
    config_layout->setColumnStretch(5, 0);
    config_layout->setColumnStretch(6, 1);
    config_layout->setColumnMinimumWidth(0, 110);
    config_layout->setColumnMinimumWidth(1, 170);
    config_layout->setColumnMinimumWidth(2, 100);
    config_layout->setColumnMinimumWidth(3, 80);
    config_layout->setColumnMinimumWidth(4, 100);

    QStringList baudRates = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "500000", "921600"};
    QStringList ports = getAvailablePorts();

    auto createRateCombo = [this](int maxRate = 500) {
        auto *combo = new QComboBox(this);
        const QList<int> supportedRates = {1, 2, 5, 10, 20, 50, 70, 100, 200, 250, 500, 1000};
        for (int rate : supportedRates)
        {
            if (rate <= maxRate)
            {
                combo->addItem(QString::number(rate));
            }
        }
        const int preferredIndex = combo->findText(maxRate >= 200 ? QStringLiteral("200") : QStringLiteral("20"));
        combo->setCurrentIndex(preferredIndex >= 0 ? preferredIndex : 0);
        combo->setEditable(true);
        combo->setFixedHeight(kMainPageInputHeight);
        combo->setFixedWidth(100);
        combo->setValidator(new QIntValidator(1, maxRate, combo));
        return combo;
    };

    auto addNoSetRateOption = [this](QComboBox *combo) {
        if (!combo)
        {
            return;
        }
        if (combo->findText(QStringLiteral("No Set")) < 0 &&
            combo->findText(QStringLiteral("不设定")) < 0)
        {
            combo->addItem(is_english_ ? QStringLiteral("No Set") : QStringLiteral("不设定"));
        }
        combo->setValidator(nullptr);
    };

    auto createPortRow = [this, config_layout, &baudRates, &ports, &createRateCombo](QLabel*& lbl, QComboBox*& portCombo, QComboBox*& baudCombo, QLabel*& rateLbl, QComboBox*& rateCombo, const QString& defaultPort, const QString& defaultBaud, int row, int maxRate = 500) {
        lbl = new QLabel(this);
        lbl->setObjectName("fieldLabel");
        lbl->setFixedHeight(kMainPageInputHeight);
        lbl->setFixedWidth(80);
        config_layout->addWidget(lbl, row, 0, Qt::AlignVCenter | Qt::AlignLeft);

        portCombo = new QComboBox(this);
        portCombo->addItem(is_english_ ? "-- Select --" : "-- 选择 --");
        portCombo->addItems(ports);
        portCombo->setEditable(true);
        portCombo->setMinimumContentsLength(10);
        portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        portCombo->setFixedHeight(kMainPageInputHeight);
        portCombo->setMinimumWidth(160);
        portCombo->setMaximumWidth(190);
        portCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        portCombo->setMaxVisibleItems(15);

        int defaultIdx = portCombo->findText(defaultPort);
        if (defaultIdx >= 0)
        {
            portCombo->setCurrentIndex(defaultIdx);
        }
        else
        {
            portCombo->setEditText(defaultPort);
        }
        config_layout->addWidget(portCombo, row, 1, Qt::AlignVCenter);

        baudCombo = new QComboBox(this);
        baudCombo->addItems(baudRates);
        baudCombo->setCurrentText(defaultBaud);
        baudCombo->setFixedHeight(kMainPageInputHeight);
        baudCombo->setFixedWidth(100);
        config_layout->addWidget(baudCombo, row, 2, Qt::AlignVCenter);

        rateLbl = new QLabel(this);
        rateLbl->setObjectName("fieldLabel");
        rateLbl->setFixedHeight(kMainPageInputHeight);
        config_layout->addWidget(rateLbl, row, 3, Qt::AlignVCenter | Qt::AlignRight);

        rateCombo = createRateCombo(maxRate);
        config_layout->addWidget(rateCombo, row, 4, Qt::AlignVCenter);
    };

    auto *configTitleBar = new QWidget(config_group_);
    configTitleBar->setObjectName("sectionTitleBar");
    configTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *configTitleLayout = new QHBoxLayout(configTitleBar);
    configTitleLayout->setContentsMargins(8, 2, 8, 2);
    configTitleLayout->setSpacing(8);

    config_inline_title_lbl_ = new QLabel(this);
    config_inline_title_lbl_->setObjectName("sectionTitleLabel");
    config_inline_title_lbl_->setFixedHeight(kMainPageButtonHeight);
    config_inline_title_lbl_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    configTitleLayout->addWidget(config_inline_title_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);

    auto_detect_ports_btn_ = new QPushButton(this);
    auto_detect_ports_btn_->setFixedHeight(kMainPageButtonHeight);
    auto_detect_ports_btn_->setMinimumWidth(120);
    connect(auto_detect_ports_btn_, &QPushButton::clicked, this, &MainWindow::onAutoDetectPortsClicked);
    configTitleLayout->addWidget(auto_detect_ports_btn_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    configTitleLayout->addStretch(1);

    data_source_mode_lbl_ = new QLabel(this);
    data_source_mode_lbl_->setObjectName("fieldLabel");
    data_source_mode_combo_ = new QComboBox(this);
    data_source_mode_combo_->addItem(sourceModeDisplayText(false, 0));
    data_source_mode_combo_->addItem(sourceModeDisplayText(false, 1));
    data_source_mode_combo_->setFixedHeight(kMainPageInputHeight);
    data_source_mode_combo_->setMinimumWidth(180);
    data_source_mode_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(data_source_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDataSourceModeChanged);
    configTitleLayout->addWidget(data_source_mode_lbl_, 0, Qt::AlignVCenter | Qt::AlignRight);
    configTitleLayout->addWidget(data_source_mode_combo_, 0, Qt::AlignVCenter);

    sky_device_config_btn_ = new QPushButton(this);
    sky_device_config_btn_->setFixedHeight(kMainPageButtonHeight);
    sky_device_config_btn_->setMinimumWidth(150);
    connect(sky_device_config_btn_, &QPushButton::clicked, this, &MainWindow::onSkyDeviceConfigClicked);
    configTitleLayout->addWidget(sky_device_config_btn_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    config_root_layout->addWidget(configTitleBar);

    sky_telemetry_port_lbl_ = new QLabel(this);
    sky_telemetry_port_lbl_->setObjectName("fieldLabel");
    sky_telemetry_port_combo_ = new QComboBox(this);
    sky_telemetry_port_combo_->addItems(ports);
    sky_telemetry_port_combo_->setEditable(true);
    sky_telemetry_port_combo_->setFixedHeight(kMainPageInputHeight);
    sky_telemetry_port_combo_->setMinimumWidth(160);
#ifdef _WIN32
    sky_telemetry_port_combo_->setEditText(QStringLiteral("COM11"));
#else
    sky_telemetry_port_combo_->setEditText(QStringLiteral("/tmp/vapor_ground"));
#endif
    sky_telemetry_baud_lbl_ = new QLabel(this);
    sky_telemetry_baud_lbl_->setObjectName("fieldLabel");
    sky_telemetry_baud_combo_ = new QComboBox(this);
    sky_telemetry_baud_combo_->addItems(baudRates);
    sky_telemetry_baud_combo_->setCurrentText(QStringLiteral("921600"));
    sky_telemetry_baud_combo_->setFixedHeight(kMainPageInputHeight);
    sky_telemetry_baud_combo_->setFixedWidth(100);
    sky_telemetry_row_widget_ = new QWidget(config_group_);
    auto *skyTelemetryLayout = new QHBoxLayout(sky_telemetry_row_widget_);
    skyTelemetryLayout->setContentsMargins(8, 2, 8, 2);
    skyTelemetryLayout->setSpacing(8);
    skyTelemetryLayout->addWidget(sky_telemetry_port_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addWidget(sky_telemetry_port_combo_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(sky_telemetry_baud_combo_, 0, Qt::AlignVCenter);
    skyTelemetryLayout->addWidget(sky_telemetry_baud_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    skyTelemetryLayout->addStretch(1);
    sky_telemetry_row_widget_->setVisible(false);
    config_root_layout->addWidget(sky_telemetry_row_widget_);

    int row = 0;

#ifdef _WIN32
    createPortRow(epsilon_lbl_, epsilon_port_combo_, epsilon_baud_combo_, epsilon_rate_lbl_, epsilon_rate_combo_, "COM3", "921600", row++, 200);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "COM5", "9600", row++, kPtbMaxSampleRateHz);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "COM6", "19200", row++);
    createPortRow(lidar_lbl_, lidar_port_combo_, lidar_baud_combo_, lidar_rate_lbl_, lidar_rate_combo_, "COM7", "500000", row++, 100);
#else
    createPortRow(epsilon_lbl_, epsilon_port_combo_, epsilon_baud_combo_, epsilon_rate_lbl_, epsilon_rate_combo_, "/dev/ttyEPSILON", "921600", row++, 200);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "/dev/ttyBARO", "9600", row++, kPtbMaxSampleRateHz);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "/dev/ttyHMP", "19200", row++);
    createPortRow(lidar_lbl_, lidar_port_combo_, lidar_baud_combo_, lidar_rate_lbl_, lidar_rate_combo_, "/dev/ttyLidar", "500000", row++, 100);
#endif

    auto addRemoteButtons = [this, config_layout](int rowIndex,
                                                  QWidget*& buttonsWidget,
                                                  QPushButton*& connectButton,
                                                  QPushButton*& disconnectButton,
                                                  QPushButton*& reconnectButton,
                                                  VaporView::SkyDeviceId device) {
        auto *buttons = new QWidget(this);
        buttonsWidget = buttons;
        auto *layout = new QHBoxLayout(buttons);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        connectButton = createRemoteDeviceButton(QStringLiteral("连接"), VaporView::CommandId::ConnectDevice, device);
        disconnectButton = createRemoteDeviceButton(QStringLiteral("断开"), VaporView::CommandId::DisconnectDevice, device);
        reconnectButton = createRemoteDeviceButton(QStringLiteral("重连"), VaporView::CommandId::ReconnectDevice, device);
        layout->addWidget(connectButton);
        layout->addWidget(disconnectButton);
        layout->addWidget(reconnectButton);
        layout->addStretch();
        config_layout->addWidget(buttons, rowIndex, 5, Qt::AlignVCenter | Qt::AlignLeft);
    };
    addRemoteButtons(0, epsilon_remote_buttons_widget_, epsilon_remote_connect_btn_, epsilon_remote_disconnect_btn_, epsilon_remote_reconnect_btn_, VaporView::SkyDeviceId::Epsilon);
    addRemoteButtons(1, ptb_remote_buttons_widget_, ptb_remote_connect_btn_, ptb_remote_disconnect_btn_, ptb_remote_reconnect_btn_, VaporView::SkyDeviceId::Ptb);
    addRemoteButtons(2, hmp_remote_buttons_widget_, hmp_remote_connect_btn_, hmp_remote_disconnect_btn_, hmp_remote_reconnect_btn_, VaporView::SkyDeviceId::Hmp);
    addRemoteButtons(3, lidar_remote_buttons_widget_, lidar_remote_connect_btn_, lidar_remote_disconnect_btn_, lidar_remote_reconnect_btn_, VaporView::SkyDeviceId::Lidar);

    if (epsilon_rate_combo_)
    {
        config_layout->removeWidget(epsilon_rate_combo_);
        delete epsilon_rate_combo_;
        epsilon_rate_combo_ = nullptr;
        epsilon_packet_rates_btn_ = new QPushButton(this);
        epsilon_packet_rates_btn_->setFixedHeight(kMainPageInputHeight);
        epsilon_packet_rates_btn_->setMinimumWidth(140);
        connect(epsilon_packet_rates_btn_, &QPushButton::clicked, this, &MainWindow::onConfigureEpsilonPacketRatesClicked);
        config_layout->addWidget(epsilon_packet_rates_btn_, 0, 4, Qt::AlignVCenter);
    }

    for (QComboBox *combo : {ptb_rate_combo_, hmp_rate_combo_, lidar_rate_combo_})
    {
        addNoSetRateOption(combo);
    }

    connect(ptb_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onPtbRateChanged);
    connect(hmp_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onHmpRateChanged);
    connect(lidar_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onLidarRateChanged);

    data_telemetry_summary_card_ = new QFrame(this);
    data_telemetry_summary_card_->setObjectName(QStringLiteral("epsilonSectionCard"));
    data_telemetry_summary_card_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::MinimumExpanding);
    auto *telemetrySummaryLayout = new QVBoxLayout(data_telemetry_summary_card_);
    telemetrySummaryLayout->setContentsMargins(2, 2, 2, 2);
    telemetrySummaryLayout->setSpacing(0);

    data_telemetry_summary_lbl_ = new QLabel(data_telemetry_summary_card_);
    data_telemetry_summary_lbl_->setObjectName("fieldLabel");
    data_telemetry_summary_lbl_->setTextFormat(Qt::RichText);
    data_telemetry_summary_lbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    data_telemetry_summary_lbl_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    data_telemetry_summary_lbl_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    data_telemetry_summary_lbl_->setMinimumHeight(kMainPageInputHeight);
    data_telemetry_summary_lbl_->setWordWrap(false);
    telemetrySummaryLayout->addWidget(data_telemetry_summary_lbl_);
    data_telemetry_summary_card_->setVisible(false);
    config_layout->addWidget(data_telemetry_summary_card_, 0, 6, row, 1, Qt::AlignTop | Qt::AlignLeft);

    config_root_layout->addWidget(config_form_widget, 0, Qt::AlignTop);
    main_layout_->addWidget(config_group_, 0);
}

void MainWindow::setupDataPanels()
{
    data_group_ = new QGroupBox(this);
    data_group_->setObjectName("sensorGroupBox");
    data_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *data_layout = new QVBoxLayout(data_group_);
    data_layout->setSpacing(0);
    data_layout->setContentsMargins(0, 0, 0, 0);

    auto *sensor_row = new QWidget(data_group_);
    auto *sensor_layout = new QHBoxLayout(sensor_row);
    sensor_layout->setContentsMargins(0, 0, 0, 0);
    sensor_layout->setSpacing(2);

    epsilon_group_ = new QGroupBox(this);
    epsilon_group_->setObjectName("sensorGroupBox");
    epsilon_group_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto *epsilon_layout = new QVBoxLayout(epsilon_group_);
    epsilon_layout->setContentsMargins(1, 0, 1, 1);
    epsilon_layout->setSpacing(0);

    auto *epsilonTitleBar = new QWidget(epsilon_group_);
    epsilonTitleBar->setObjectName("sectionTitleBar");
    epsilonTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *epsilonTitleLayout = new QHBoxLayout(epsilonTitleBar);
    epsilonTitleLayout->setContentsMargins(8, 2, 8, 2);
    epsilonTitleLayout->setSpacing(8);

    epsilon_inline_title_lbl_ = new QLabel(epsilonTitleBar);
    epsilon_inline_title_lbl_->setObjectName("sectionTitleLabel");
    epsilon_inline_title_lbl_->setFixedHeight(kMainPageButtonHeight);
    epsilon_inline_title_lbl_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    epsilonTitleLayout->addWidget(epsilon_inline_title_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);

    auto *epsilonRateTitleLabel = new QLabel(epsilonTitleBar);
    epsilonRateTitleLabel->setObjectName(QStringLiteral("rateLabel"));
    epsilonRateTitleLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    epsilonRateTitleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    epsilonRateTitleLabel->setWordWrap(false);
    epsilonRateTitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    epsilonRateTitleLabel->setFixedHeight(kMainPageButtonHeight);
    epsilonTitleLayout->addWidget(epsilonRateTitleLabel, 1, Qt::AlignVCenter);

    epsilon_layout->addWidget(epsilonTitleBar);
    epsilon_panel_ = new EpsilonPanel(epsilonRateTitleLabel, this);
    epsilon_layout->addWidget(epsilon_panel_);
    sensor_layout->addWidget(epsilon_group_, 0, Qt::AlignLeft | Qt::AlignTop);

    gnss_group_ = nullptr;
    imu_group_ = nullptr;
    gnss_panel_ = nullptr;
    imu_panel_ = nullptr;
    gnss_inline_title_lbl_ = nullptr;
    imu_inline_title_lbl_ = nullptr;

    auto *env_group = new QGroupBox(this);
    env_group->setObjectName("sensorGroupBox");
    env_group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *env_layout = new QVBoxLayout(env_group);
    env_layout->setContentsMargins(1, 0, 1, 1);
    env_layout->setSpacing(0);

    auto *envTitleBar = new QWidget(env_group);
    envTitleBar->setObjectName("sectionTitleBar");
    envTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *envTitleLayout = new QHBoxLayout(envTitleBar);
    envTitleLayout->setContentsMargins(8, 2, 8, 2);
    envTitleLayout->setSpacing(8);

    env_inline_title_lbl_ = new QLabel(envTitleBar);
    env_inline_title_lbl_->setObjectName("sectionTitleLabel");
    env_inline_title_lbl_->setFixedHeight(kMainPageButtonHeight);
    env_inline_title_lbl_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    envTitleLayout->addWidget(env_inline_title_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    envTitleLayout->addStretch(1);

    auto createStatusIcon = [envTitleBar]() {
        auto *label = new QLabel(envTitleBar);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignCenter);
        label->setFixedSize(kEnvStatusIconSize + 4, kEnvStatusIconSize + 4);
        return label;
    };
    env_lidar_status_icon_ = createStatusIcon();
    env_ptb_status_icon_ = createStatusIcon();
    env_hmp_status_icon_ = createStatusIcon();
    envTitleLayout->addWidget(env_lidar_status_icon_, 0, Qt::AlignVCenter);
    envTitleLayout->addWidget(env_ptb_status_icon_, 0, Qt::AlignVCenter);
    envTitleLayout->addWidget(env_hmp_status_icon_, 0, Qt::AlignVCenter);
    env_layout->addWidget(envTitleBar);

    lidar_panel_ = new LidarPanel(this);
    env_layout->addWidget(lidar_panel_);

    ptb_panel_ = new PtbPanel(this);
    env_layout->addWidget(ptb_panel_);

    hmp_panel_ = new HmpPanel(this);
    env_layout->addWidget(hmp_panel_);
    updateEnvironmentStatusIcons(false, false, false);

    const int sensorCardHeight = std::max({
        epsilon_group_->sizeHint().height(),
        epsilon_group_->minimumSizeHint().height()
    });
    epsilon_group_->setFixedHeight(sensorCardHeight);
    env_group->setFixedHeight(sensorCardHeight);
    sensor_row->setMinimumHeight(sensorCardHeight);

    sensor_layout->addWidget(env_group, 1);

    data_layout->addWidget(sensor_row, 0);
    data_layout->addStretch(1);
    const int dataCardMinHeight = data_group_->minimumSizeHint().height();
    data_group_->setMinimumHeight(dataCardMinHeight);
    data_group_->setFixedHeight(dataCardMinHeight);
    env_group_ = env_group;

    lidar_group_ = nullptr;
    ptb_group_ = nullptr;
    hmp_group_ = nullptr;

    main_layout_->addWidget(new MainCardResizeHandle(config_group_, kConfigCardMinHeight, this), 0);
    main_layout_->addWidget(data_group_, 0);

    tcp_wave_group_ = new QGroupBox(this);
    tcp_wave_group_->setObjectName("sensorGroupBox");
    tcp_wave_group_->setMinimumHeight(kTcpWaveCardMinHeight);
    tcp_wave_group_->setFixedHeight(kTcpWaveCardMinHeight);
    tcp_wave_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *tcpWaveLayout = new QVBoxLayout(tcp_wave_group_);
    tcpWaveLayout->setContentsMargins(0, 0, 0, 0);
    tcpWaveLayout->setSpacing(0);

    tcp_wave_panel_ = new TcpWavePanel(this);
    tcp_wave_panel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(tcp_wave_panel_, &TcpWavePanel::rawWaveFrameReady,
            this, &MainWindow::onTcpRawWaveFrameReady);
    connect(tcp_wave_panel_, &TcpWavePanel::logMessageRequested,
            this, &MainWindow::log);
    connect(tcp_wave_panel_, &TcpWavePanel::connectionStateChanged, this, [this](bool) {
        updateRecordingActionStates();
    });
    connect(tcp_wave_panel_, &TcpWavePanel::remoteWaveTcpConnectionRequested, this, [this](bool connectRequested) {
        remote_wave_stream_requested_ = false;
        remote_wave_stream_auto_start_ = connectRequested;
        if (ground_telemetry_service_ && ground_telemetry_service_->isOpen())
        {
            if (connectRequested)
            {
                remote_wave_stream_enable_pending_ = true;
                ground_telemetry_service_->sendCommand(VaporView::CommandId::EnableWaveformStreaming);
            }
            else
            {
                remote_wave_stream_enable_pending_ = false;
                ground_telemetry_service_->sendCommand(VaporView::CommandId::DisableWaveformStreaming);
            }
        }
        if (!connectRequested && tcp_wave_panel_)
        {
            remote_device_states_.insert(VaporView::SkyDeviceId::WaveTcp, VaporView::DeviceState::Disconnected);
            remote_last_data_ms_.remove(VaporView::SkyDeviceId::WaveTcp);
            tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
            updateRemoteTelemetrySummaryLabel();
        }
        sendRemoteDeviceCommand(connectRequested ? VaporView::CommandId::ConnectDevice : VaporView::CommandId::DisconnectDevice,
                                VaporView::SkyDeviceId::WaveTcp);
    });
    connect(tcp_wave_panel_, &TcpWavePanel::remotePeakSearchRangeRequested,
            this, &MainWindow::sendRemotePeakSearchRange);
    tcpWaveLayout->addWidget(tcp_wave_panel_);
    main_layout_->addWidget(new MainCardResizeHandle(data_group_, dataCardMinHeight, this), 0);
    main_layout_->addWidget(tcp_wave_group_, 0);
    main_layout_->addStretch(1);
}

void MainWindow::setupLogPanel()
{
    log_side_panel_ = new QWidget(this);
    log_side_panel_->setObjectName(QStringLiteral("logSidePanel"));
    log_side_panel_->setAttribute(Qt::WA_StyledBackground, true);
    log_side_panel_->setAutoFillBackground(true);
    log_side_panel_->setMinimumWidth(120);
    auto *logSideLayout = new QVBoxLayout(log_side_panel_);
    logSideLayout->setContentsMargins(0, 0, 0, 0);
    logSideLayout->setSpacing(8);

    recording_status_card_ = new QFrame(log_side_panel_);
    recording_status_card_->setObjectName(QStringLiteral("recordingStatusCard"));
    recording_status_card_->setFrameShape(QFrame::NoFrame);
    recording_status_card_->setAttribute(Qt::WA_StyledBackground, true);
    recording_status_card_->setAutoFillBackground(true);
    recording_status_card_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *recordingCardLayout = new QVBoxLayout(recording_status_card_);
    recordingCardLayout->setContentsMargins(1, 1, 1, 1);
    recordingCardLayout->setSpacing(0);

    auto *recordingTitleBar = new QWidget(recording_status_card_);
    recordingTitleBar->setObjectName("sectionTitleBar");
    recordingTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *recordingTitleLayout = new QHBoxLayout(recordingTitleBar);
    recordingTitleLayout->setContentsMargins(8, 2, 8, 2);
    recordingTitleLayout->setSpacing(8);

    recording_status_title_lbl_ = new QLabel(recordingTitleBar);
    recording_status_title_lbl_->setObjectName("sectionTitleLabel");
    recording_status_title_lbl_->setFixedHeight(kMainPageButtonHeight);
    recording_status_title_lbl_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    recordingTitleLayout->addWidget(recording_status_title_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    recordingTitleLayout->addStretch(1);
    recordingCardLayout->addWidget(recordingTitleBar);

    auto *recordingBody = new QWidget(recording_status_card_);
    recordingBody->setObjectName(QStringLiteral("recordingStatusBody"));
    recordingBody->setAttribute(Qt::WA_StyledBackground, true);
    recordingBody->setAutoFillBackground(true);
    auto *recordingStatusLayout = new QHBoxLayout(recordingBody);
    recordingStatusLayout->setContentsMargins(10, 8, 10, 8);
    recordingStatusLayout->setSpacing(0);

    recording_status_label_ = new QLabel(recordingBody);
    recording_status_label_->setObjectName(QStringLiteral("recordingStatusLabel"));
    recording_status_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    recording_status_label_->setWordWrap(true);
    recording_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    recording_status_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    recordingStatusLayout->addWidget(recording_status_label_);
    recordingCardLayout->addWidget(recordingBody);
    logSideLayout->addWidget(recording_status_card_, 0);

    log_group_ = new QFrame(log_side_panel_);
    log_group_->setObjectName("logPanelFrame");
    log_group_->setFrameShape(QFrame::NoFrame);
    log_group_->setAttribute(Qt::WA_StyledBackground, true);
    log_group_->setAutoFillBackground(true);
    log_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *log_layout = new QVBoxLayout(log_group_);
    log_layout->setContentsMargins(1, 1, 1, 1);
    log_layout->setSpacing(0);

    auto *logTitleBar = new QWidget(log_group_);
    logTitleBar->setObjectName("sectionTitleBar");
    logTitleBar->setFixedHeight(kMainPageTitleBarHeight);
    auto *logTitleLayout = new QHBoxLayout(logTitleBar);
    logTitleLayout->setContentsMargins(8, 2, 8, 2);
    logTitleLayout->setSpacing(8);

    log_inline_title_lbl_ = new QLabel(logTitleBar);
    log_inline_title_lbl_->setObjectName("sectionTitleLabel");
    log_inline_title_lbl_->setFixedHeight(kMainPageButtonHeight);
    log_inline_title_lbl_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    logTitleLayout->addWidget(log_inline_title_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    logTitleLayout->addStretch(1);
    log_clear_btn_ = createTitleBarActionButton(clear_log_action_, logTitleBar);
    log_clear_btn_->setFixedSize(kMainPageButtonHeight, kMainPageButtonHeight);
    log_clear_btn_->setIconSize(QSize(kMainPageButtonHeight - 12, kMainPageButtonHeight - 12));
    logTitleLayout->addWidget(log_clear_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    log_layout->addWidget(logTitleBar);

    log_text_edit_ = new QTextEdit(log_group_);
    log_text_edit_->setObjectName(QStringLiteral("logTextEdit"));
    log_text_edit_->viewport()->setObjectName(QStringLiteral("logTextViewport"));
    log_text_edit_->setFrameShape(QFrame::NoFrame);
    log_text_edit_->setLineWidth(0);
    log_text_edit_->setAutoFillBackground(false);
    log_text_edit_->viewport()->setAutoFillBackground(false);
    log_text_edit_->setReadOnly(true);
    log_text_edit_->setMinimumWidth(100);
    log_layout->addWidget(log_text_edit_);
    logSideLayout->addWidget(log_group_, 1);
}

void MainWindow::setEnglish(bool english)
{
    is_english_ = english;

    if (data_menu_)
    {
        data_menu_->setTitle(english ? "&Data" : "数据(&D)");
    }
    recording_directory_action_->setText(english ? "Recording Folder..." : "记录目录...");
    if (recording_rate_menu_)
    {
        recording_rate_menu_->setTitle(english ? "Record Rates" : "记录频率");
        rebuildRecordingRateMenu();
    }
    if (devices_menu_)
    {
        devices_menu_->setTitle(english ? "&Devices" : "设备(&E)");
    }
    if (epsilon_packet_rates_action_)
    {
        epsilon_packet_rates_action_->setText(english ? "EPSILON Packet Rates..." : "设置EPSILON包频率...");
    }
    if (epsilon_rtcm_port_action_)
    {
        epsilon_rtcm_port_action_->setText(english ? "Configure EPSILON RTCM Port..." : "配置EPSILON RTCM串口...");
    }
    if (epsilon_reconfigure_action_)
    {
        epsilon_reconfigure_action_->setText(english ? "Reconfigure EPSILON Output..." : "重新配置EPSILON输出...");
    }
    session_viewer_action_->setText(english ? "Data Viewer..." : "数据查看器...");
    exit_action_->setText(english ? "E&xit" : "退出(&X)");

    if (font_menu_)
    {
        font_menu_->setTitle(english ? "Font &Size" : "字号(&S)");
    }
    font_tiny_action_->setText(english ? "Tiny (70%)" : "超小 (70%)");
    font_extra_small_action_->setText(english ? "Extra Small (80%)" : "特小 (80%)");
    font_small_action_->setText(english ? "Small (90%)" : "小号 (90%)");
    font_normal_action_->setText(english ? "Normal (100%)" : "标准 (100%)");
    font_large_action_->setText(english ? "Large (115%)" : "大号 (115%)");
    font_extra_large_action_->setText(english ? "Extra Large (130%)" : "超大 (130%)");

    if (language_menu_)
    {
        language_menu_->setTitle(english ? "&Language" : "语言(&L)");
    }
    lang_action_->setText(english ? "Switch to Chinese" : "切换到英文");
    lang_action_->setToolTip(english ? "Switch to Chinese" : "切换到英文");
    lang_action_->setStatusTip(english ? "Switch interface language" : "切换界面语言");
    updateThemeAction();
    updateCustomTitleBarTexts();
    discardTitleApplicationMenuPanel();

    if (help_menu_)
    {
        help_menu_->setTitle(english ? "&Help" : "帮助(&H)");
    }
    about_action_->setText(english ? "&About" : "关于(&A)");

    refresh_ports_btn_->setText(english ? "Refresh" : "刷新");
    refresh_ports_btn_->setToolTip(english ? "Refresh ports" : "刷新串口");
    refresh_ports_btn_->setStatusTip(refresh_ports_btn_->toolTip());
    connect_btn_->setText(english ? "Connect" : "连接");
    connect_btn_->setToolTip(english ? "Connect" : "连接");
    connect_btn_->setStatusTip(connect_btn_->toolTip());
    cancel_connect_btn_->setText(english ? "Cancel" : "取消");
    cancel_connect_btn_->setToolTip(english ? "Cancel connection" : "取消连接");
    cancel_connect_btn_->setStatusTip(cancel_connect_btn_->toolTip());
    disconnect_btn_->setText(english ? "Disconnect" : "断开");
    disconnect_btn_->setToolTip(english ? "Disconnect" : "断开连接");
    disconnect_btn_->setStatusTip(disconnect_btn_->toolTip());
    start_recording_btn_->setText(english ? "Start Recording" : "开始记录");
    start_recording_btn_->setToolTip(english ? "Start recording" : "开始记录");
    start_recording_btn_->setStatusTip(start_recording_btn_->toolTip());
    pause_recording_btn_->setText(english ? "Pause Recording" : "暂停记录");
    pause_recording_btn_->setToolTip(english ? "Pause recording" : "暂停记录");
    pause_recording_btn_->setStatusTip(pause_recording_btn_->toolTip());
    stop_recording_btn_->setText(english ? "Stop Recording" : "结束记录");
    stop_recording_btn_->setToolTip(english ? "Stop recording" : "结束记录");
    stop_recording_btn_->setStatusTip(stop_recording_btn_->toolTip());
    clear_log_action_->setText(english ? "Clear Log" : "清空日志");
    clear_log_action_->setToolTip(english ? "Clear Log" : "清空日志");
    clear_log_action_->setStatusTip(english ? "Clear Log" : "清空日志");
    rtk_config_action_->setText(english ? "RTK Config" : "RTK配置");
    rtk_config_action_->setToolTip(english ? "RTK config" : "RTK配置");
    rtk_config_action_->setStatusTip(rtk_config_action_->toolTip());
    session_viewer_action_->setToolTip(english ? "Data viewer" : "数据查看器");
    session_viewer_action_->setStatusTip(session_viewer_action_->toolTip());

    status_label_->setText(english ? "Ready" : "就绪");

    config_group_->setTitle(QString());
    data_group_->setTitle(QString());
    tcp_wave_group_->setTitle(QString());

    if (epsilon_group_) epsilon_group_->setTitle(QString());
    if (gnss_group_) gnss_group_->setTitle(QString());
    if (imu_group_) imu_group_->setTitle(QString());
    if (env_group_) env_group_->setTitle(QString());

    if (epsilon_lbl_) epsilon_lbl_->setText(english ? "EPSILON:" : "EPSILON:");
    if (gnss_lbl_) gnss_lbl_->setText(english ? "GNSS:" : "GNSS:");
    if (imu_lbl_) imu_lbl_->setText(english ? "IMU:" : "IMU:");
    if (ptb_lbl_) ptb_lbl_->setText(english ? "PTB210:" : "PTB210:");
    if (hmp_lbl_) hmp_lbl_->setText(english ? "HMP3:" : "HMP3:");
    if (lidar_lbl_) lidar_lbl_->setText(english ? "TFA1500-L:" : "TFA1500-L:");

    if (config_inline_title_lbl_)
    {
        config_inline_title_lbl_->setText(english ? "Serial Port Configuration" : "串口配置");
    }
    if (data_source_mode_lbl_) data_source_mode_lbl_->setText(english ? "Source:" : "数据源:");
    if (sky_telemetry_port_lbl_) sky_telemetry_port_lbl_->setText(english ? "Sky Telemetry Port:" : "天空端数传串口:");
    if (sky_telemetry_baud_lbl_) sky_telemetry_baud_lbl_->setText(QStringLiteral("--"));
    if (sky_device_config_btn_) sky_device_config_btn_->setText(english ? "Sky Device Config" : "天空端设备配置");
    if (data_source_mode_combo_)
    {
        const QSignalBlocker blocker(data_source_mode_combo_);
        data_source_mode_combo_->setItemText(0, sourceModeDisplayText(english, 0));
        data_source_mode_combo_->setItemText(1, sourceModeDisplayText(english, 1));
    }
    if (auto_detect_ports_btn_)
    {
        auto_detect_ports_btn_->setText(port_detection_in_progress_
            ? (english ? "Cancel Auto Detect" : "取消自动识别")
            : (english ? "Auto Detect Ports" : "自动识别串口"));
        auto_detect_ports_btn_->setToolTip(port_detection_in_progress_
            ? (english ? "Stop the current serial-port detection task." : "停止当前串口自动识别任务。")
            : (english ? "Probe available serial ports and automatically assign detected devices."
                       : "扫描可用串口，并将识别出的设备自动填入对应端口。"));
    }
    if (log_inline_title_lbl_)
    {
        log_inline_title_lbl_->setText(english ? "Log" : "日志");
    }
    if (recording_status_title_lbl_)
    {
        recording_status_title_lbl_->setText(english ? "Recording Status" : "记录状态");
    }
    if (epsilon_inline_title_lbl_)
    {
        epsilon_inline_title_lbl_->setText(english ? "EPSILON Integrated Navigation" : "EPSILON组合导航");
    }
    if (gnss_inline_title_lbl_)
    {
        gnss_inline_title_lbl_->setText(english ? "GNSS / RTK" : "GNSS / RTK");
    }
    if (imu_inline_title_lbl_)
    {
        imu_inline_title_lbl_->setText(english ? "IMU" : "IMU");
    }
    if (env_inline_title_lbl_)
    {
        env_inline_title_lbl_->setText(english ? "Environment / Range" : "环境与测距");
    }
    if (global_rate_lbl_) global_rate_lbl_->setText(english ? "Global Rate:" : "统一频率:");
    if (epsilon_rate_lbl_) epsilon_rate_lbl_->setText(english ? "Packets:" : "包频率:");
    if (epsilon_packet_rates_btn_)
    {
        epsilon_packet_rates_btn_->setText(english ? "Packet Rates..." : "配置EPSILON包频率...");
        epsilon_packet_rates_btn_->setToolTip(english
            ? "Configure EPSILON packet output rates"
            : "配置 EPSILON 各数据包输出频率");
    }
    if (gnss_rate_lbl_) gnss_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (imu_rate_lbl_) imu_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (imu_apply_btn_)
    {
        imu_apply_btn_->setText(english ? "Apply IMU" : "应用IMU");
        imu_apply_btn_->setToolTip(english ? "Apply the selected IMU format, baud rate, and output frequency" : "应用当前选择的 IMU 输出格式、波特率和输出频率");
    }
    if (imu_hi91_btn_)
    {
        imu_hi91_btn_->setToolTip(english ? "Switch IMU output to HI91 immediately" : "立即切换 IMU 输出为 HI91");
    }
    if (imu_hi92_btn_)
    {
        imu_hi92_btn_->setToolTip(english ? "Switch IMU output to HI92 immediately" : "立即切换 IMU 输出为 HI92");
    }
    if (imu_baud_115200_btn_)
    {
        imu_baud_115200_btn_->setToolTip(english ? "Switch IMU baud rate to 115200" : "一键切换 IMU 波特率到 115200");
    }
    if (imu_baud_921600_btn_)
    {
        imu_baud_921600_btn_->setToolTip(english ? "Switch IMU baud rate to 921600" : "一键切换 IMU 波特率到 921600");
    }
    if (imu_rate_100_btn_)
    {
        imu_rate_100_btn_->setToolTip(english ? "Switch IMU output frequency to 100 Hz" : "一键切换 IMU 输出频率到 100 Hz");
    }
    if (imu_rate_200_btn_)
    {
        imu_rate_200_btn_->setToolTip(english ? "Switch IMU output frequency to 200 Hz" : "一键切换 IMU 输出频率到 200 Hz");
    }
    if (imu_rate_500_btn_)
    {
        imu_rate_500_btn_->setToolTip(english ? "Switch IMU output frequency to 500 Hz" : "一键切换 IMU 输出频率到 500 Hz");
    }
    if (imu_rate_1000_btn_)
    {
        imu_rate_1000_btn_->setToolTip(english ? "Switch IMU output frequency to 1000 Hz" : "一键切换 IMU 输出频率到 1000 Hz");
    }
    ptb_rate_lbl_->setText(english ? "Rate:" : "频率:");
    hmp_rate_lbl_->setText(english ? "Rate:" : "频率:");
    lidar_rate_lbl_->setText(english ? "Rate:" : "频率:");
    for (QComboBox *combo : {ptb_rate_combo_, hmp_rate_combo_, lidar_rate_combo_})
    {
        if (!combo)
        {
            continue;
        }
        const QSignalBlocker blocker(combo);
        const QString oldText = english ? QStringLiteral("不设定") : QStringLiteral("No Set");
        const QString newText = english ? QStringLiteral("No Set") : QStringLiteral("不设定");
        const int idx = combo->findText(oldText);
        if (idx >= 0)
        {
            combo->setItemText(idx, newText);
        }
        else if (combo->findText(newText) < 0)
        {
            combo->addItem(newText);
        }
        if (isRateUnspecified(combo->currentText()))
        {
            combo->setCurrentText(newText);
        }
    }

    if (epsilon_panel_) epsilon_panel_->setEnglish(english);
    if (gnss_panel_) gnss_panel_->setEnglish(english);
    if (imu_panel_) imu_panel_->setEnglish(english);
    if (ptb_panel_) ptb_panel_->setEnglish(english);
    if (hmp_panel_) hmp_panel_->setEnglish(english);
    if (lidar_panel_) lidar_panel_->setEnglish(english);
    if (tcp_wave_panel_) tcp_wave_panel_->setEnglish(english);
    if (sky_device_config_dialog_) sky_device_config_dialog_->setEnglish(english);

    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.epsilon) collectors.epsilon->setEnglish(english);
    if (collectors.gnss) collectors.gnss->setEnglish(english);
    if (collectors.imu) collectors.imu->setEnglish(english);
    if (collectors.ptb) collectors.ptb->setEnglish(english);
    if (collectors.hmp) collectors.hmp->setEnglish(english);
    if (collectors.lidar) collectors.lidar->setEnglish(english);

    if (rtk_config_dialog_)
    {
        rtk_config_dialog_->setEnglish(english);
    }
    if (session_viewer_window_)
    {
        session_viewer_window_->setEnglish(english);
    }

    if (isRemoteSkyMode())
    {
        refreshRemoteSkyDataUi();
    }
    else
    {
        updateEnvironmentStatusIcons(current_lidar_.valid, current_ptb_.valid, current_hmp_.valid);
    }
    updateSourceModeUi();
    updateRecordingStatusLabel();
}

void MainWindow::onOpenSessionViewerClicked()
{
    showBusyStatusTaskProgress(is_english_ ? "Opening Data Viewer..." : "正在打开数据查看器...");
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    if (!session_viewer_window_)
    {
        session_viewer_window_ = new SessionViewerWindow();
        session_viewer_window_->setAttribute(Qt::WA_DeleteOnClose, true);
        connect(session_viewer_window_, &QObject::destroyed, this, [this]() {
            session_viewer_window_ = nullptr;
        });
        session_viewer_window_->setEnglish(is_english_);
    }

    session_viewer_window_->setDefaultDataDirectory(
        recording_directory_.isEmpty() ? defaultRecordingDirectory() : recording_directory_);

    VaporView::centerWindowOnScreen(session_viewer_window_, this);
    session_viewer_window_->show();
    session_viewer_window_->raise();
    session_viewer_window_->activateWindow();
    hideStatusTaskProgress();
}

void MainWindow::onSwitchLanguage()
{
    is_english_ = !is_english_;
    setEnglish(is_english_);
    log(is_english_ ? "Language switched to English" : "语言已切换为中文");
}

void MainWindow::showAboutDialog()
{
    const QString title = is_english_ ? QStringLiteral("About VaporView") : QStringLiteral("关于 VaporView");
    const QString text = is_english_
        ? QStringLiteral(
              "VaporView Application\n\n"
              "Version 1.0.0\n\n"
              "Integrated navigation and environment monitoring system.\n\n"
              "Supported devices:\n"
              "- EPSILON Integrated Navigation (FDILink)\n"
              "- PTB210 Barometer\n"
              "- HMP3 Temperature/Humidity Sensor\n"
              "- TFA1500-L Laser Rangefinder")
        : QStringLiteral(
              "VaporView 应用程序\n\n"
              "版本 1.0.0\n\n"
              "组合导航与环境监控系统。\n\n"
              "支持的设备:\n"
              "- EPSILON 组合导航一体机 (FDILink)\n"
              "- PTB210 气压计\n"
              "- HMP3 温湿度传感器\n"
              "- TFA1500-L 激光测距模块");
    QMessageBox::about(this, title, text);
}

void MainWindow::updateThemeAction()
{
    if (!theme_toggle_action_)
    {
        return;
    }

    const bool targetLight = dark_theme_enabled_;
    theme_toggle_action_->setIcon(targetLight ? createLightThemeIcon() : createDarkThemeIcon());
    theme_toggle_action_->setText(targetLight
        ? (is_english_ ? "Light Theme" : "亮色模式")
        : (is_english_ ? "Dark Theme" : "暗色模式"));
    theme_toggle_action_->setToolTip(targetLight
        ? (is_english_ ? "Switch to light theme" : "切换到亮色模式")
        : (is_english_ ? "Switch to dark theme" : "切换到暗色模式"));
    theme_toggle_action_->setStatusTip(theme_toggle_action_->toolTip());
}

void MainWindow::updateCustomTitleBarTexts()
{
    if (custom_title_label_)
    {
        custom_title_label_->setText(QStringLiteral("VaporView"));
    }
    if (title_menu_btn_)
    {
        title_menu_btn_->setToolTip(is_english_ ? "Menu" : "菜单");
        title_menu_btn_->setStatusTip(title_menu_btn_->toolTip());
    }
    if (window_minimize_btn_)
    {
        window_minimize_btn_->setToolTip(is_english_ ? "Minimize" : "最小化");
        window_minimize_btn_->setStatusTip(window_minimize_btn_->toolTip());
    }
    if (window_close_btn_)
    {
        window_close_btn_->setToolTip(is_english_ ? "Close" : "关闭");
        window_close_btn_->setStatusTip(window_close_btn_->toolTip());
    }
    updateWindowControlButtons();
}

void MainWindow::updateCustomTitleBarStyle()
{
    if (!custom_title_bar_)
    {
        return;
    }

    custom_title_bar_->setFixedHeight(scalePixels(48));
    const QSize actionButtonSize(scalePixels(34), scalePixels(34));
    const QSize windowButtonSize(scalePixels(34), scalePixels(34));
    const QSize iconSize(scalePixels(24), scalePixels(24));
    const QSize maximizeIconSize(scalePixels(21), scalePixels(21));

    const auto buttons = custom_title_bar_->findChildren<QToolButton *>();
    for (QToolButton *button : buttons)
    {
        if (!button)
        {
            continue;
        }
        const bool windowButton = button == window_minimize_btn_ ||
                                  button == window_maximize_btn_ ||
                                  button == window_close_btn_;
        button->setFixedSize(windowButton ? windowButtonSize : actionButtonSize);
        button->setIconSize(iconSize);
    }

    if (window_maximize_btn_)
    {
        window_maximize_btn_->setIconSize(maximizeIconSize);
    }
    if (log_clear_btn_)
    {
        log_clear_btn_->setFixedSize(actionButtonSize);
        log_clear_btn_->setIconSize(iconSize);
    }
    if (custom_logo_label_)
    {
        const int logoSize = scalePixels(44);
        custom_logo_label_->setFixedSize(logoSize, logoSize);
        custom_logo_label_->setPixmap(renderVaporViewLogo(dark_theme_enabled_, logoSize, custom_logo_label_->devicePixelRatioF()));
    }
    const QIcon logoIcon = createVaporViewLogoIcon(dark_theme_enabled_);
    if (!logoIcon.isNull())
    {
        setWindowIcon(logoIcon);
        qApp->setWindowIcon(logoIcon);
    }

    if (title_menu_btn_)
    {
        title_menu_btn_->setIcon(createTitleBarIcon(QStringLiteral("menu"), dark_theme_enabled_));
    }
    if (title_application_panel_)
    {
        title_application_panel_->hide();
        title_application_panel_->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_));
    }
    if (title_application_sub_panel_)
    {
        title_application_sub_panel_->hide();
        title_application_sub_panel_->setStyleSheet(titleApplicationPanelStyleSheet(dark_theme_enabled_));
    }
    if (window_minimize_btn_)
    {
        window_minimize_btn_->setIcon(createTitleBarIcon(QStringLiteral("minus"), dark_theme_enabled_));
    }
    if (window_close_btn_)
    {
        window_close_btn_->setIcon(createTitleBarIcon(QStringLiteral("x"), dark_theme_enabled_));
    }
    updateWindowControlButtons();
}

void MainWindow::updateWindowControlButtons()
{
    if (!window_maximize_btn_)
    {
        return;
    }

    const bool shouldRestore = isWindowMaximizedForUi();
    window_maximize_btn_->setIcon(createTitleBarIcon(shouldRestore ? QStringLiteral("copy") : QStringLiteral("square"),
                                                     dark_theme_enabled_));
    window_maximize_btn_->setToolTip(shouldRestore
        ? (is_english_ ? "Restore" : "还原")
        : (is_english_ ? "Maximize" : "最大化"));
    window_maximize_btn_->setStatusTip(window_maximize_btn_->toolTip());
}

void MainWindow::toggleWindowMaximized()
{
    if (isFullScreen())
    {
        return;
    }

    if (isWindowMaximizedForUi())
    {
        const QRect restoreGeometry = normal_window_geometry_.isValid()
            ? normal_window_geometry_
            : fallbackNormalWindowGeometry();
        setWindowState(windowState() & ~Qt::WindowMaximized);
        showNormal();
        if (restoreGeometry.isValid())
        {
            setGeometry(restoreGeometry);
        }
    }
    else
    {
        rememberNormalWindowGeometry();
        showMaximized();
    }

    updateWindowControlButtons();
    updateWindowBorderFrames();
    updateWindowResizeHandles();
    QTimer::singleShot(0, this, &MainWindow::updateWindowControlButtons);
    QTimer::singleShot(60, this, &MainWindow::updateWindowControlButtons);
}

bool MainWindow::isWindowMaximizedForUi() const
{
    if (isMaximized() || windowState().testFlag(Qt::WindowMaximized))
    {
        return true;
    }
    if (isFullScreen())
    {
        return false;
    }

    const QRect availableGeometry = currentScreenAvailableGeometry();
    if (!availableGeometry.isValid())
    {
        return false;
    }

    const int tolerance = std::max(3, scalePixels(3));
    auto coversAvailableGeometry = [&](const QRect& rect) {
        return rect.isValid() &&
               rect.left() <= availableGeometry.left() + tolerance &&
               rect.top() <= availableGeometry.top() + tolerance &&
               rect.right() >= availableGeometry.right() - tolerance &&
               rect.bottom() >= availableGeometry.bottom() - tolerance &&
               rect.width() >= availableGeometry.width() - tolerance &&
               rect.height() >= availableGeometry.height() - tolerance;
    };

    return coversAvailableGeometry(frameGeometry()) || coversAvailableGeometry(geometry());
}

void MainWindow::rememberNormalWindowGeometry()
{
    if (isFullScreen() || isMaximized() || windowState().testFlag(Qt::WindowMaximized))
    {
        return;
    }

    const QRect currentGeometry = geometry();
    if (!currentGeometry.isValid() || currentGeometry.width() <= 0 || currentGeometry.height() <= 0)
    {
        return;
    }

    const QRect availableGeometry = currentScreenAvailableGeometry();
    if (availableGeometry.isValid())
    {
        const int tolerance = std::max(3, scalePixels(3));
        const bool visuallyMaximized =
            currentGeometry.left() <= availableGeometry.left() + tolerance &&
            currentGeometry.top() <= availableGeometry.top() + tolerance &&
            currentGeometry.right() >= availableGeometry.right() - tolerance &&
            currentGeometry.bottom() >= availableGeometry.bottom() - tolerance &&
            currentGeometry.width() >= availableGeometry.width() - tolerance &&
            currentGeometry.height() >= availableGeometry.height() - tolerance;
        if (visuallyMaximized)
        {
            return;
        }
    }

    normal_window_geometry_ = currentGeometry;
}

QRect MainWindow::fallbackNormalWindowGeometry() const
{
    const QRect availableGeometry = currentScreenAvailableGeometry();
    const QSize minimumSize = this->minimumSize().expandedTo(minimumSizeHint());
    QSize targetSize = base_window_size_.expandedTo(minimumSize);
    if (availableGeometry.isValid())
    {
        targetSize = targetSize.boundedTo(availableGeometry.size()).expandedTo(minimumSize.boundedTo(availableGeometry.size()));
        const QPoint topLeft(
            availableGeometry.left() + std::max(0, (availableGeometry.width() - targetSize.width()) / 2),
            availableGeometry.top() + std::max(0, (availableGeometry.height() - targetSize.height()) / 2));
        return QRect(topLeft, targetSize);
    }
    return QRect(QPoint(80, 80), targetSize);
}

QRect MainWindow::currentScreenAvailableGeometry() const
{
    const QScreen *targetScreen = screen();
    if (!targetScreen && windowHandle())
    {
        targetScreen = windowHandle()->screen();
    }
    if (!targetScreen)
    {
        targetScreen = QGuiApplication::primaryScreen();
    }
    return targetScreen ? targetScreen->availableGeometry() : QRect();
}

void MainWindow::setupWindowBorderFrames()
{
    auto createBorder = [this]() {
        auto *border = new QFrame(this);
        border->setAttribute(Qt::WA_TransparentForMouseEvents);
        border->setFocusPolicy(Qt::NoFocus);
        border->setFrameShape(QFrame::NoFrame);
        border->setLineWidth(0);
        border->setAutoFillBackground(false);
        return border;
    };

    window_border_top_ = createBorder();
    window_border_right_ = createBorder();
    window_border_bottom_ = createBorder();
    window_border_left_ = createBorder();

    window_border_top_->hide();
    window_border_bottom_->setStyleSheet(QStringLiteral("background-color: #0C0C0C; border: none;"));
    const QString verticalBorderStyle = QStringLiteral("background-color: #0C0C0C; border: none;");
    window_border_left_->setStyleSheet(verticalBorderStyle);
    window_border_right_->setStyleSheet(verticalBorderStyle);

    updateWindowBorderFrames();
}

void MainWindow::updateWindowBorderFrames()
{
    const bool visible = !isFullScreen() && !isWindowMaximizedForUi();
    const int borderThickness = 1;
    if (window_border_top_)
    {
        window_border_top_->setVisible(false);
    }
    if (window_border_left_)
    {
        window_border_left_->setVisible(visible);
        window_border_left_->setGeometry(0, 0, borderThickness, height());
        window_border_left_->raise();
    }
    if (window_border_right_)
    {
        window_border_right_->setVisible(visible);
        window_border_right_->setGeometry(std::max(0, width() - borderThickness), 0, borderThickness, height());
        window_border_right_->raise();
    }
    if (window_border_bottom_)
    {
        window_border_bottom_->setVisible(visible);
        window_border_bottom_->setGeometry(0, std::max(0, height() - borderThickness), width(), borderThickness);
        window_border_bottom_->raise();
    }
}

void MainWindow::setupWindowResizeHandles()
{
    const QVector<Qt::Edges> edges = {
        Qt::TopEdge | Qt::LeftEdge,
        Qt::TopEdge,
        Qt::TopEdge | Qt::RightEdge,
        Qt::LeftEdge,
        Qt::RightEdge,
        Qt::BottomEdge | Qt::LeftEdge,
        Qt::BottomEdge,
        Qt::BottomEdge | Qt::RightEdge,
    };

    window_resize_handles_.reserve(edges.size());
    for (Qt::Edges edgeSet : edges)
    {
        auto *handle = new WindowResizeHandle(edgeSet, this);
        handle->setObjectName(QStringLiteral("windowResizeHandle"));
        window_resize_handles_.append(handle);
    }

    updateWindowResizeHandles();
}

void MainWindow::updateWindowResizeHandles()
{
    if (window_resize_handles_.size() != 8)
    {
        return;
    }

    const bool visible = !isFullScreen() && !isWindowMaximizedForUi();
    const int thickness = scalePixels(8);
    const int w = width();
    const int h = height();
    const int rightX = std::max(0, w - thickness);
    const int bottomY = std::max(0, h - thickness);

    const QVector<QRect> geometries = {
        QRect(0, 0, thickness, thickness),
        QRect(thickness, 0, std::max(0, w - thickness * 2), thickness),
        QRect(rightX, 0, thickness, thickness),
        QRect(0, thickness, thickness, std::max(0, h - thickness * 2)),
        QRect(rightX, thickness, thickness, std::max(0, h - thickness * 2)),
        QRect(0, bottomY, thickness, thickness),
        QRect(thickness, bottomY, std::max(0, w - thickness * 2), thickness),
        QRect(rightX, bottomY, thickness, thickness),
    };

    for (int i = 0; i < window_resize_handles_.size(); ++i)
    {
        QWidget *handle = window_resize_handles_.at(i);
        handle->setVisible(visible);
        handle->setGeometry(geometries.at(i));
        handle->raise();
    }

    updateWindowBorderFrames();
    if (title_application_panel_ && title_application_panel_->isVisible())
    {
        title_application_panel_->raise();
    }
    if (title_application_sub_panel_ && title_application_sub_panel_->isVisible())
    {
        title_application_sub_panel_->raise();
    }
}

void MainWindow::onToggleTheme()
{
    dark_theme_enabled_ = !dark_theme_enabled_;
    discardTitleApplicationMenuPanel();
    applyStyleConfiguration();
    updateThemeAction();

    QSettings settings("VaporView", "MainWindow");
    settings.setValue("dark_theme_enabled", dark_theme_enabled_);

    log(dark_theme_enabled_
        ? (is_english_ ? "Theme switched to dark" : "已切换为暗色模式")
        : (is_english_ ? "Theme switched to light" : "已切换为亮色模式"));
}

void MainWindow::onFontScaleTriggered(QAction *action)
{
    if (!action)
    {
        return;
    }

    const int percent = action->data().toInt();
    if (percent == font_scale_percent_)
    {
        return;
    }

    setFontScale(percent);
    log(QString(is_english_ ? "Font size set to %1%" : "字体大小已设置为 %1%").arg(percent));
}

int MainWindow::parseRate(const QString& text) const
{
    bool ok;
    int rate = text.toInt(&ok);
    if (ok && rate >= 1 && rate <= 1000)
    {
        return rate;
    }
    return 20;
}

bool MainWindow::isRateUnspecified(const QString& text) const
{
    const QString trimmed = text.trimmed();
    return trimmed.compare(QStringLiteral("No Set"), Qt::CaseInsensitive) == 0
        || trimmed == QStringLiteral("不设定");
}

int MainWindow::effectiveRateOrDefault(const QString& text, int defaultRate, int maxRate) const
{
    const int boundedDefault = std::clamp(defaultRate, 1, std::max(1, maxRate));
    if (isRateUnspecified(text))
    {
        return boundedDefault;
    }
    return std::clamp(parseRate(text), 1, std::max(1, maxRate));
}

void MainWindow::onGlobalRateChanged(const QString& text)
{
    int rate = parseRate(text);
    const bool skipEpsilonDeviceRate = epsilon_rate_combo_ && isRateUnspecified(epsilon_rate_combo_->currentText());
    const bool skipPtbDeviceRate = ptb_rate_combo_ && isRateUnspecified(ptb_rate_combo_->currentText());
    const bool skipHmpDeviceRate = hmp_rate_combo_ && isRateUnspecified(hmp_rate_combo_->currentText());
    const bool skipLidarDeviceRate = lidar_rate_combo_ && isRateUnspecified(lidar_rate_combo_->currentText());

    epsilon_sample_rate_ = skipEpsilonDeviceRate ? kDefaultEpsilonSampleRateHz : std::clamp(rate, 20, 200);
    ptb_sample_rate_ = skipPtbDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(rate);
    hmp_sample_rate_ = skipHmpDeviceRate ? kDefaultHmpSampleRateHz : rate;
    lidar_sample_rate_ = skipLidarDeviceRate ? kDefaultLidarSampleRateHz : std::min(rate, 100);

    if (epsilon_rate_combo_) epsilon_rate_combo_->blockSignals(true);
    if (ptb_rate_combo_) ptb_rate_combo_->blockSignals(true);
    if (hmp_rate_combo_) hmp_rate_combo_->blockSignals(true);
    if (lidar_rate_combo_) lidar_rate_combo_->blockSignals(true);

    if (epsilon_rate_combo_ && !skipEpsilonDeviceRate) epsilon_rate_combo_->setCurrentText(QString::number(epsilon_sample_rate_));
    if (ptb_rate_combo_ && !skipPtbDeviceRate) ptb_rate_combo_->setCurrentText(QString::number(ptb_sample_rate_));
    if (hmp_rate_combo_ && !skipHmpDeviceRate) hmp_rate_combo_->setCurrentText(text);
    if (lidar_rate_combo_ && !skipLidarDeviceRate) lidar_rate_combo_->setCurrentText(QString::number(lidar_sample_rate_));

    if (epsilon_rate_combo_) epsilon_rate_combo_->blockSignals(false);
    if (ptb_rate_combo_) ptb_rate_combo_->blockSignals(false);
    if (hmp_rate_combo_) hmp_rate_combo_->blockSignals(false);
    if (lidar_rate_combo_) lidar_rate_combo_->blockSignals(false);

    QSettings settings("VaporView", "MainWindow");
    bool epsilonUsesCustomPacketRates = false;
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, epsilon_sample_rate_, &epsilonUsesCustomPacketRates);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilon_sample_rate_);
    const CollectorSnapshot collectors = snapshotCollectors();

    if (collectors.epsilon && collectors.epsilon->isRunning())
    {
        collectors.epsilon->setSampleRate(epsilonCallbackRate);
        if (!skipEpsilonDeviceRate)
        {
            collectors.epsilon->setOutputPacketRates(epsilonDesiredPacketRates);
        }
    }
    if (collectors.ptb && collectors.ptb->isRunning())
    {
        collectors.ptb->setSampleRate(ptb_sample_rate_);
        if (!skipPtbDeviceRate && !collectors.ptb->setDeviceSampleRate(ptb_sample_rate_))
        {
            log(QString(is_english_
                ? "PTB sample rate command failed for %1 Hz"
                : "PTB采样频率命令下发失败：%1 Hz").arg(ptb_sample_rate_));
        }
    }
    if (collectors.hmp && collectors.hmp->isRunning())
    {
        collectors.hmp->setSampleRate(hmp_sample_rate_);
    }
    if (collectors.lidar && collectors.lidar->isRunning())
    {
        collectors.lidar->setSampleRate(lidar_sample_rate_);
        if (!skipLidarDeviceRate)
        {
            collectors.lidar->setDeviceSampleRate(lidar_sample_rate_);
        }
    }
    
    if (epsilonUsesCustomPacketRates)
    {
        log(QString(is_english_
                        ? "All rates set to %1 Hz; EPSILON keeps the saved custom packet-rate profile."
                        : "所有频率已设置为 %1 Hz；EPSILON 保持已保存的自定义包频率配置。")
                .arg(rate));
    }
    else
    {
        log(QString(is_english_ ? "All rates set to %1 Hz" : "所有频率已设置为 %1 Hz").arg(rate));
    }
    if (skipEpsilonDeviceRate || skipPtbDeviceRate || skipHmpDeviceRate || skipLidarDeviceRate)
    {
        log(is_english_
            ? "Devices set to No Set keep their output-rate commands disabled."
            : "已选择“不设定”的设备保持不下发输出频率命令。");
    }
    if (!skipPtbDeviceRate && ptb_sample_rate_ != rate)
    {
        log(QString(is_english_
            ? "PTB sample rate capped at %1 Hz"
            : "PTB采样频率已限制为 %1 Hz").arg(ptb_sample_rate_));
    }
}

void MainWindow::onGnssRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    epsilon_sample_rate_ = effectiveRateOrDefault(text, kDefaultEpsilonSampleRateHz, 200);
    QSettings settings("VaporView", "MainWindow");
    bool epsilonUsesCustomPacketRates = false;
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, epsilon_sample_rate_, &epsilonUsesCustomPacketRates);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilon_sample_rate_);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.epsilon)
    {
        collectors.epsilon->setSampleRate(epsilonCallbackRate);
        if (collectors.epsilon->isRunning() && !skipDeviceRate)
        {
            collectors.epsilon->setOutputPacketRates(epsilonDesiredPacketRates);
        }
    }
    if (skipDeviceRate)
    {
        log(is_english_
            ? "EPSILON output-rate command disabled; using the current device output."
            : "已禁用 EPSILON 输出频率下发，使用设备当前输出。");
    }
    else if (epsilonUsesCustomPacketRates)
    {
        log(QString(is_english_
                        ? "EPSILON grouped rate was set to %1 Hz, but the saved custom packet-rate profile remains active."
                        : "EPSILON 分组频率已设置为 %1 Hz，但当前仍启用已保存的自定义包频率配置。")
                .arg(epsilon_sample_rate_));
    }
    else
    {
        log(QString(is_english_ ? "EPSILON output rate set to %1 Hz" : "EPSILON 输出频率已设置为 %1 Hz").arg(epsilon_sample_rate_));
    }
}

void MainWindow::onImuRateChanged(const QString& text)
{
    Q_UNUSED(text);
}

void MainWindow::onPtbRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    const int requestedRate = parseRate(text);
    ptb_sample_rate_ = skipDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(requestedRate);
    if (skipDeviceRate)
    {
        const CollectorSnapshot collectors = snapshotCollectors();
        if (collectors.ptb)
        {
            collectors.ptb->setSampleRate(ptb_sample_rate_);
        }
        log(is_english_
            ? "PTB sample-rate command disabled; using the current device output."
            : "已禁用 PTB 采样频率下发，使用设备当前输出。");
        return;
    }

    if (ptb_rate_combo_ && ptb_rate_combo_->currentText() != QString::number(ptb_sample_rate_))
    {
        QSignalBlocker blocker(ptb_rate_combo_);
        ptb_rate_combo_->setCurrentText(QString::number(ptb_sample_rate_));
    }

    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.ptb)
    {
        collectors.ptb->setSampleRate(ptb_sample_rate_);
        if (collectors.ptb->isRunning())
        {
            if (!collectors.ptb->setDeviceSampleRate(ptb_sample_rate_))
            {
                log(QString(is_english_
                    ? "PTB sample rate command failed for %1 Hz"
                    : "PTB采样频率命令下发失败：%1 Hz").arg(ptb_sample_rate_));
                return;
            }
        }
    }
    if (requestedRate != ptb_sample_rate_)
    {
        log(QString(is_english_
            ? "PTB sample rate set to %1 Hz (capped from %2 Hz)"
            : "PTB采样频率已设置为 %1 Hz（由 %2 Hz 限制）")
            .arg(ptb_sample_rate_)
            .arg(requestedRate));
    }
    else
    {
        log(QString(is_english_ ? "PTB sample rate set to %1 Hz" : "PTB采样频率已设置为 %1 Hz").arg(ptb_sample_rate_));
    }
}

void MainWindow::onHmpRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    hmp_sample_rate_ = effectiveRateOrDefault(text, kDefaultHmpSampleRateHz);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.hmp) collectors.hmp->setSampleRate(hmp_sample_rate_);
    if (skipDeviceRate)
    {
        log(is_english_
            ? "HMP polling-rate selection left unset; using the default host polling rate."
            : "HMP 轮询频率保持不设定，使用默认主机轮询频率。");
    }
    else
    {
        log(QString(is_english_ ? "HMP sample rate set to %1 Hz" : "HMP采样频率已设置为 %1 Hz").arg(hmp_sample_rate_));
    }
}

void MainWindow::onLidarRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    lidar_sample_rate_ = effectiveRateOrDefault(text, kDefaultLidarSampleRateHz, 100);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.lidar)
    {
        collectors.lidar->setSampleRate(lidar_sample_rate_);
        if (collectors.lidar->isRunning() && !skipDeviceRate)
        {
            collectors.lidar->setDeviceSampleRate(lidar_sample_rate_);
        }
    }
    if (skipDeviceRate)
    {
        log(is_english_ ? "Lidar output-rate command disabled; using device default/adaptive output" : "已禁用激光测距仪输出频率下发，使用设备默认/自适应输出");
    }
    else
    {
        log(QString(is_english_ ? "Lidar sample rate set to %1 Hz" : "激光测距仪采样频率已设置为 %1 Hz").arg(lidar_sample_rate_));
    }
}

void MainWindow::applyAllSampleRates()
{
    int rate = parseRate(global_rate_combo_ ? global_rate_combo_->currentText() : QString::number(kDefaultHmpSampleRateHz));
    const bool skipEpsilonDeviceRate = epsilon_rate_combo_ && isRateUnspecified(epsilon_rate_combo_->currentText());
    const bool skipPtbDeviceRate = ptb_rate_combo_ && isRateUnspecified(ptb_rate_combo_->currentText());
    const bool skipHmpDeviceRate = hmp_rate_combo_ && isRateUnspecified(hmp_rate_combo_->currentText());
    const bool skipLidarDeviceRate = lidar_rate_combo_ && isRateUnspecified(lidar_rate_combo_->currentText());
    const CollectorSnapshot collectors = snapshotCollectors();
    QSettings settings("VaporView", "MainWindow");
    bool epsilonUsesCustomPacketRates = false;
    const int epsilonRate = skipEpsilonDeviceRate ? kDefaultEpsilonSampleRateHz : std::clamp(rate, 20, 200);
    const int ptbRate = skipPtbDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(rate);
    const int hmpRate = skipHmpDeviceRate ? kDefaultHmpSampleRateHz : rate;
    const int lidarRate = skipLidarDeviceRate ? kDefaultLidarSampleRateHz : std::min(rate, 100);
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, epsilonRate, &epsilonUsesCustomPacketRates);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilonRate);

    if (collectors.epsilon && collectors.epsilon->isRunning())
    {
        collectors.epsilon->setSampleRate(epsilonCallbackRate);
        if (!skipEpsilonDeviceRate)
        {
            collectors.epsilon->setOutputPacketRates(epsilonDesiredPacketRates);
        }
    }
    if (collectors.ptb && collectors.ptb->isRunning())
    {
        collectors.ptb->setSampleRate(ptbRate);
        if (!skipPtbDeviceRate && !collectors.ptb->setDeviceSampleRate(ptbRate))
        {
            log(QString(is_english_
                ? "PTB sample rate command failed for %1 Hz"
                : "PTB采样频率命令下发失败：%1 Hz").arg(ptbRate));
        }
    }
    if (collectors.hmp && collectors.hmp->isRunning())
    {
        collectors.hmp->setSampleRate(hmpRate);
    }
    if (collectors.lidar && collectors.lidar->isRunning())
    {
        collectors.lidar->setSampleRate(lidarRate);
        if (!skipLidarDeviceRate)
        {
            collectors.lidar->setDeviceSampleRate(lidarRate);
        }
    }

    if (epsilon_rate_combo_) epsilon_rate_combo_->blockSignals(true);
    ptb_rate_combo_->blockSignals(true);
    hmp_rate_combo_->blockSignals(true);
    lidar_rate_combo_->blockSignals(true);

    if (epsilon_rate_combo_ && !skipEpsilonDeviceRate) epsilon_rate_combo_->setCurrentText(QString::number(epsilonRate));
    if (ptb_rate_combo_ && !skipPtbDeviceRate) ptb_rate_combo_->setCurrentText(QString::number(ptbRate));
    if (hmp_rate_combo_ && !skipHmpDeviceRate) hmp_rate_combo_->setCurrentText(QString::number(rate));
    if (lidar_rate_combo_ && !skipLidarDeviceRate) lidar_rate_combo_->setCurrentText(QString::number(lidarRate));

    if (epsilon_rate_combo_) epsilon_rate_combo_->blockSignals(false);
    ptb_rate_combo_->blockSignals(false);
    hmp_rate_combo_->blockSignals(false);
    lidar_rate_combo_->blockSignals(false);

    gnss_sample_rate_ = rate;
    imu_sample_rate_ = rate;
    ptb_sample_rate_ = ptbRate;
    hmp_sample_rate_ = hmpRate;
    lidar_sample_rate_ = lidarRate;

    log(QString(is_english_ ? "All rates set to %1 Hz" : "所有频率已设置为 %1 Hz").arg(rate));
    if (skipEpsilonDeviceRate || skipPtbDeviceRate || skipHmpDeviceRate || skipLidarDeviceRate)
    {
        log(is_english_
            ? "Devices set to No Set keep their output-rate commands disabled."
            : "已选择“不设定”的设备保持不下发输出频率命令。");
    }
    if (!skipPtbDeviceRate && ptbRate != rate)
    {
        log(QString(is_english_
            ? "PTB sample rate capped at %1 Hz"
            : "PTB采样频率已限制为 %1 Hz").arg(ptbRate));
    }
}

void MainWindow::log(const QString& message)
{
    if (message.startsWith('\r'))
    {
        const QString inlineMessage = message.mid(1);
        QTextCursor cursor = log_text_edit_->textCursor();
        cursor.movePosition(QTextCursor::End);

        if (!has_inline_progress_log_)
        {
            if (!log_text_edit_->document()->isEmpty())
            {
                cursor.insertBlock();
            }
            cursor.insertText(inlineMessage);
            has_inline_progress_log_ = true;
        }
        else
        {
            cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.insertText(inlineMessage);
        }

        log_text_edit_->setTextCursor(cursor);
        log_text_edit_->ensureCursorVisible();
        if (QScrollBar *scrollBar = log_text_edit_->verticalScrollBar())
        {
            scrollBar->setValue(scrollBar->maximum());
        }
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    log_text_edit_->append(QString("[%1] %2").arg(timestamp, message));
    if (QScrollBar *scrollBar = log_text_edit_->verticalScrollBar())
    {
        scrollBar->setValue(scrollBar->maximum());
    }
    has_inline_progress_log_ = false;

    appendEventLogLine(QStringLiteral("info"), message);
    if (shouldMirrorToErrorLog(message))
    {
        appendErrorLogLine(message);
    }
}

void MainWindow::updateRecordingStatusLabel()
{
    if (!recording_status_label_)
    {
        return;
    }

    auto setVisualStatus = [this](const char *status) {
        const QString statusValue = QString::fromLatin1(status);
        recording_status_label_->setProperty("status", statusValue);
        if (recording_status_card_)
        {
            recording_status_card_->setProperty("status", statusValue);
        }
    };
    auto polishVisualStatus = [this]() {
        recording_status_label_->style()->unpolish(recording_status_label_);
        recording_status_label_->style()->polish(recording_status_label_);
        if (recording_status_card_)
        {
            recording_status_card_->style()->unpolish(recording_status_card_);
            recording_status_card_->style()->polish(recording_status_card_);
        }
    };
    auto localDetailText = [this](const QString& session,
                                  qlonglong sensorRows,
                                  qlonglong waveformFrames,
                                  qulonglong rawEpsilon,
                                  qulonglong rawPtb,
                                  qulonglong rawHmp,
                                  qulonglong rawLidar,
                                  qulonglong rawTcpWave) {
        return is_english_
            ? QStringLiteral("Session: %1\nSensor rows: %2\nWaveform frames: %3\nRaw EPSILON: %4\nRaw PTB: %5\nRaw HMP: %6\nRaw Lidar: %7\nRaw TCP wave: %8")
                  .arg(session)
                  .arg(sensorRows)
                  .arg(waveformFrames)
                  .arg(rawEpsilon)
                  .arg(rawPtb)
                  .arg(rawHmp)
                  .arg(rawLidar)
                  .arg(rawTcpWave)
            : QStringLiteral("会话：%1\n设备行数：%2\n波形帧数：%3\nRaw EPSILON：%4\nRaw PTB：%5\nRaw HMP：%6\nRaw Lidar：%7\nRaw TCP 波形：%8")
                  .arg(session)
                  .arg(sensorRows)
                  .arg(waveformFrames)
                  .arg(rawEpsilon)
                  .arg(rawPtb)
                  .arg(rawHmp)
                  .arg(rawLidar)
                  .arg(rawTcpWave);
    };

    if (isRemoteSkyMode())
    {
        const bool useLastRemoteStatus =
            remote_status_.session_name.isEmpty() &&
            remote_status_.telemetry_record_count == 0 &&
            remote_status_.raw_tcp_wave_record_count == 0 &&
            has_last_remote_recording_status_;
        const VaporView::TelemetryStatus& displayStatus = useLastRemoteStatus
            ? last_remote_recording_status_
            : remote_status_;
        const quint64 rawTotal =
            displayStatus.raw_epsilon_record_count +
            displayStatus.raw_ptb_record_count +
            displayStatus.raw_hmp_record_count +
            displayStatus.raw_lidar_record_count +
            displayStatus.raw_tcp_wave_record_count;
        const QString elapsed = formatElapsedCompact(displayStatus.recording_elapsed_ms);
        const QString session = displayStatus.session_name.isEmpty()
            ? QStringLiteral("--")
            : displayStatus.session_name;
        const QString detail = is_english_
            ? QStringLiteral("Session: %1\nElapsed: %2\nTelemetry rows: %3\nWave features: %4\nWave snapshots: %5\nRaw EPSILON: %6\nRaw PTB: %7\nRaw HMP: %8\nRaw Lidar: %9\nRaw TCP wave: %10")
                  .arg(session)
                  .arg(elapsed)
                  .arg(displayStatus.telemetry_record_count)
                  .arg(displayStatus.waveform_feature_record_count)
                  .arg(displayStatus.waveform_snapshot_record_count)
                  .arg(displayStatus.raw_epsilon_record_count)
                  .arg(displayStatus.raw_ptb_record_count)
                  .arg(displayStatus.raw_hmp_record_count)
                  .arg(displayStatus.raw_lidar_record_count)
                  .arg(displayStatus.raw_tcp_wave_record_count)
            : QStringLiteral("会话：%1\n时长：%2\n遥测行数：%3\n波形特征：%4\n波形快照：%5\nRaw EPSILON：%6\nRaw PTB：%7\nRaw HMP：%8\nRaw Lidar：%9\nRaw TCP 波形：%10")
                  .arg(session)
                  .arg(elapsed)
                  .arg(displayStatus.telemetry_record_count)
                  .arg(displayStatus.waveform_feature_record_count)
                  .arg(displayStatus.waveform_snapshot_record_count)
                  .arg(displayStatus.raw_epsilon_record_count)
                  .arg(displayStatus.raw_ptb_record_count)
                  .arg(displayStatus.raw_hmp_record_count)
                  .arg(displayStatus.raw_lidar_record_count)
                  .arg(displayStatus.raw_tcp_wave_record_count);
        recording_status_label_->setToolTip(detail);
        if (recording_status_card_)
        {
            recording_status_card_->setToolTip(detail);
        }
        if (remote_recording_state_ == 1)
        {
            recording_status_label_->setText(
                QString(is_english_ ? "Sky Recording: On\n%1\nRaw total: %2"
                                    : "天空端记录：进行中\n%1\nRaw 总数：%2")
                    .arg(detail)
                    .arg(rawTotal));
            setVisualStatus("connected");
        }
        else if (remote_recording_state_ == 2)
        {
            recording_status_label_->setText(
                QString(is_english_ ? "Sky Recording: Paused\n%1\nRaw total: %2"
                                    : "天空端记录：已暂停\n%1\nRaw 总数：%2")
                    .arg(detail)
                    .arg(rawTotal));
            setVisualStatus("connecting");
        }
        else
        {
            recording_status_label_->setText(
                QString(is_english_ ? "Sky Recording: Off\n%1\nRaw total: %2"
                                    : "天空端记录：未记录\n%1\nRaw 总数：%2")
                    .arg(detail)
                    .arg(rawTotal));
            setVisualStatus("disconnected");
        }
        polishVisualStatus();
        updateRecordingActionStates();
        return;
    }

    if (sensors_file_ && sensors_file_->isOpen())
    {
        const QFileInfo info(session_directory_);
        const QString session = info.fileName().isEmpty() ? QStringLiteral("--") : info.fileName();
        const QString detail = localDetailText(
            session,
            static_cast<qlonglong>(recording_entry_count_.load()),
            static_cast<qlonglong>(waveform_frame_count_.load()),
            static_cast<qulonglong>(raw_epsilon_record_count_.load()),
            static_cast<qulonglong>(raw_ptb_record_count_.load()),
            static_cast<qulonglong>(raw_hmp_record_count_.load()),
            static_cast<qulonglong>(raw_lidar_record_count_.load()),
            static_cast<qulonglong>(raw_tcp_wave_record_count_.load()));
        if (recording_paused_)
        {
            recording_status_label_->setText(
                QString(is_english_ ? "Recording: Paused\n%1" : "记录：已暂停\n%1").arg(detail));
            setVisualStatus("connecting");
        }
        else
        {
            recording_status_label_->setText(
                QString(is_english_ ? "Recording: On\n%1" : "记录：进行中\n%1").arg(detail));
            setVisualStatus("connected");
        }
    }
    else
    {
        const QString session = last_recording_session_name_.isEmpty()
            ? QStringLiteral("--")
            : last_recording_session_name_;
        const QString detail = localDetailText(
            session,
            static_cast<qlonglong>(last_recording_entry_count_),
            static_cast<qlonglong>(last_recording_waveform_frame_count_),
            static_cast<qulonglong>(last_raw_epsilon_record_count_),
            static_cast<qulonglong>(last_raw_ptb_record_count_),
            static_cast<qulonglong>(last_raw_hmp_record_count_),
            static_cast<qulonglong>(last_raw_lidar_record_count_),
            static_cast<qulonglong>(last_raw_tcp_wave_record_count_));
        recording_status_label_->setText(
            QString(is_english_ ? "Recording: Off\n%1" : "记录：未记录\n%1").arg(detail));
        setVisualStatus("disconnected");
    }

    const QString summary = recording_status_label_->text();
    recording_status_label_->setToolTip(summary);
    if (recording_status_card_)
    {
        recording_status_card_->setToolTip(summary);
    }
    polishVisualStatus();
    updateRecordingActionStates();
}

QString MainWindow::defaultRecordingDirectory() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath("CMakeLists.txt")) && QFileInfo::exists(dir.filePath("README.md")))
        {
            return dir.filePath("data");
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return QDir(QCoreApplication::applicationDirPath()).filePath("data");
}

QString MainWindow::locateRepositoryRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath("CMakeLists.txt")) && QFileInfo::exists(dir.filePath("README.md")))
        {
            return dir.path();
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return QString();
}

bool MainWindow::prepareRecordingSessionLayout(const QString& recordsPath, const QString& sessionName)
{
    QDir recordsDir(recordsPath);
    if (!recordsDir.exists() && !recordsDir.mkpath("."))
    {
        return false;
    }

    QString finalSessionName = sessionName;
    QString finalSessionDirectory = recordsDir.filePath(finalSessionName);
    int suffix = 1;
    while (QFileInfo::exists(finalSessionDirectory))
    {
        finalSessionName = QString("%1_%2").arg(sessionName).arg(suffix++);
        finalSessionDirectory = recordsDir.filePath(finalSessionName);
    }

    QDir sessionDir(finalSessionDirectory);
    if (!recordsDir.mkpath(finalSessionName) ||
        !sessionDir.mkpath("sensors") ||
        !sessionDir.mkpath("raw") ||
        !sessionDir.mkpath("logs") ||
        !sessionDir.mkpath("config"))
    {
        return false;
    }

    session_name_ = finalSessionName;
    session_directory_ = QDir::fromNativeSeparators(finalSessionDirectory);
    sensors_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("sensors/devices.csv"));
    raw_epsilon_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/epsilon.dat"));
    raw_ptb_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/ptb.dat"));
    raw_hmp_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/hmp.dat"));
    raw_lidar_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/lidar.dat"));
    raw_tcp_wave_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw/tcp_wave.dat"));
    raw_dat_doc_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("raw_dat_format.md"));
    session_metadata_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("session.json"));
    event_log_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("logs/event_log.csv"));
    error_log_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("logs/error_log.txt"));
    device_config_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("config/device_config.json"));
    return true;
}

bool MainWindow::copyRawDatFormatDocumentToSession()
{
    if (session_directory_.isEmpty() || raw_dat_doc_filename_.isEmpty())
    {
        return false;
    }

    const QString repositoryRoot = locateRepositoryRoot();
    if (repositoryRoot.isEmpty())
    {
        return false;
    }

    const QString sourcePath = QDir(repositoryRoot).filePath("docs/raw_dat_format.md");
    if (!QFileInfo::exists(sourcePath))
    {
        return false;
    }

    QFile::remove(raw_dat_doc_filename_);
    return QFile::copy(sourcePath, raw_dat_doc_filename_);
}

bool MainWindow::openUnifiedRawDatFile(std::unique_ptr<QFile>& file, const QString& filename, quint16 sourceId)
{
    if (filename.isEmpty())
    {
        return false;
    }

    file = std::make_unique<QFile>(filename);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.reset();
        return false;
    }

    UnifiedRawFileHeader header{};
    std::memcpy(header.magic, kUnifiedRawMagic, sizeof(header.magic));
    header.version = qToLittleEndian(kUnifiedRawFormatVersion);
    header.header_size = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawFileHeader)));
    header.source_id = qToLittleEndian(sourceId);
    header.reserved = 0;

    if (file->write(reinterpret_cast<const char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
    {
        file->close();
        file.reset();
        return false;
    }

    file->flush();
    return true;
}

bool MainWindow::writeUnifiedRawRecord(QFile *file,
                                       std::atomic<quint64>& recordCount,
                                       quint16 sourceId,
                                       quint16 recordType,
                                       quint32 flags,
                                       quint64 hostTimestampUs,
                                       const void *payload,
                                       size_t payloadSize)
{
    if (!file || !file->isOpen() || (payloadSize > 0 && !payload) ||
        payloadSize > static_cast<size_t>(std::numeric_limits<quint32>::max()))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!file->isOpen())
    {
        return false;
    }

    const quint64 sequence = recordCount.load(std::memory_order_relaxed);
    UnifiedRawRecordHeader header{};
    header.marker = qToLittleEndian(kUnifiedRawRecordMarker);
    header.header_size = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawRecordHeader)));
    header.host_timestamp_us = qToLittleEndian(hostTimestampUs);
    header.payload_size = qToLittleEndian(static_cast<quint32>(payloadSize));
    header.source_id = qToLittleEndian(sourceId);
    header.record_type = qToLittleEndian(recordType);
    header.flags = qToLittleEndian(flags);
    header.sequence = qToLittleEndian(sequence);

    if (file->write(reinterpret_cast<const char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
    {
        return false;
    }
    if (payloadSize > 0 &&
        file->write(reinterpret_cast<const char*>(payload), static_cast<qint64>(payloadSize)) != static_cast<qint64>(payloadSize))
    {
        return false;
    }

    recordCount.store(sequence + 1, std::memory_order_relaxed);
    return true;
}

void MainWindow::closeUnifiedRawDatFiles()
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    for (QFile *file : {raw_epsilon_file_.get(),
                       raw_ptb_file_.get(),
                       raw_hmp_file_.get(),
                       raw_lidar_file_.get(),
                       raw_tcp_wave_file_.get()})
    {
        if (file && file->isOpen())
        {
            file->flush();
            file->close();
        }
    }
}

void MainWindow::resetUnifiedRawDatFiles()
{
    raw_epsilon_file_.reset();
    raw_ptb_file_.reset();
    raw_hmp_file_.reset();
    raw_lidar_file_.reset();
    raw_tcp_wave_file_.reset();
}

void MainWindow::appendEventLogLine(const QString& level, const QString& message)
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!event_log_file_ || !event_log_file_->isOpen())
    {
        return;
    }

    QTextStream out(event_log_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out << csvEscape(recordingTimestampUtc()) << ','
        << currentTimestampUs() << ','
        << csvEscape(level) << ','
        << csvEscape(message) << '\n';
    out.flush();
}

void MainWindow::appendErrorLogLine(const QString& message)
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!error_log_file_ || !error_log_file_->isOpen())
    {
        return;
    }

    QTextStream out(error_log_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out << '[' << recordingTimestampUtc() << "] " << message << '\n';
    out.flush();
}

quint64 MainWindow::currentTimestampUs() const
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

quint64 MainWindow::steadyToEpochUs(const std::chrono::steady_clock::time_point& timePoint) const
{
    if (timePoint == std::chrono::steady_clock::time_point{})
    {
        return 0;
    }

    const auto delta = timePoint - steady_clock_anchor_;
    const auto systemPoint = system_clock_anchor_ + std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
    return static_cast<quint64>(std::chrono::duration_cast<std::chrono::microseconds>(systemPoint.time_since_epoch()).count());
}

void MainWindow::writeSessionMetadata(const QString& endTimeUtc)
{
    if (session_metadata_filename_.isEmpty() || session_directory_.isEmpty())
    {
        return;
    }

    QDir sessionDir(session_directory_);
    QJsonObject root;
    root["session_name"] = session_name_;
    root["start_time_utc"] = session_start_time_utc_;
    root["start_time_us"] = QString::number(session_start_time_us_);
    root["end_time_utc"] = endTimeUtc;
    root["software_version"] = QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev")
        : QCoreApplication::applicationVersion();
    root["epsilon_schema_version"] = QStringLiteral("epsilon.v1");
    root["waveform_points_per_frame"] = 50000;
    root["sensor_export_rate_hz"] = recording_export_rate_hz_;
    root["other_devices_export_rate_hz"] = recording_export_rate_hz_;
    root["raw_export_mode"] = QStringLiteral("unified_raw_dat");
    root["raw_dat_format_version"] = static_cast<int>(kUnifiedRawFormatVersion);
    root["waveform_export_rate_hz"] = 0;
    root["waveform_export_mode"] = QStringLiteral("per_frame");
    root["waveform_value_type"] = QStringLiteral("float32");
    root["waveform_timestamp_type"] = QStringLiteral("uint64");
    root["timestamp_unit"] = QStringLiteral("microseconds");
    root["sensor_rows"] = QString::number(recording_entry_count_.load());
    root["waveform_frames"] = QString::number(waveform_frame_count_.load());
    root["waveform_file_count"] = QString::number(waveform_file_count_.load());

    QJsonObject rawFiles;
    auto addRawFile = [&rawFiles, &sessionDir](const QString& name,
                                               const QString& filename,
                                               quint16 sourceId,
                                               quint64 recordCount) {
        QJsonObject raw;
        raw["path"] = sessionDir.relativeFilePath(filename);
        raw["source_id"] = static_cast<int>(sourceId);
        raw["format_version"] = static_cast<int>(kUnifiedRawFormatVersion);
        raw["record_count"] = QString::number(recordCount);
        rawFiles[name] = raw;
    };
    addRawFile(QStringLiteral("epsilon"), raw_epsilon_filename_, kRawSourceEpsilon, raw_epsilon_record_count_.load());
    addRawFile(QStringLiteral("ptb"), raw_ptb_filename_, kRawSourcePtb, raw_ptb_record_count_.load());
    addRawFile(QStringLiteral("hmp"), raw_hmp_filename_, kRawSourceHmp, raw_hmp_record_count_.load());
    addRawFile(QStringLiteral("lidar"), raw_lidar_filename_, kRawSourceLidar, raw_lidar_record_count_.load());
    addRawFile(QStringLiteral("tcp_wave"), raw_tcp_wave_filename_, kRawSourceTcpWave, raw_tcp_wave_record_count_.load());
    root["raw_files"] = rawFiles;

    QJsonObject paths;
    paths["raw_directory"] = QStringLiteral("raw");
    paths["devices_csv"] = sessionDir.relativeFilePath(sensors_filename_);
    paths["raw_format_doc"] = sessionDir.relativeFilePath(raw_dat_doc_filename_);
    paths["event_log"] = sessionDir.relativeFilePath(event_log_filename_);
    paths["error_log"] = sessionDir.relativeFilePath(error_log_filename_);
    paths["device_config"] = sessionDir.relativeFilePath(device_config_filename_);
    root["paths"] = paths;

    QFile file(session_metadata_filename_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

void MainWindow::writeDeviceConfigSnapshot()
{
    if (device_config_filename_.isEmpty())
    {
        return;
    }

    QJsonObject root;
    root["recording_directory"] = recording_directory_;
    root["session_directory"] = session_directory_;
    root["epsilon_schema_version"] = QStringLiteral("epsilon.v1");
    root["sensor_export_rate_hz"] = recording_export_rate_hz_;
    root["other_devices_export_rate_hz"] = recording_export_rate_hz_;
    root["raw_export_mode"] = QStringLiteral("unified_raw_dat");
    root["raw_dat_format_version"] = static_cast<int>(kUnifiedRawFormatVersion);
    root["waveform_export_rate_hz"] = 0;
    root["waveform_export_mode"] = QStringLiteral("per_frame");

    QJsonObject waveform;
    waveform["host"] = tcp_wave_panel_ ? tcp_wave_panel_->host() : QStringLiteral("127.0.0.1");
    waveform["port"] = tcp_wave_panel_ ? tcp_wave_panel_->port() : 8888;
    waveform["frame_rate_hz"] = 0;
    waveform["frame_rate_mode"] = QStringLiteral("per_frame");
    waveform["points_per_frame"] = 50000;
    waveform["value_type"] = QStringLiteral("float32");
    waveform["timestamp_type"] = QStringLiteral("uint64");
    root["waveform"] = waveform;

    QJsonObject raw;
    raw["directory"] = QStringLiteral("raw");
    raw["format_doc"] = QStringLiteral("raw_dat_format.md");
    raw["mode"] = QStringLiteral("per_verified_raw_frame_or_response");
    root["raw_dat"] = raw;

    QJsonObject sensors;
    auto addSerialConfig = [&sensors](const QString& name, QComboBox* port, QComboBox* baud, QComboBox* rate) {
        QJsonObject obj;
        obj["port"] = port ? port->currentText() : QString();
        obj["baud"] = baud ? baud->currentText() : QString();
        obj["rate_hz"] = rate ? rate->currentText() : QString();
        sensors[name] = obj;
    };
    addSerialConfig("epsilon", epsilon_port_combo_, epsilon_baud_combo_, nullptr);
    addSerialConfig("ptb", ptb_port_combo_, ptb_baud_combo_, ptb_rate_combo_);
    addSerialConfig("hmp", hmp_port_combo_, hmp_baud_combo_, hmp_rate_combo_);
    addSerialConfig("lidar", lidar_port_combo_, lidar_baud_combo_, lidar_rate_combo_);
    root["sensors"] = sensors;

    QFile file(device_config_filename_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

void MainWindow::startRecordingWorkers()
{
    if (recording_thread_running_.load())
    {
        return;
    }

    recording_paused_ = false;
    last_imu_record_timestamp_us_.store(0);

    QFile *filePtr = sensors_file_.get();
    recording_thread_running_.store(true);
    recording_thread_ = std::thread([this, filePtr]() {
        const int exportRateHz = std::max(1, recording_export_rate_hz_);
        const auto exportPeriod = std::chrono::microseconds(1000000 / exportRateHz);
        auto nextTick = std::chrono::steady_clock::now();
        while (recording_thread_running_.load())
        {
            const auto tickTime = std::chrono::steady_clock::now();
            const quint64 recordTimestampUs = currentTimestampUs();
            const CollectorSnapshot collectors = snapshotCollectors();
            const VaporView::EpsilonData epsilonSample = collectors.epsilon ? collectors.epsilon->getLatestData() : VaporView::EpsilonData();
            const VaporView::PtbData ptbSample = collectors.ptb ? collectors.ptb->getLatestData() : VaporView::PtbData();
            const VaporView::HmpData hmpSample = collectors.hmp ? collectors.hmp->getLatestData() : VaporView::HmpData();
            const VaporView::LidarData lidarSample = collectors.lidar ? collectors.lidar->getLatestData() : VaporView::LidarData();

            QStringList row;
            row.reserve(72);
            row << QString::number(recordTimestampUs);

            auto appendEmptyColumns = [&row](int count) {
                for (int i = 0; i < count; ++i)
                {
                    row << QString();
                }
            };
            auto appendBool = [&row](bool value) {
                row << csvBool(value);
            };
            auto isFresh = [tickTime](auto* collector, const auto& sample) {
                if (!collector || sample.timestamp == std::chrono::steady_clock::time_point{})
                {
                    return false;
                }
                const int rate = std::max(1, collector->getSampleRate());
                const int timeoutMs = std::max(250, static_cast<int>(std::ceil(3000.0 / rate)));
                const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(tickTime - sample.timestamp).count();
                return ageMs >= 0 && ageMs <= timeoutMs;
            };

            if (isFresh(collectors.epsilon.get(), epsilonSample))
            {
                row
                    << QString::number(steadyToEpochUs(epsilonSample.timestamp))
                    << QString::number(epsilonSample.device_timestamp_us)
                    << QString::number(epsilonSample.utc_unix_s)
                    << QString::number(epsilonSample.utc_microseconds)
                    << QString::number(epsilonSample.latitude_deg, 'f', 9)
                    << QString::number(epsilonSample.longitude_deg, 'f', 9)
                    << QString::number(epsilonSample.height_m, 'f', 6)
                    << QString::number(epsilonSample.ecef_x_m, 'f', 6)
                    << QString::number(epsilonSample.ecef_y_m, 'f', 6)
                    << QString::number(epsilonSample.ecef_z_m, 'f', 6)
                    << QString::number(epsilonSample.ned_n_m, 'f', 6)
                    << QString::number(epsilonSample.ned_e_m, 'f', 6)
                    << QString::number(epsilonSample.ned_d_m, 'f', 6)
                    << QString::number(epsilonSample.vel_n_mps, 'f', 6)
                    << QString::number(epsilonSample.vel_e_mps, 'f', 6)
                    << QString::number(epsilonSample.vel_d_mps, 'f', 6)
                    << QString::number(epsilonSample.body_vel_x_mps, 'f', 6)
                    << QString::number(epsilonSample.body_vel_y_mps, 'f', 6)
                    << QString::number(epsilonSample.body_vel_z_mps, 'f', 6)
                    << QString::number(epsilonSample.body_acc_x_mps2, 'f', 6)
                    << QString::number(epsilonSample.body_acc_y_mps2, 'f', 6)
                    << QString::number(epsilonSample.body_acc_z_mps2, 'f', 6)
                    << QString::number(epsilonSample.roll_deg, 'f', 6)
                    << QString::number(epsilonSample.pitch_deg, 'f', 6)
                    << QString::number(epsilonSample.yaw_deg, 'f', 6)
                    << QString::number(epsilonSample.quat_w, 'f', 8)
                    << QString::number(epsilonSample.quat_x, 'f', 8)
                    << QString::number(epsilonSample.quat_y, 'f', 8)
                    << QString::number(epsilonSample.quat_z, 'f', 8)
                    << QString::number(epsilonSample.ang_vel_x_radps, 'f', 8)
                    << QString::number(epsilonSample.ang_vel_y_radps, 'f', 8)
                    << QString::number(epsilonSample.ang_vel_z_radps, 'f', 8)
                    << QString::number(epsilonSample.imu_acc_x_mps2, 'f', 6)
                    << QString::number(epsilonSample.imu_acc_y_mps2, 'f', 6)
                    << QString::number(epsilonSample.imu_acc_z_mps2, 'f', 6)
                    << QString::number(epsilonSample.imu_gyr_x_radps, 'f', 8)
                    << QString::number(epsilonSample.imu_gyr_y_radps, 'f', 8)
                    << QString::number(epsilonSample.imu_gyr_z_radps, 'f', 8)
                    << QString::number(epsilonSample.mag_x_mg, 'f', 6)
                    << QString::number(epsilonSample.mag_y_mg, 'f', 6)
                    << QString::number(epsilonSample.mag_z_mg, 'f', 6)
                    << QString::fromStdString(epsilonSample.gnss_fix_text)
                    << QString::number(epsilonSample.gnss_satellites)
                    << QString::number(epsilonSample.hdop, 'f', 4)
                    << QString::number(epsilonSample.vdop, 'f', 4)
                    << QString::number(epsilonSample.hacc_m, 'f', 4)
                    << QString::number(epsilonSample.vacc_m, 'f', 4)
                    << QString::number(epsilonSample.lat_std_m, 'f', 4)
                    << QString::number(epsilonSample.lon_std_m, 'f', 4)
                    << QString::number(epsilonSample.height_std_m, 'f', 4)
                    << (std::isfinite(epsilonSample.diff_age_s) ? QString::number(epsilonSample.diff_age_s, 'f', 4) : QString())
                    << csvBool(epsilonSample.heading_valid)
                    << QString::number(epsilonSample.system_status_bits)
                    << QString::number(epsilonSample.filter_status_bits)
                    << QString::number(epsilonSample.update_status_bits);
                appendBool(epsilonSample.valid);
                row << QString::fromStdString(epsilonSample.error_message);
            }
            else
            {
                appendEmptyColumns(57);
            }

            if (isFresh(collectors.hmp.get(), hmpSample))
            {
                row
                    << QString::number(hmpSample.temperature, 'f', 6)
                    << QString::number(hmpSample.humidity, 'f', 6);
            }
            else
            {
                appendEmptyColumns(2);
            }

            if (isFresh(collectors.ptb.get(), ptbSample))
            {
                row << QString::number(ptbSample.pressure_hpa, 'f', 6);
            }
            else
            {
                appendEmptyColumns(1);
            }

            if (isFresh(collectors.lidar.get(), lidarSample))
            {
                row
                    << QString::number(lidarSample.distance_m, 'f', 6)
                    << QString::number(lidarSample.signal_strength);
                appendBool(lidarSample.valid);
            }
            else
            {
                appendEmptyColumns(3);
            }

            {
                std::lock_guard<std::mutex> lock(recording_files_mutex_);
                QTextStream out(filePtr);
                out.setEncoding(QStringConverter::Utf8);
                for (int i = 0; i < row.size(); ++i)
                {
                    if (i > 0)
                    {
                        out << ',';
                    }
                    out << csvEscape(row.at(i));
                }
                out << '\n';
                out.flush();
            }

            recording_entry_count_.fetch_add(1);
            QMetaObject::invokeMethod(this, [this]() {
                updateRecordingStatusLabel();
            }, Qt::QueuedConnection);

            nextTick += exportPeriod;
            std::this_thread::sleep_until(nextTick);
        }
    });
}

void MainWindow::stopRecordingWorkers()
{
    recording_thread_running_.store(false);
    if (recording_thread_.joinable())
    {
        recording_thread_.join();
    }
}

bool MainWindow::startRecordingSession()
{
    if (sensors_file_ && sensors_file_->isOpen())
    {
        if (!recording_paused_)
        {
            return true;
        }

        startRecordingWorkers();
        updateRecordingStatusLabel();
        log(QString(is_english_ ? "Resumed recording session: %1" : "已继续记录会话: %1").arg(session_directory_));
        return true;
    }

    QString recordsPath = recording_directory_.trimmed();
    if (recordsPath.isEmpty())
    {
        recordsPath = defaultRecordingDirectory();
        recording_directory_ = recordsPath;
    }

    const QString sessionName = QStringLiteral("session_%1").arg(recordingSessionDirectoryTimestamp());
    if (!prepareRecordingSessionLayout(recordsPath, sessionName))
    {
        QMessageBox::warning(
            this,
            is_english_ ? "Error" : "错误",
            is_english_ ? "Failed to create session directories" : "无法创建会话目录结构");
        return false;
    }

    sensors_file_ = std::make_unique<QFile>(sensors_filename_);
    event_log_file_ = std::make_unique<QFile>(event_log_filename_);
    error_log_file_ = std::make_unique<QFile>(error_log_filename_);
    if (!sensors_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !event_log_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !error_log_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !openUnifiedRawDatFile(raw_epsilon_file_, raw_epsilon_filename_, kRawSourceEpsilon) ||
        !openUnifiedRawDatFile(raw_ptb_file_, raw_ptb_filename_, kRawSourcePtb) ||
        !openUnifiedRawDatFile(raw_hmp_file_, raw_hmp_filename_, kRawSourceHmp) ||
        !openUnifiedRawDatFile(raw_lidar_file_, raw_lidar_filename_, kRawSourceLidar) ||
        !openUnifiedRawDatFile(raw_tcp_wave_file_, raw_tcp_wave_filename_, kRawSourceTcpWave))
    {
        sensors_file_.reset();
        resetUnifiedRawDatFiles();
        event_log_file_.reset();
        error_log_file_.reset();
        QMessageBox::warning(
            this,
            is_english_ ? "Error" : "错误",
            is_english_ ? "Failed to open session files for writing" : "无法打开会话文件进行写入");
        return false;
    }

    session_start_time_utc_ = recordingTimestampUtc();
    session_start_time_us_ = currentTimestampUs();
    recording_entry_count_.store(0);
    waveform_frame_count_.store(0);
    waveform_file_count_.store(0);
    raw_epsilon_record_count_.store(0);
    raw_ptb_record_count_.store(0);
    raw_hmp_record_count_.store(0);
    raw_lidar_record_count_.store(0);
    raw_tcp_wave_record_count_.store(0);
    {
        QTextStream eventOut(event_log_file_.get());
        eventOut.setEncoding(QStringConverter::Utf8);
        eventOut << "timestamp_utc,timestamp_us,level,message\n";
        eventOut.flush();
    }

    writeSensorsHeader();
    if (!copyRawDatFormatDocumentToSession())
    {
        log(QString(is_english_
            ? "Warning: failed to copy unified raw DAT format document into session folder"
            : "警告：未能将统一 raw DAT 格式说明复制到当前会话目录"));
    }
    writeSessionMetadata();
    writeDeviceConfigSnapshot();
    startRecordingWorkers();
    updateRecordingStatusLabel();
    log(QString(is_english_ ? "Started recording session: %1" : "已开始记录会话: %1").arg(session_directory_));
    return true;
}

void MainWindow::onChooseRecordingDirectoryClicked()
{
    const QString currentDirectory = recording_directory_.isEmpty() ? defaultRecordingDirectory() : recording_directory_;
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        is_english_ ? "Select Recording Folder" : "选择记录目录",
        currentDirectory,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selectedDirectory.isEmpty())
    {
        return;
    }

    recording_directory_ = QDir::fromNativeSeparators(selectedDirectory);
    QSettings settings("VaporView", "MainWindow");
    settings.setValue("recording_directory", recording_directory_);
    log(QString(is_english_ ? "Recording folder set to: %1" : "记录目录已设置为: %1").arg(recording_directory_));
}

void MainWindow::onStartRecordingClicked()
{
    if (isRemoteSkyMode())
    {
        if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
        {
            log(is_english_ ? "Connect Remote Sky telemetry before recording" : "开始记录前请先连接天空端数传");
            return;
        }
        ground_telemetry_service_->sendCommand(VaporView::CommandId::StartRecording);
        return;
    }

    const bool tcpConnected = tcp_wave_panel_ && tcp_wave_panel_->isConnected();
    if (!is_connected_ && !tcpConnected)
    {
        log(is_english_ ? "At least one serial device or the TCP wave link must be connected before recording"
                        : "开始记录前，至少需要一个串口设备在线或 TCP 波形链路已连接");
        return;
    }

    if (!startRecordingSession())
    {
        log(is_english_ ? "Failed to start recording session" : "启动记录会话失败");
    }
}

void MainWindow::onPauseRecordingClicked()
{
    if (isRemoteSkyMode())
    {
        if (ground_telemetry_service_) ground_telemetry_service_->sendCommand(VaporView::CommandId::PauseRecording);
        return;
    }
    pauseRecordingSession(true);
}

void MainWindow::onStopRecordingClicked()
{
    if (isRemoteSkyMode())
    {
        if (ground_telemetry_service_) ground_telemetry_service_->sendCommand(VaporView::CommandId::StopRecording);
        return;
    }
    stopRecording(true);
}

void MainWindow::pauseRecordingSession(bool announce)
{
    if (!sensors_file_ || !sensors_file_->isOpen() || recording_paused_)
    {
        return;
    }

    stopRecordingWorkers();
    recording_paused_ = true;
    writeSessionMetadata();
    updateRecordingStatusLabel();

    if (announce)
    {
        log(QString(is_english_ ? "Paused recording session: %1" : "已暂停记录会话: %1").arg(session_directory_));
    }
}

void MainWindow::stopRecording(bool announce)
{
    const bool hadOpenSession = sensors_file_ && sensors_file_->isOpen();
    stopRecordingWorkers();

    if (!hadOpenSession)
    {
        recording_paused_ = false;
        updateRecordingStatusLabel();
        return;
    }

    const qint64 entryCount = recording_entry_count_.load();
    const qint64 waveformCount = waveform_frame_count_.load();
    const QString sessionPath = session_directory_;
    last_recording_session_name_ = QFileInfo(sessionPath).fileName();
    if (last_recording_session_name_.isEmpty())
    {
        last_recording_session_name_ = session_name_;
    }
    last_recording_entry_count_ = entryCount;
    last_recording_waveform_frame_count_ = waveformCount;
    last_raw_epsilon_record_count_ = raw_epsilon_record_count_.load();
    last_raw_ptb_record_count_ = raw_ptb_record_count_.load();
    last_raw_hmp_record_count_ = raw_hmp_record_count_.load();
    last_raw_lidar_record_count_ = raw_lidar_record_count_.load();
    last_raw_tcp_wave_record_count_ = raw_tcp_wave_record_count_.load();

    if (sensors_file_ && sensors_file_->isOpen())
    {
        writeSessionMetadata(recordingTimestampUtc());
    }

    if (announce)
    {
        log(QString(is_english_
            ? "Stopped recording (%1 sensor rows, %2 waveform frames): %3"
            : "记录已结束（设备 %1 行，波形 %2 帧）: %3")
            .arg(entryCount)
            .arg(waveformCount)
            .arg(sessionPath));
    }

    closeUnifiedRawDatFiles();

    {
        std::lock_guard<std::mutex> lock(recording_files_mutex_);
        if (sensors_file_ && sensors_file_->isOpen())
        {
            sensors_file_->flush();
            sensors_file_->close();
        }
        if (event_log_file_ && event_log_file_->isOpen())
        {
            event_log_file_->flush();
            event_log_file_->close();
        }
        if (error_log_file_ && error_log_file_->isOpen())
        {
            error_log_file_->flush();
            error_log_file_->close();
        }
    }

    sensors_file_.reset();
    resetUnifiedRawDatFiles();
    event_log_file_.reset();
    error_log_file_.reset();
    recording_entry_count_.store(0);
    waveform_frame_count_.store(0);
    waveform_file_count_.store(0);
    raw_epsilon_record_count_.store(0);
    raw_ptb_record_count_.store(0);
    raw_hmp_record_count_.store(0);
    raw_lidar_record_count_.store(0);
    raw_tcp_wave_record_count_.store(0);
    recording_paused_ = false;
    session_directory_.clear();
    session_name_.clear();
    session_start_time_utc_.clear();
    session_start_time_us_ = 0;
    sensors_filename_.clear();
    raw_epsilon_filename_.clear();
    raw_ptb_filename_.clear();
    raw_hmp_filename_.clear();
    raw_lidar_filename_.clear();
    raw_tcp_wave_filename_.clear();
    raw_dat_doc_filename_.clear();
    session_metadata_filename_.clear();
    event_log_filename_.clear();
    error_log_filename_.clear();
    device_config_filename_.clear();
    updateRecordingStatusLabel();

}

void MainWindow::writeSensorsHeader()
{
    if (!sensors_file_ || !sensors_file_->isOpen())
    {
        return;
    }

    QTextStream out(sensors_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out
        << "record_timestamp_us,"
        << "epsilon_host_timestamp_us,epsilon_device_timestamp_us,epsilon_utc_unix_s,epsilon_utc_microseconds,"
        << "nav_lat_deg,nav_lon_deg,nav_height_m,"
        << "ecef_x_m,ecef_y_m,ecef_z_m,"
        << "ned_n_m,ned_e_m,ned_d_m,"
        << "vel_n_mps,vel_e_mps,vel_d_mps,"
        << "body_vel_x_mps,body_vel_y_mps,body_vel_z_mps,"
        << "body_acc_x_mps2,body_acc_y_mps2,body_acc_z_mps2,"
        << "roll_deg,pitch_deg,yaw_deg,"
        << "quat_w,quat_x,quat_y,quat_z,"
        << "ang_vel_x_radps,ang_vel_y_radps,ang_vel_z_radps,"
        << "imu_acc_x_mps2,imu_acc_y_mps2,imu_acc_z_mps2,"
        << "imu_gyr_x_radps,imu_gyr_y_radps,imu_gyr_z_radps,"
        << "mag_x_mg,mag_y_mg,mag_z_mg,"
        << "gnss_fix,gnss_satellites,hdop,vdop,hacc_m,vacc_m,"
        << "lat_std_m,lon_std_m,height_std_m,diff_age_s,"
        << "heading_valid,system_status_bits,filter_status_bits,update_status_bits,"
        << "epsilon_valid,epsilon_error_message,"
        << "hmp_temperature_c,hmp_humidity_rh,ptb_pressure_hpa,lidar_distance_m,lidar_signal_strength,lidar_valid\n";
    out.flush();
}

void MainWindow::onTcpRawWaveFrameReady(quint64 timestampUs,
                                        QByteArray rawSignalPayload,
                                        QByteArray harmonicPayload,
                                        VaporView::TcpFloatEncoding floatEncoding)
{
    if (!recording_thread_running_.load() || recording_paused_)
    {
        return;
    }

    if (static_cast<quint64>(rawSignalPayload.size()) > std::numeric_limits<quint32>::max() ||
        static_cast<quint64>(harmonicPayload.size()) > std::numeric_limits<quint32>::max())
    {
        return;
    }

    QByteArray payload;
    payload.resize(static_cast<int>(sizeof(quint32) * 2 + rawSignalPayload.size() + harmonicPayload.size()));
    char *cursor = payload.data();
    const quint32 rawSize = qToLittleEndian(static_cast<quint32>(rawSignalPayload.size()));
    const quint32 harmonicSize = qToLittleEndian(static_cast<quint32>(harmonicPayload.size()));
    std::memcpy(cursor, &rawSize, sizeof(rawSize));
    cursor += sizeof(rawSize);
    std::memcpy(cursor, &harmonicSize, sizeof(harmonicSize));
    cursor += sizeof(harmonicSize);
    if (!rawSignalPayload.isEmpty())
    {
        std::memcpy(cursor, rawSignalPayload.constData(), rawSignalPayload.size());
        cursor += rawSignalPayload.size();
    }
    if (!harmonicPayload.isEmpty())
    {
        std::memcpy(cursor, harmonicPayload.constData(), harmonicPayload.size());
    }

    if (writeUnifiedRawRecord(raw_tcp_wave_file_.get(),
                              raw_tcp_wave_record_count_,
                              kRawSourceTcpWave,
                              kRawRecordTypeGeneric,
                              kRawTcpWaveCombinedPayloadFlag | VaporView::tcpFloatEncodingToRawDatFlags(floatEncoding),
                              timestampUs,
                              payload.constData(),
                              static_cast<size_t>(payload.size())))
    {
        waveform_frame_count_.fetch_add(1);
        waveform_file_count_.store(1);
        QMetaObject::invokeMethod(this, [this]() {
            updateRecordingStatusLabel();
        }, Qt::QueuedConnection);
    }
}

void MainWindow::updateRecordingActionStates()
{
    if (isRemoteSkyMode())
    {
        const bool linkOpen = ground_telemetry_service_ && ground_telemetry_service_->isOpen();
        const bool recordingActive = remote_recording_state_ == 1;
        const bool recordingPaused = remote_recording_state_ == 2;
        if (start_recording_btn_) start_recording_btn_->setEnabled(linkOpen && !recordingActive);
        if (pause_recording_btn_) pause_recording_btn_->setEnabled(linkOpen && recordingActive);
        if (stop_recording_btn_) stop_recording_btn_->setEnabled(linkOpen && (recordingActive || recordingPaused));
        return;
    }

    const bool tcpConnected = tcp_wave_panel_ && tcp_wave_panel_->isConnected();
    const bool recordingSourceAvailable = is_connected_ || tcpConnected;
    const bool sessionOpen = sensors_file_ && sensors_file_->isOpen();
    const bool recordingActive = sessionOpen && !recording_paused_ && recording_thread_running_.load();
    const bool uiBusy = connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_;
    const bool canStart = recordingSourceAvailable && !uiBusy && (!sessionOpen || recording_paused_);
    const bool canPause = !uiBusy && recordingActive;
    const bool canStop = sessionOpen && !uiBusy;

    if (start_recording_btn_)
    {
        start_recording_btn_->setEnabled(canStart);
    }
    if (pause_recording_btn_)
    {
        pause_recording_btn_->setEnabled(canPause);
    }
    if (stop_recording_btn_)
    {
        stop_recording_btn_->setEnabled(canStop);
    }
}

void MainWindow::updateConnectionStatus(bool connected)
{
    is_connected_ = connected;
    const bool uiBusy = connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_;
    const bool inputsEnabled = !connected && !uiBusy;

    connect_btn_->setEnabled(inputsEnabled);
    cancel_connect_btn_->setEnabled(connection_attempt_in_progress_);
    disconnect_btn_->setEnabled(connected && !connection_attempt_in_progress_ && !epsilon_reconfigure_in_progress_);
    refresh_ports_btn_->setEnabled(inputsEnabled);
    if (epsilon_reconfigure_action_)
    {
        epsilon_reconfigure_action_->setEnabled(!uiBusy);
    }
    if (epsilon_rtcm_port_action_)
    {
        epsilon_rtcm_port_action_->setEnabled(!uiBusy);
    }
    if (epsilon_packet_rates_action_)
    {
        epsilon_packet_rates_action_->setEnabled(!uiBusy);
    }
    if (auto_detect_ports_btn_)
    {
        auto_detect_ports_btn_->setEnabled(!connected && !connection_attempt_in_progress_ && !epsilon_reconfigure_in_progress_);
        auto_detect_ports_btn_->setText(port_detection_in_progress_
            ? (is_english_ ? "Cancel Auto Detect" : "取消自动识别")
            : (is_english_ ? "Auto Detect Ports" : "自动识别串口"));
        auto_detect_ports_btn_->setToolTip(port_detection_in_progress_
            ? (is_english_ ? "Stop the current serial-port detection task." : "停止当前串口自动识别任务。")
            : (is_english_ ? "Probe available serial ports and automatically assign detected devices."
                           : "扫描可用串口，并将识别出的设备自动填入对应端口。"));
    }

    if (epsilon_port_combo_) epsilon_port_combo_->setEnabled(inputsEnabled);
    if (ptb_port_combo_) ptb_port_combo_->setEnabled(inputsEnabled);
    if (hmp_port_combo_) hmp_port_combo_->setEnabled(inputsEnabled);
    if (lidar_port_combo_) lidar_port_combo_->setEnabled(inputsEnabled);
    if (epsilon_baud_combo_) epsilon_baud_combo_->setEnabled(inputsEnabled);
    if (ptb_baud_combo_) ptb_baud_combo_->setEnabled(inputsEnabled);
    if (hmp_baud_combo_) hmp_baud_combo_->setEnabled(inputsEnabled);
    if (lidar_baud_combo_) lidar_baud_combo_->setEnabled(inputsEnabled);
    for (QPushButton* button : {imu_apply_btn_, imu_hi91_btn_, imu_hi92_btn_, imu_baud_115200_btn_, imu_baud_921600_btn_,
                                imu_rate_100_btn_, imu_rate_200_btn_, imu_rate_500_btn_, imu_rate_1000_btn_})
    {
        if (button)
        {
            button->setEnabled(!connection_attempt_in_progress_ && !port_detection_in_progress_ && !epsilon_reconfigure_in_progress_);
        }
    }

    if (port_detection_in_progress_)
    {
        status_label_->setText(is_english_ ? "Detecting Ports..." : "正在识别串口...");
        status_label_->setProperty("status", "connecting");
    }
    else if (epsilon_reconfigure_in_progress_)
    {
        status_label_->setText(is_english_ ? "Reconfiguring EPSILON..." : "正在重配 EPSILON...");
        status_label_->setProperty("status", "connecting");
    }
    else if (connection_attempt_in_progress_)
    {
        status_label_->setText(is_english_ ? "Connecting..." : "正在连接...");
        status_label_->setProperty("status", "connecting");
    }
    else if (connected)
    {
        status_label_->setText(is_english_ ? "Connected" : "已连接");
        status_label_->setProperty("status", "connected");
    }
    else
    {
        status_label_->setText(is_english_ ? "Disconnected" : "未连接");
        status_label_->setProperty("status", "disconnected");
    }
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
    updateSourceModeUi();
    updateRecordingActionStates();
}

bool MainWindow::anyCollectorRunning() const
{
    const CollectorSnapshot collectors = snapshotCollectors();
    return (collectors.epsilon && collectors.epsilon->isRunning()) ||
        (collectors.gnss && collectors.gnss->isRunning()) ||
        (collectors.imu && collectors.imu->isRunning()) ||
        (collectors.ptb && collectors.ptb->isRunning()) ||
        (collectors.hmp && collectors.hmp->isRunning()) ||
        (collectors.lidar && collectors.lidar->isRunning());
}

MainWindow::CollectorSnapshot MainWindow::snapshotCollectors() const
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    return {epsilon_collector_, gnss_collector_, imu_collector_, ptb_collector_, hmp_collector_, lidar_collector_};
}

void MainWindow::setCollectors(CollectorSnapshot collectors)
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    epsilon_collector_ = std::move(collectors.epsilon);
    gnss_collector_ = std::move(collectors.gnss);
    imu_collector_ = std::move(collectors.imu);
    ptb_collector_ = std::move(collectors.ptb);
    hmp_collector_ = std::move(collectors.hmp);
    lidar_collector_ = std::move(collectors.lidar);

    if (epsilon_collector_) epsilon_collector_->setEnglish(is_english_);
    if (gnss_collector_) gnss_collector_->setEnglish(is_english_);
    if (imu_collector_) imu_collector_->setEnglish(is_english_);
    if (ptb_collector_) ptb_collector_->setEnglish(is_english_);
    if (hmp_collector_) hmp_collector_->setEnglish(is_english_);
    if (lidar_collector_) lidar_collector_->setEnglish(is_english_);
}

void MainWindow::stopAllCollectors()
{
    CollectorSnapshot collectors;
    {
        std::lock_guard<std::mutex> lock(collector_mutex_);
        collectors.epsilon = std::move(epsilon_collector_);
        collectors.gnss = std::move(gnss_collector_);
        collectors.imu = std::move(imu_collector_);
        collectors.ptb = std::move(ptb_collector_);
        collectors.hmp = std::move(hmp_collector_);
        collectors.lidar = std::move(lidar_collector_);
    }

    if (collectors.epsilon)
    {
        collectors.epsilon->stop();
    }
    if (collectors.gnss)
    {
        collectors.gnss->stop();
    }
    if (collectors.imu)
    {
        collectors.imu->stop();
    }
    if (collectors.ptb)
    {
        collectors.ptb->stop();
    }
    if (collectors.hmp)
    {
        collectors.hmp->stop();
    }
    if (collectors.lidar)
    {
        collectors.lidar->stop();
    }
}

bool MainWindow::shouldAbortConnectionAttempt()
{
    return cancel_connection_requested_.load();
}

void MainWindow::finishConnectionAttempt(bool connected)
{
    connection_attempt_in_progress_ = false;
    cancel_connection_requested_.store(false);
    if (!connected && sensors_file_ && sensors_file_->isOpen())
    {
        stopRecording(true);
    }
    hideStatusTaskProgress();
    updateConnectionStatus(connected);
}

void MainWindow::onRefreshPortsClicked()
{
    QStringList ports = getAvailablePorts();

    auto updateCombo = [this, &ports](QComboBox* combo) {
        if (!combo)
        {
            return;
        }
        QString current = combo->currentText();
        combo->clear();
        combo->addItem(is_english_ ? "-- Select --" : "-- 选择 --");
        combo->addItems(ports);
        int idx = combo->findText(current);
        if (idx >= 0)
        {
            combo->setCurrentIndex(idx);
        }
        else
        {
            combo->setEditText(current);
        }
    };

    updateCombo(epsilon_port_combo_);
    updateCombo(ptb_port_combo_);
    updateCombo(hmp_port_combo_);
    updateCombo(lidar_port_combo_);

    log(QString(is_english_ ? "Ports refreshed: %1 serial ports"
                            : "端口已刷新: %1 个串口")
            .arg(ports.size()));
}

void MainWindow::onAutoDetectPortsClicked()
{
    if (port_detection_in_progress_)
    {
        cancel_connection_requested_.store(true);
        log(is_english_ ? "Cancel requested, stopping automatic serial-port detection..." : "已请求取消，正在停止自动识别串口...");
        showBusyStatusTaskProgress(is_english_ ? "Canceling port detection..." : "正在取消串口识别...");
        updateConnectionStatus(is_connected_);
        QApplication::processEvents(QEventLoop::AllEvents);
        return;
    }

    if (is_connected_ || connection_attempt_in_progress_)
    {
        return;
    }

    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }

    onRefreshPortsClicked();
    port_detection_in_progress_ = true;
    cancel_connection_requested_.store(false);
    updateConnectionStatus(is_connected_);
    log(is_english_ ? "Starting automatic serial-port detection..." : "开始自动识别串口...");
    showBusyStatusTaskProgress(is_english_ ? "Detecting Ports..." : "正在识别串口...");

    const QString selectedEpsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    const QString selectedPtbPort = ptb_port_combo_ ? ptb_port_combo_->currentText().trimmed() : QString();
    const QString selectedHmpPort = hmp_port_combo_ ? hmp_port_combo_->currentText().trimmed() : QString();
    const QString selectedLidarPort = lidar_port_combo_ ? lidar_port_combo_->currentText().trimmed() : QString();
    const QString selectedEpsilonBaud = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    const QString selectedPtbBaud = ptb_baud_combo_ ? ptb_baud_combo_->currentText().trimmed() : QStringLiteral("9600");
    const QString selectedHmpBaud = hmp_baud_combo_ ? hmp_baud_combo_->currentText().trimmed() : QStringLiteral("19200");
    const QString selectedLidarBaud = lidar_baud_combo_ ? lidar_baud_combo_->currentText().trimmed() : QStringLiteral("500000");

    port_detection_thread_ = std::thread([this,
                                          selectedEpsilonPort,
                                          selectedPtbPort,
                                          selectedHmpPort,
                                          selectedLidarPort,
                                          selectedEpsilonBaud,
                                          selectedPtbBaud,
                                          selectedHmpBaud,
                                          selectedLidarBaud]() {
        struct ProbeSpec
        {
            QString key;
            QString label;
            QString baud_text;
            std::function<bool(const QString&)> probe;
        };

        struct SelectedProbeSpec
        {
            ProbeSpec spec;
            QString port_name;
        };

        struct DetectionResult
        {
            QString key;
            QString port_name;
            QString baud_text;
        };

        const bool english = is_english_;
        const auto cancelRequested = [this]() { return cancel_connection_requested_.load(); };
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](QVector<DetectionResult> detections) {
            QMetaObject::invokeMethod(this, [this, detections = std::move(detections)]() {
                const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
                auto applySelection = [&selectText](QComboBox* combo, const QString& value) {
                    if (!combo)
                    {
                        return;
                    }
                    const int idx = combo->findText(value);
                    if (idx >= 0)
                    {
                        combo->setCurrentIndex(idx);
                    }
                    else if (!value.isEmpty() && value != selectText)
                    {
                        combo->setEditText(value);
                    }
                };
                auto normalizePort = [&selectText](const QString& value) {
                    return (value.isEmpty() || value == selectText) ? selectText : value;
                };

                QHash<QString, QComboBox*> portCombos{
                    {"epsilon", epsilon_port_combo_},
                    {"ptb", ptb_port_combo_},
                    {"hmp", hmp_port_combo_},
                    {"lidar", lidar_port_combo_},
                };
                QHash<QString, QComboBox*> baudCombos{
                    {"epsilon", epsilon_baud_combo_},
                    {"ptb", ptb_baud_combo_},
                    {"hmp", hmp_baud_combo_},
                    {"lidar", lidar_baud_combo_},
                };
                QHash<QString, QString> detectedPorts;
                QHash<QString, QString> detectedBauds;
                QSet<QString> detectedKeys;
                QSet<QString> detectedPortNames;
                for (const DetectionResult& detection : detections)
                {
                    const QString portName = normalizePort(detection.port_name);
                    if (portName == selectText)
                    {
                        continue;
                    }
                    if (!portCombos.contains(detection.key))
                    {
                        continue;
                    }
                    detectedPorts[detection.key] = portName;
                    detectedBauds[detection.key] = detection.baud_text;
                    detectedKeys.insert(detection.key);
                    detectedPortNames.insert(portName);
                }

                for (auto it = portCombos.cbegin(); it != portCombos.cend(); ++it)
                {
                    if (!detectedKeys.contains(it.key()) && detectedPortNames.contains(normalizePort(it.value() ? it.value()->currentText() : QString())))
                    {
                        applySelection(it.value(), selectText);
                    }
                }

                for (const DetectionResult& detection : detections)
                {
                    const QString portName = detectedPorts.value(detection.key);
                    if (portName.isEmpty())
                    {
                        continue;
                    }
                    applySelection(portCombos.value(detection.key), portName);
                    if (QComboBox *baudCombo = baudCombos.value(detection.key, nullptr))
                    {
                        baudCombo->setCurrentText(detectedBauds.value(detection.key));
                    }
                }

                port_detection_in_progress_ = false;
                cancel_connection_requested_.store(false);
                hideStatusTaskProgress();
                updateConnectionStatus(is_connected_);
            }, Qt::QueuedConnection);
        };

        auto probeCollector = [cancelRequested](const QString& port_name, auto&& collector, const VaporView::SerialConfig& config) {
            collector->setCancelCallback(cancelRequested);
            if (!collector->start(port_name.toStdString(), config))
            {
                return false;
            }

            const bool responded = collector->checkDeviceResponse();
            collector->stop();
            return responded;
        };

        const QString epsilonDefaultBaud = QStringLiteral("921600");
        const QString ptbDefaultBaud = QStringLiteral("9600");
        const QString hmpDefaultBaud = QStringLiteral("19200");
        const QString lidarDefaultBaud = QStringLiteral("500000");
        auto normalizeBaud = [](const QString& baud, const QString& fallback) {
            const QString trimmed = baud.trimmed();
            bool ok = false;
            const int value = trimmed.toInt(&ok);
            return ok && value > 0 ? trimmed : fallback;
        };
        const QHash<QString, QString> deviceLabels{
            {"epsilon", "EPSILON"},
            {"ptb", "PTB210"},
            {"hmp", "HMP3"},
            {"lidar", "TFA1500-L"},
        };

        auto makeEpsilonProbe = [probeCollector](const QString& baudText) {
            const int baud = baudText.toInt();
            return ProbeSpec{"epsilon", "EPSILON", baudText, [probeCollector, baud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::EpsilonCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N81(baud));
            }};
        };
        auto makePtbProbe = [probeCollector](const QString& baudText) {
            const int baud = baudText.toInt();
            return ProbeSpec{"ptb", "PTB210", baudText, [probeCollector, baud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::PtbCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::E71(baud));
            }};
        };
        auto makeHmpProbe = [probeCollector](const QString& baudText) {
            const int baud = baudText.toInt();
            return ProbeSpec{"hmp", "HMP3", baudText, [probeCollector, baud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::HmpCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N82(baud));
            }};
        };
        auto makeLidarProbe = [probeCollector](const QString& baudText) {
            const int baud = baudText.toInt();
            return ProbeSpec{"lidar", "TFA1500-L", baudText, [probeCollector, baud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::LidarCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N81(baud));
            }};
        };
        auto addUniqueProbe = [](QVector<ProbeSpec>& specs, QSet<QString>& seen, ProbeSpec spec) {
            const QString id = spec.key + QLatin1Char('@') + spec.baud_text;
            if (seen.contains(id))
            {
                return;
            }
            seen.insert(id);
            specs.push_back(std::move(spec));
        };

        auto addSelectedProbe = [](QVector<SelectedProbeSpec>& specs, QSet<QString>& seen, ProbeSpec spec, const QString& portName) {
            const QString normalizedPort = portName.trimmed();
            if (normalizedPort.isEmpty() || normalizedPort.startsWith(QStringLiteral("--")))
            {
                return;
            }
            const QString id = spec.key + QLatin1Char('@') + normalizedPort + QLatin1Char('@') + spec.baud_text;
            if (seen.contains(id))
            {
                return;
            }
            seen.insert(id);
            specs.push_back({std::move(spec), normalizedPort});
        };

        QVector<SelectedProbeSpec> selected_probe_specs;
        QSet<QString> seenSelectedProbeIds;
        addSelectedProbe(selected_probe_specs, seenSelectedProbeIds, makeEpsilonProbe(normalizeBaud(selectedEpsilonBaud, epsilonDefaultBaud)), selectedEpsilonPort);
        addSelectedProbe(selected_probe_specs, seenSelectedProbeIds, makePtbProbe(normalizeBaud(selectedPtbBaud, ptbDefaultBaud)), selectedPtbPort);
        addSelectedProbe(selected_probe_specs, seenSelectedProbeIds, makeHmpProbe(normalizeBaud(selectedHmpBaud, hmpDefaultBaud)), selectedHmpPort);
        addSelectedProbe(selected_probe_specs, seenSelectedProbeIds, makeLidarProbe(normalizeBaud(selectedLidarBaud, lidarDefaultBaud)), selectedLidarPort);

        QVector<ProbeSpec> default_probe_specs;
        QSet<QString> seenDefaultProbeIds;
        addUniqueProbe(default_probe_specs, seenDefaultProbeIds, makeEpsilonProbe(epsilonDefaultBaud));
        addUniqueProbe(default_probe_specs, seenDefaultProbeIds, makePtbProbe(ptbDefaultBaud));
        addUniqueProbe(default_probe_specs, seenDefaultProbeIds, makeHmpProbe(hmpDefaultBaud));
        addUniqueProbe(default_probe_specs, seenDefaultProbeIds, makeLidarProbe(lidarDefaultBaud));

        QStringList port_names = getAvailablePorts();
        if (port_names.isEmpty() && selected_probe_specs.isEmpty())
        {
            postLog(english ? "Auto detect stopped: no serial ports found." : "自动识别结束：当前没有发现可用串口。");
            finishOnUi({});
            return;
        }

        QVector<DetectionResult> detections;
        QSet<QString> detectedKeys;
        QSet<QString> detectedPorts;
        QSet<QString> attemptedProbeIds;
        postLog(QString(english ? "Auto detect: selected settings first, then %1 serial port(s) with default bauds."
                                 : "自动识别：先探测已选配置，再用默认波特率探测 %1 个串口。")
                    .arg(port_names.size()));

        auto probeAttemptId = [](const ProbeSpec& spec, const QString& portName) {
            return spec.key + QLatin1Char('@') + portName + QLatin1Char('@') + spec.baud_text;
        };
        auto recordDetection = [&](const ProbeSpec& spec, const QString& portName) {
            detections.push_back({spec.key, portName, spec.baud_text});
            detectedKeys.insert(spec.key);
            detectedPorts.insert(portName);
            postLog(QString(english ? "[Auto Detect] Identified %1 on %2 @ %3" : "[自动识别] 已识别 %1: %2 @ %3")
                        .arg(spec.label, portName, spec.baud_text));
        };
        auto finishCanceled = [&]() {
            postLog(QString(english
                ? "Auto detect canceled; keeping %1 identified device(s)."
                : "自动识别已取消；保留已识别出的 %1 个设备。")
                .arg(detections.size()));
            finishOnUi(std::move(detections));
        };

        auto runSelectedProbePhase = [&](const QString& phaseLabel, const QVector<SelectedProbeSpec>& specs) {
            if (specs.isEmpty())
            {
                return true;
            }

            postLog(QString(english ? "[Auto Detect] %1" : "[自动识别] %1").arg(phaseLabel));
            for (const SelectedProbeSpec& selectedSpec : specs)
            {
                const ProbeSpec& spec = selectedSpec.spec;
                if (detectedKeys.contains(spec.key))
                {
                    continue;
                }
                if (cancelRequested())
                {
                    finishCanceled();
                    return false;
                }
                if (detectedPorts.contains(selectedSpec.port_name))
                {
                    continue;
                }

                const QString attemptId = probeAttemptId(spec, selectedSpec.port_name);
                attemptedProbeIds.insert(attemptId);
                postLog(QString(english ? "[Auto Detect] Probing selected %1 on %2 @ %3..." : "[自动识别] 正在探测已选 %1: %2 @ %3 ...")
                            .arg(spec.label, selectedSpec.port_name, spec.baud_text));

                if (spec.probe(selectedSpec.port_name))
                {
                    recordDetection(spec, selectedSpec.port_name);
                }
            }
            return true;
        };

        auto runDefaultProbePhase = [&](const QString& phaseLabel, const QVector<ProbeSpec>& specs) {
            if (specs.isEmpty() || port_names.isEmpty())
            {
                return true;
            }

            postLog(QString(english ? "[Auto Detect] %1" : "[自动识别] %1").arg(phaseLabel));
            for (const ProbeSpec& spec : specs)
            {
                if (detectedKeys.contains(spec.key))
                {
                    continue;
                }

                for (const QString& port_name : port_names)
                {
                    if (cancelRequested())
                    {
                        finishCanceled();
                        return false;
                    }
                    if (detectedPorts.contains(port_name))
                    {
                        continue;
                    }
                    const QString attemptId = probeAttemptId(spec, port_name);
                    if (attemptedProbeIds.contains(attemptId))
                    {
                        continue;
                    }
                    attemptedProbeIds.insert(attemptId);

                    postLog(QString(english ? "[Auto Detect] Probing %1 on %2 @ %3..." : "[自动识别] 正在探测 %1: %2 @ %3 ...")
                                .arg(spec.label, port_name, spec.baud_text));

                    if (!spec.probe(port_name))
                    {
                        continue;
                    }

                    recordDetection(spec, port_name);
                    break;
                }
            }
            return true;
        };

        if (!runSelectedProbePhase(english
                ? "Selected port/baud pass: probing the current configured port for each device."
                : "已选串口/波特率阶段：先探测每个设备当前配置的串口。",
                selected_probe_specs))
        {
            return;
        }
        if (!runDefaultProbePhase(english
                ? "Default baud pass: probing remaining devices on available ports."
                : "默认波特率阶段：使用各设备默认波特率探测剩余串口。",
                default_probe_specs))
        {
            return;
        }

        for (auto it = deviceLabels.cbegin(); it != deviceLabels.cend(); ++it)
        {
            if (!detectedKeys.contains(it.key()))
            {
                postLog(QString(english ? "[Auto Detect] %1 not found" : "[自动识别] 未找到 %1").arg(it.value()));
            }
        }

        postLog(QString(english ? "Auto detect finished: identified %1 device(s)." : "自动识别完成：共识别出 %1 个设备。")
                    .arg(detections.size()));
        finishOnUi(std::move(detections));
    });
}

void MainWindow::onConnectClicked()
{
    if (isRemoteSkyMode())
    {
        clearRemoteSkyDataUi();
        const QString port = sky_telemetry_port_combo_ ? sky_telemetry_port_combo_->currentText().trimmed() : QString();
        const int baud = sky_telemetry_baud_combo_ ? sky_telemetry_baud_combo_->currentText().toInt() : 921600;
        if (port.isEmpty())
        {
            log(is_english_ ? "Select the Sky telemetry port first" : "请先选择天空端数传串口");
            return;
        }
        log(QString(is_english_ ? "Opening Sky telemetry serial port: %1 @ %2" : "正在打开天空端数传串口：%1 @ %2").arg(port).arg(baud));
        if (ground_telemetry_service_ && ground_telemetry_service_->open(port, baud))
        {
            updateConnectionStatus(true);
            ground_telemetry_service_->sendCommand(VaporView::CommandId::DisableWaveformStreaming);
            ground_telemetry_service_->sendCommand(VaporView::CommandId::RequestStatus);
            status_label_->setText(is_english_ ? "Telemetry port open, waiting for Sky handshake" : "数传串口已打开，等待天空端握手");
            status_label_->setProperty("status", "connecting");
            status_label_->style()->unpolish(status_label_);
            status_label_->style()->polish(status_label_);
            log(is_english_ ? "Telemetry serial port opened; waiting for Sky handshake..." : "数传串口已打开，正在等待天空端握手...");
        }
        else
        {
            updateConnectionStatus(false);
            log(is_english_ ? "Failed to open Remote Sky telemetry port" : "打开天空端数传串口失败");
        }
        return;
    }

    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }

    connection_attempt_in_progress_ = true;
    cancel_connection_requested_.store(false);
    updateConnectionStatus(false);

    log(is_english_ ? "Connecting..." : "正在连接...");

    current_epsilon_ = VaporView::EpsilonData();
    current_gnss_ = VaporView::GnssData();
    current_imu_ = VaporView::ImuData();
    current_ptb_ = VaporView::PtbData();
    current_hmp_ = VaporView::HmpData();
    current_lidar_ = VaporView::LidarData();

    if (epsilon_panel_) epsilon_panel_->updateData(current_epsilon_);
    if (ptb_panel_) ptb_panel_->updateData(current_ptb_);
    if (hmp_panel_) hmp_panel_->updateData(current_hmp_);
    if (lidar_panel_) lidar_panel_->updateData(current_lidar_);
    updateEnvironmentStatusIcons(false, false, false);

    if (epsilon_panel_) epsilon_panel_->updateRate(0.0);
    if (ptb_panel_) ptb_panel_->updateRate(0.0);
    if (hmp_panel_) hmp_panel_->updateRate(0.0);
    if (lidar_panel_) lidar_panel_->updateRate(0.0);

    const bool english = is_english_;
    const QString selectText = english ? "-- Select --" : "-- 选择 --";
    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    const QString ptbPort = ptb_port_combo_->currentText();
    const QString hmpPort = hmp_port_combo_->currentText();
    const QString lidarPort = lidar_port_combo_->currentText();
    const QString epsilonBaudText = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    const QString ptbBaudText = ptb_baud_combo_->currentText();
    const QString hmpBaudText = hmp_baud_combo_->currentText();
    const QString lidarBaudText = lidar_baud_combo_->currentText();
    const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const QString ptbRateText = ptb_rate_combo_ ? ptb_rate_combo_->currentText() : QStringLiteral("20");
    const QString hmpRateText = hmp_rate_combo_ ? hmp_rate_combo_->currentText() : QStringLiteral("20");
    const QString lidarRateText = lidar_rate_combo_ ? lidar_rate_combo_->currentText() : QStringLiteral("100");
    const bool skipEpsilonDeviceRate = isRateUnspecified(epsilonRateText);
    const bool skipPtbDeviceRate = isRateUnspecified(ptbRateText);
    const bool skipHmpDeviceRate = isRateUnspecified(hmpRateText);
    const bool skipLidarDeviceRate = isRateUnspecified(lidarRateText);
    const int epsilonRate = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    const int ptbRate = clampPtbSampleRate(effectiveRateOrDefault(ptbRateText, kDefaultPtbSampleRateHz, kPtbMaxSampleRateHz));
    const int hmpRate = effectiveRateOrDefault(hmpRateText, kDefaultHmpSampleRateHz);
    const int lidarRate = effectiveRateOrDefault(lidarRateText, kDefaultLidarSampleRateHz, 100);
    QSettings settings("VaporView", "MainWindow");
    bool epsilonUsesCustomPacketRates = false;
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, epsilonRate, &epsilonUsesCustomPacketRates);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilonRate);
    const QString epsilonDesiredPacketSignature = epsilonPacketRatesSignature(epsilonDesiredPacketRates);
    const QString epsilonDesiredPacketSummary = epsilonPacketRatesSummary(epsilonDesiredPacketRates);
    const bool epsilonConfigLikelyMatches =
        !epsilonPort.isEmpty() &&
        epsilonPort != selectText &&
        settings.value("epsilon_last_config_apply_version").toInt() == kEpsilonPacketConfigApplyVersion &&
        settings.value("epsilon_last_config_port").toString() == epsilonPort &&
        settings.value("epsilon_last_config_baud").toString() == epsilonBaudText &&
        settings.value("epsilon_last_config_signature").toString() == epsilonDesiredPacketSignature;
    const int selectedDeviceCount =
        ((epsilonPort != selectText && !epsilonPort.isEmpty()) ? 1 : 0) +
        ((ptbPort != selectText && !ptbPort.isEmpty()) ? 1 : 0) +
        ((hmpPort != selectText && !hmpPort.isEmpty()) ? 1 : 0) +
        ((lidarPort != selectText && !lidarPort.isEmpty()) ? 1 : 0);
    epsilon_sample_rate_ = epsilonRate;
    ptb_sample_rate_ = ptbRate;
    hmp_sample_rate_ = hmpRate;
    lidar_sample_rate_ = lidarRate;

    stopAllCollectors();

    const int connectionProgressSteps = std::max(1, selectedDeviceCount * 4 + 1);
    showStatusTaskProgress(is_english_ ? "Connecting devices..." : "正在连接设备...", 0, connectionProgressSteps);

    connection_thread_ = std::thread([this,
                                      english,
                                      selectText,
                                      epsilonPort,
                                      ptbPort,
                                      hmpPort,
                                      lidarPort,
                                      epsilonBaudText,
                                      ptbBaudText,
                                      hmpBaudText,
                                      lidarBaudText,
                                      epsilonConfigLikelyMatches,
                                      epsilonUsesCustomPacketRates,
                                      epsilonDesiredPacketRates,
                                      epsilonDesiredPacketSignature,
                                      epsilonDesiredPacketSummary,
                                      epsilonCallbackRate,
                                      epsilonRate,
                                      ptbRate,
                                      hmpRate,
                                      lidarRate,
                                      skipEpsilonDeviceRate,
                                      skipPtbDeviceRate,
                                      skipHmpDeviceRate,
                                      skipLidarDeviceRate,
                                      connectionProgressSteps]() {
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto postProgress = [this, connectionProgressSteps](const QString& label, int value) {
            QMetaObject::invokeMethod(this, [this, label, value, connectionProgressSteps]() {
                showStatusTaskProgress(label, value, connectionProgressSteps);
            }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](bool connected) {
            QMetaObject::invokeMethod(this, [this, connected]() { finishConnectionAttempt(connected); }, Qt::QueuedConnection);
        };
        auto persistEpsilonConfig = [epsilonPort, epsilonBaudText, epsilonRate, epsilonDesiredPacketSignature]() {
            QSettings settings("VaporView", "MainWindow");
            settings.setValue("epsilon_last_config_port", epsilonPort);
            settings.setValue("epsilon_last_config_baud", epsilonBaudText);
            settings.setValue("epsilon_last_config_rate_hz", epsilonRate);
            settings.setValue("epsilon_last_config_signature", epsilonDesiredPacketSignature);
            settings.setValue("epsilon_last_config_apply_version", kEpsilonPacketConfigApplyVersion);
        };

        CollectorSnapshot collectors;
        collectors.epsilon = std::make_shared<VaporView::EpsilonCollector>();
        collectors.ptb = std::make_shared<VaporView::PtbCollector>();
        collectors.hmp = std::make_shared<VaporView::HmpCollector>();
        collectors.lidar = std::make_shared<VaporView::LidarCollector>();
        setCollectors(collectors);

        auto logCallback = [this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        };
        auto cancelCallback = [this]() { return cancel_connection_requested_.load(); };

        collectors.epsilon->setSampleRate(epsilonCallbackRate);
        collectors.ptb->setSampleRate(ptbRate);
        collectors.hmp->setSampleRate(hmpRate);
        collectors.lidar->setSampleRate(lidarRate);

        collectors.epsilon->setLogCallback(logCallback);
        collectors.ptb->setLogCallback(logCallback);
        collectors.hmp->setLogCallback(logCallback);
        collectors.lidar->setLogCallback(logCallback);
        collectors.epsilon->setCancelCallback(cancelCallback);
        collectors.ptb->setCancelCallback(cancelCallback);
        collectors.hmp->setCancelCallback(cancelCallback);
        collectors.lidar->setCancelCallback(cancelCallback);
        collectors.epsilon->setRawFrameCallback([this](uint64_t hostTimestampUs, uint8_t packetId, uint8_t serialNumber, const uint8_t* packet_data, size_t size) {
            if (!recording_thread_running_.load())
            {
                return;
            }

            writeUnifiedRawRecord(raw_epsilon_file_.get(),
                                  raw_epsilon_record_count_,
                                  kRawSourceEpsilon,
                                  packetId,
                                  serialNumber,
                                  static_cast<quint64>(hostTimestampUs),
                                  packet_data,
                                  size);
        });
        collectors.ptb->setRawResponseCallback([this](uint64_t hostTimestampUs, const uint8_t* responseData, size_t size) {
            if (!recording_thread_running_.load())
            {
                return;
            }
            writeUnifiedRawRecord(raw_ptb_file_.get(),
                                  raw_ptb_record_count_,
                                  kRawSourcePtb,
                                  kRawRecordTypeGeneric,
                                  0u,
                                  static_cast<quint64>(hostTimestampUs),
                                  responseData,
                                  size);
        });
        collectors.hmp->setRawResponseCallback([this](uint64_t hostTimestampUs, const uint8_t* responseData, size_t size) {
            if (!recording_thread_running_.load())
            {
                return;
            }
            writeUnifiedRawRecord(raw_hmp_file_.get(),
                                  raw_hmp_record_count_,
                                  kRawSourceHmp,
                                  0x03u,
                                  0u,
                                  static_cast<quint64>(hostTimestampUs),
                                  responseData,
                                  size);
        });
        collectors.lidar->setRawFrameCallback([this](uint64_t hostTimestampUs, VaporView::LidarProtocol protocol, const uint8_t* frame, size_t size) {
            if (!recording_thread_running_.load())
            {
                return;
            }
            writeUnifiedRawRecord(raw_lidar_file_.get(),
                                  raw_lidar_record_count_,
                                  kRawSourceLidar,
                                  static_cast<quint16>(protocol),
                                  0u,
                                  static_cast<quint64>(hostTimestampUs),
                                  frame,
                                  size);
        });

        int total_devices = 0;
        int connected_devices = 0;
        int progressStep = 0;

        auto cancelAttempt = [&]() {
            stopAllCollectors();
            postLog(english ? "Connection canceled" : "连接已取消");
            finishOnUi(false);
        };
        auto abortIfRequested = [&]() {
            if (!shouldAbortConnectionAttempt())
            {
                return false;
            }
            cancelAttempt();
            return true;
        };
        auto connectCollector = [&](const QString& tag,
                                    const QString& port,
                                    const QString& baudText,
                                    auto* collector,
                                    const VaporView::SerialConfig& config,
                                    auto&& onReady) -> int {
            if (port == selectText || port.isEmpty())
            {
                postLog(QString(english ? "[%1] Skipped (not selected)" : "[%1] 跳过 (未选择)").arg(tag));
                return 0;
            }

            total_devices++;
            postProgress(english ? QString("Connecting %1...").arg(tag) : QString("正在连接 %1...").arg(tag), ++progressStep);
            postLog(QString(english ? "[%1] Checking port: %2" : "[%1] 检查端口: %2").arg(tag, port));
            if (abortIfRequested()) return -1;

            postProgress(english ? QString("Opening %1...").arg(tag) : QString("正在打开 %1...").arg(tag), ++progressStep);
            postLog(QString(english ? "[%1] Port selected, connecting..." : "[%1] 已选择端口，正在连接...").arg(tag));
            if (abortIfRequested()) return -1;

            if (!collector->start(port.toStdString(), config))
            {
                postLog(QString(english ? "[%1] Failed to open port: %2" : "[%1] 打开端口失败: %2")
                            .arg(tag, QString::fromStdString(collector->getLastError())));
                return 0;
            }

            postProgress(english ? QString("Checking %1 response...").arg(tag) : QString("正在检测 %1 响应...").arg(tag), ++progressStep);
            postLog(QString(english ? "[%1] Serial port opened, checking device response..." : "[%1] 串口已打开，正在检测设备响应...").arg(tag));
            if (abortIfRequested()) return -1;

            if (!collector->checkDeviceResponse())
            {
                if (abortIfRequested()) return -1;
                postLog(QString(english ? "[%1] Device not responding! Check power and cables." : "[%1] 设备无响应！请检查电源和连接线。").arg(tag));
                collector->stop();
                return 0;
            }

            postProgress(english ? QString("Starting %1 stream...").arg(tag) : QString("正在启动 %1 数据流...").arg(tag), ++progressStep);
            postLog(QString(english ? "[%1] Device responding, connected: %2 @ %3 baud" : "[%1] 设备响应正常，连接成功: %2 @ %3 波特率")
                        .arg(tag, port, baudText));
            if (!onReady())
            {
                collector->stop();
                return 0;
            }

            connected_devices++;
            return 1;
        };

        postLog(english ? "========== Starting Connection ==========" : "========== 开始连接 ==========");
        if (abortIfRequested()) return;

        if (connectCollector("EPSILON", epsilonPort, epsilonBaudText, collectors.epsilon.get(),
                             VaporView::SerialConfig::N81(epsilonBaudText.toInt()),
                             [&]() {
                                 collectors.epsilon->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onEpsilonDataReady", Qt::QueuedConnection); });
                                 collectors.epsilon->setSampleRate(epsilonCallbackRate);
                                 if (skipEpsilonDeviceRate)
                                 {
                                     postLog(english ? "[EPSILON] Skip output-rate command; using the current device output." : "[EPSILON] 跳过输出频率下发，使用设备当前输出。");
                                 }
                                 else if (epsilonConfigLikelyMatches)
                                 {
                                     postLog(QString(english
                                                         ? "[EPSILON] Using the last saved %1 profile; skipping automatic reconfiguration."
                                                         : "[EPSILON] 使用上次已保存的%1配置，跳过自动重配。")
                                                 .arg(epsilonUsesCustomPacketRates
                                                          ? (english ? "custom packet-rate" : "自定义包频率")
                                                          : (english ? "grouped output-rate" : "分组输出频率")));
                                 }
                                 else if (!collectors.epsilon->setOutputPacketRates(epsilonDesiredPacketRates))
                                 {
                                     postLog(QString(english
                                                         ? "[EPSILON] Failed to configure the %1 profile: %2"
                                                         : "[EPSILON] 配置%1失败：%2")
                                                 .arg(epsilonUsesCustomPacketRates
                                                          ? (english ? "custom packet-rate" : "自定义包频率")
                                                          : (english ? "grouped output-rate" : "分组输出频率"))
                                                 .arg(epsilonDesiredPacketSummary));
                                     return false;
                                 }
                                 else
                                 {
                                     persistEpsilonConfig();
                                     postLog(QString(english
                                                         ? "[EPSILON] Applied %1 profile: %2"
                                                         : "[EPSILON] 已应用%1配置：%2")
                                                 .arg(epsilonUsesCustomPacketRates
                                                          ? (english ? "custom packet-rate" : "自定义包频率")
                                                          : (english ? "grouped output-rate" : "分组输出频率"))
                                                 .arg(epsilonDesiredPacketSummary));
                                 }
                                 if (collectors.epsilon->startStreaming()) return true;
                                 postLog(english ? "[EPSILON] Failed to start navigation stream." : "[EPSILON] 启动导航数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("PTB", ptbPort, ptbBaudText, collectors.ptb.get(),
                             VaporView::SerialConfig::E71(ptbBaudText.toInt()),
                             [&]() {
                                 collectors.ptb->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onPtbDataReady", Qt::QueuedConnection); });
                                 collectors.ptb->setSampleRate(ptbRate);
                                 if (skipPtbDeviceRate)
                                 {
                                     postLog(english ? "[PTB] Skip sample-rate command; using the current device output." : "[PTB] 跳过采样频率下发，使用设备当前输出。");
                                 }
                                 else if (!collectors.ptb->setDeviceSampleRate(ptbRate))
                                 {
                                     postLog(QString(english
                                                       ? "[PTB] Failed to set sample rate to %1 Hz."
                                                       : "[PTB] 采样频率设置为 %1 Hz 失败。")
                                                 .arg(ptbRate));
                                     return false;
                                 }
                                 else
                                 {
                                     postLog(QString(english ? "[PTB] Sample rate set to %1 Hz" : "[PTB] 采样频率设置为 %1 Hz").arg(ptbRate));
                                 }
                                 if (collectors.ptb->startStreaming()) return true;
                                 postLog(english ? "[PTB] Failed to start data stream." : "[PTB] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("HMP", hmpPort, hmpBaudText, collectors.hmp.get(),
                             VaporView::SerialConfig::N82(hmpBaudText.toInt()),
                             [&]() {
                                 collectors.hmp->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onHmpDataReady", Qt::QueuedConnection); });
                                 collectors.hmp->setSampleRate(hmpRate);
                                 if (skipHmpDeviceRate)
                                 {
                                     postLog(english ? "[HMP] Polling-rate selection left unset; using the default host polling rate." : "[HMP] 轮询频率保持不设定，使用默认主机轮询频率。");
                                 }
                                 else
                                 {
                                     postLog(QString(english ? "[HMP] Sample rate set to %1 Hz" : "[HMP] 采样频率设置为 %1 Hz").arg(hmpRate));
                                 }
                                 if (collectors.hmp->startStreaming()) return true;
                                 postLog(english ? "[HMP] Failed to start data stream." : "[HMP] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("LIDAR", lidarPort, lidarBaudText, collectors.lidar.get(),
                             VaporView::SerialConfig::N81(lidarBaudText.toInt()),
                             [&]() {
                                 collectors.lidar->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onLidarDataReady", Qt::QueuedConnection); });
                                 collectors.lidar->setSampleRate(lidarRate);
                                 if (skipLidarDeviceRate)
                                 {
                                     postLog(english ? "[Lidar] Skip output-rate command; using device default/adaptive output." : "[Lidar] 跳过输出频率下发，使用设备默认/自适应输出。");
                                 }
                                 else if (!collectors.lidar->setDeviceSampleRate(lidarRate))
                                 {
                                     postLog(QString(english ? "[Lidar] Failed to apply output rate %1 Hz, using device default." : "[Lidar] 应用 %1 Hz 输出频率失败，使用设备默认输出。").arg(lidarRate));
                                 }
                                 else
                                 {
                                     postLog(QString(english ? "[Lidar] Output rate set to %1 Hz or host-side limit updated" : "[Lidar] 输出频率已设置为 %1 Hz，或已更新主机侧限频").arg(lidarRate));
                                 }
                                 if (collectors.lidar->startStreaming()) return true;
                                 postLog(english ? "[Lidar] Failed to start data stream." : "[Lidar] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        postProgress(english ? "Finalizing connection..." : "正在完成连接...", connectionProgressSteps);
        postLog(QString(english ? "========== Connection Summary: %1/%2 devices connected ==========" : "========== 连接摘要: %1/%2 设备已连接 ==========").arg(connected_devices).arg(total_devices));
        if (connected_devices == 0)
        {
            postLog(english ? "No ports connected" : "没有端口连接成功");
            finishOnUi(false);
            return;
        }

        finishOnUi(true);
    });
}

void MainWindow::onDisconnectClicked()
{
    if (isRemoteSkyMode())
    {
        log(is_english_ ? "Disconnecting Remote Sky telemetry..." : "正在断开天空端数传...");
        if (ground_telemetry_service_)
        {
            ground_telemetry_service_->close();
        }
        remote_recording_state_ = 0;
        clearRemoteSkyDataUi();
        updateConnectionStatus(false);
        log(is_english_ ? "Remote Sky disconnected" : "天空端数传已断开");
        return;
    }

    log(is_english_ ? "Disconnecting..." : "正在断开...");

    stopRecording(true);
    stopAllCollectors();
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }
    finishConnectionAttempt(false);
    log(is_english_ ? "Disconnected" : "已断开");
}

void MainWindow::onCancelConnectClicked()
{
    if (!connection_attempt_in_progress_)
    {
        return;
    }

    cancel_connection_requested_.store(true);
    log(is_english_ ? "Cancel requested, stopping connection attempt..." : "已请求取消，正在停止连接流程...");
    QApplication::processEvents(QEventLoop::AllEvents);
}

void MainWindow::onGnssDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.gnss)
    {
        current_gnss_ = collectors.gnss->getLatestData();
    }
}

void MainWindow::onEpsilonDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.epsilon)
    {
        current_epsilon_ = collectors.epsilon->getLatestData();
    }
}

void MainWindow::onImuDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.imu)
    {
        current_imu_ = collectors.imu->getLatestData();
    }
}

void MainWindow::onPtbDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.ptb)
    {
        current_ptb_ = collectors.ptb->getLatestData();
    }
}

void MainWindow::onHmpDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.hmp)
    {
        current_hmp_ = collectors.hmp->getLatestData();
    }
}

void MainWindow::onLidarDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.lidar)
    {
        current_lidar_ = collectors.lidar->getLatestData();
    }
}

void MainWindow::onRefreshTimer()
{
    if (isRemoteSkyMode())
    {
        refreshRemoteSkyDataUi();
        return;
    }

    const CollectorSnapshot collectors = snapshotCollectors();

    if (epsilon_panel_) epsilon_panel_->updateData(current_epsilon_);
    if (ptb_panel_) ptb_panel_->updateData(current_ptb_);
    if (hmp_panel_) hmp_panel_->updateData(current_hmp_);
    if (lidar_panel_) lidar_panel_->updateData(current_lidar_);
    updateEnvironmentStatusIcons(current_lidar_.valid, current_ptb_.valid, current_hmp_.valid);

    if (collectors.epsilon && epsilon_panel_)
    {
        epsilon_panel_->updateRate(collectors.epsilon->getActualRate());
    }
    if (collectors.ptb)
    {
        const double rate = collectors.ptb->getActualRate();
        ptb_panel_->updateRate(rate);
    }
    if (collectors.hmp)
    {
        const double rate = collectors.hmp->getActualRate();
        hmp_panel_->updateRate(rate);
    }
    if (collectors.lidar)
    {
        const double rate = collectors.lidar->getActualRate();
        lidar_panel_->updateRate(rate);
    }
}

void MainWindow::onRemoteBasicTelemetryUpdated(const VaporView::TelemetryBasic& telemetry)
{
    noteRemotePacket(VaporView::MsgType::TelemetryBasic);
    const auto now = std::chrono::steady_clock::now();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    auto hasFlag = [&telemetry](quint32 flag) {
        return (telemetry.validity_flags & flag) != 0;
    };

    current_epsilon_ = VaporView::EpsilonData();
    const double unknown = std::numeric_limits<double>::quiet_NaN();
    current_epsilon_.vel_n_mps = unknown;
    current_epsilon_.vel_e_mps = unknown;
    current_epsilon_.vel_d_mps = unknown;
    current_epsilon_.imu_acc_x_mps2 = unknown;
    current_epsilon_.imu_acc_y_mps2 = unknown;
    current_epsilon_.imu_acc_z_mps2 = unknown;
    current_epsilon_.imu_gyr_x_radps = unknown;
    current_epsilon_.imu_gyr_y_radps = unknown;
    current_epsilon_.imu_gyr_z_radps = unknown;
    current_epsilon_.roll_deg = unknown;
    current_epsilon_.pitch_deg = unknown;
    current_epsilon_.yaw_deg = unknown;
    current_epsilon_.hacc_m = unknown;
    current_epsilon_.vacc_m = unknown;
    if (hasFlag(VaporView::BasicHasEpsilonTime) ||
        hasFlag(VaporView::BasicHasPosition) ||
        hasFlag(VaporView::BasicHasEcef))
    {
        current_epsilon_.valid = true;
        current_epsilon_.timestamp = now;
        current_epsilon_.device_timestamp_us = telemetry.epsilon_time_us;
        current_epsilon_.utc_unix_s = telemetry.host_time_us / 1000000ULL;
        current_epsilon_.utc_microseconds = static_cast<quint32>(telemetry.host_time_us % 1000000ULL);
        int gnssFixCode = static_cast<int>(telemetry.gnss_fix_code);
        if (gnssFixCode == 0 && telemetry.filter_status_bits != 0)
        {
            gnssFixCode = static_cast<int>((telemetry.filter_status_bits >> 4) & 0x0F);
        }
        current_epsilon_.gnss_fix_code = gnssFixCode;
        current_epsilon_.gnss_fix_text = epsilonGnssFixTextForCode(gnssFixCode);
        if (hasFlag(VaporView::BasicHasPosition))
        {
            current_epsilon_.latitude_deg = telemetry.latitude_deg;
            current_epsilon_.longitude_deg = telemetry.longitude_deg;
            current_epsilon_.height_m = telemetry.height_m;
        }
        if (hasFlag(VaporView::BasicHasEcef))
        {
            current_epsilon_.ecef_x_m = telemetry.ecef_x_m;
            current_epsilon_.ecef_y_m = telemetry.ecef_y_m;
            current_epsilon_.ecef_z_m = telemetry.ecef_z_m;
        }
        current_epsilon_.system_status_bits = telemetry.status_bits;
        current_epsilon_.filter_status_bits = telemetry.filter_status_bits;
        current_epsilon_.update_status_bits = telemetry.update_status_bits;
        if (hasFlag(VaporView::BasicHasGnssQuality))
        {
            current_epsilon_.gnss_satellites = telemetry.gnss_satellites;
            current_epsilon_.hdop = telemetry.hdop;
            current_epsilon_.vdop = telemetry.vdop;
            current_epsilon_.hacc_m = telemetry.hacc_m;
            current_epsilon_.vacc_m = telemetry.vacc_m;
            current_epsilon_.heading_valid = telemetry.heading_valid;
        }
        if (hasFlag(VaporView::BasicHasNedVelocity))
        {
            current_epsilon_.vel_n_mps = telemetry.vel_n_mps;
            current_epsilon_.vel_e_mps = telemetry.vel_e_mps;
            current_epsilon_.vel_d_mps = telemetry.vel_d_mps;
        }
        if (hasFlag(VaporView::BasicHasImu))
        {
            current_epsilon_.imu_acc_x_mps2 = telemetry.imu_acc_x_mps2;
            current_epsilon_.imu_acc_y_mps2 = telemetry.imu_acc_y_mps2;
            current_epsilon_.imu_acc_z_mps2 = telemetry.imu_acc_z_mps2;
            current_epsilon_.imu_gyr_x_radps = telemetry.imu_gyr_x_radps;
            current_epsilon_.imu_gyr_y_radps = telemetry.imu_gyr_y_radps;
            current_epsilon_.imu_gyr_z_radps = telemetry.imu_gyr_z_radps;
        }
        if (hasFlag(VaporView::BasicHasAttitude))
        {
            current_epsilon_.roll_deg = telemetry.roll_deg;
            current_epsilon_.pitch_deg = telemetry.pitch_deg;
            current_epsilon_.yaw_deg = telemetry.yaw_deg;
        }
        if (hasFlag(VaporView::BasicHasEpsilonDiagnostics))
        {
            current_epsilon_.raw_frame_count = telemetry.raw_frame_count;
            current_epsilon_.dropped_frame_count = telemetry.dropped_frame_count;
            current_epsilon_.imu_packet_rate_hz = telemetry.imu_packet_rate_hz;
            current_epsilon_.ahrs_packet_rate_hz = telemetry.ahrs_packet_rate_hz;
            current_epsilon_.insgps_packet_rate_hz = telemetry.insgps_packet_rate_hz;
            current_epsilon_.sys_state_packet_rate_hz = telemetry.sys_state_packet_rate_hz;
            current_epsilon_.raw_gnss_packet_rate_hz = telemetry.raw_gnss_packet_rate_hz;
            current_epsilon_.satellite_packet_rate_hz = telemetry.satellite_packet_rate_hz;
            current_epsilon_.geodetic_packet_rate_hz = telemetry.geodetic_packet_rate_hz;
            current_epsilon_.ecef_packet_rate_hz = telemetry.ecef_packet_rate_hz;
        }
        remote_last_data_ms_.insert(VaporView::SkyDeviceId::Epsilon, nowMs);
    }
    else
    {
        remote_last_data_ms_.remove(VaporView::SkyDeviceId::Epsilon);
    }

    current_lidar_ = VaporView::LidarData();
    if (hasFlag(VaporView::BasicHasLidar))
    {
        current_lidar_.valid = true;
        current_lidar_.timestamp = now;
        current_lidar_.distance_m = telemetry.lidar_height_m;
        if (hasFlag(VaporView::BasicHasLidarStrength))
        {
            current_lidar_.signal_strength = telemetry.lidar_signal_strength;
        }
        remote_last_data_ms_.insert(VaporView::SkyDeviceId::Lidar, nowMs);
    }
    else
    {
        current_lidar_.error_message = remoteNoDataText(is_english_).toStdString();
        remote_last_data_ms_.remove(VaporView::SkyDeviceId::Lidar);
    }

    current_hmp_ = VaporView::HmpData();
    if (hasFlag(VaporView::BasicHasTemperature) && hasFlag(VaporView::BasicHasHumidity))
    {
        current_hmp_.valid = true;
        current_hmp_.timestamp = now;
        current_hmp_.temperature = telemetry.temperature_c;
        current_hmp_.humidity = telemetry.humidity_percent;
        remote_last_data_ms_.insert(VaporView::SkyDeviceId::Hmp, nowMs);
    }
    else
    {
        current_hmp_.error_message = remoteNoDataText(is_english_).toStdString();
        remote_last_data_ms_.remove(VaporView::SkyDeviceId::Hmp);
    }

    current_ptb_ = VaporView::PtbData();
    if (hasFlag(VaporView::BasicHasPressure))
    {
        current_ptb_.valid = true;
        current_ptb_.timestamp = now;
        current_ptb_.pressure_hpa = telemetry.pressure_hpa;
        remote_last_data_ms_.insert(VaporView::SkyDeviceId::Ptb, nowMs);
    }
    else
    {
        current_ptb_.error_message = remoteNoDataText(is_english_).toStdString();
        remote_last_data_ms_.remove(VaporView::SkyDeviceId::Ptb);
    }

    refreshRemoteSkyDataUi();
}

void MainWindow::onRemoteWaveformUpdated(const VaporView::DownsampledWaveform& waveform)
{
    noteRemotePacket(VaporView::MsgType::WaveformDownsampled);
    if (!remote_wave_stream_requested_)
    {
        return;
    }
    remote_last_data_ms_.insert(VaporView::SkyDeviceId::WaveTcp, QDateTime::currentMSecsSinceEpoch());
    if (tcp_wave_panel_)
    {
        if (waveform.channel_id == 1)
        {
            tcp_wave_panel_->injectRemoteRawSignalFrame(waveform.host_time_us, waveform.samples);
        }
        else if (waveform.channel_id == 4)
        {
            tcp_wave_panel_->injectRemoteSecondHarmonicFrame(waveform.host_time_us, waveform.samples);
        }
    }
}

void MainWindow::onRemoteWaveformFeatureUpdated(const VaporView::WaveformFeature& feature)
{
    noteRemotePacket(VaporView::MsgType::WaveformFeature);
    if (!remote_wave_stream_requested_)
    {
        return;
    }
    remote_last_data_ms_.insert(VaporView::SkyDeviceId::WaveTcp, QDateTime::currentMSecsSinceEpoch());
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->injectRemoteWaveformFeature(feature);
    }
}

void MainWindow::onRemoteTelemetryStatusUpdated(const VaporView::TelemetryStatus& status)
{
    noteRemotePacket(VaporView::MsgType::TelemetryStatus);
    if (!remote_sky_online_)
    {
        remote_sky_online_ = true;
        log(is_english_ ? "Remote Sky handshake confirmed" : "天空端握手成功");
    }
    remote_status_ = status;
    const quint64 rawTotal =
        status.raw_epsilon_record_count +
        status.raw_ptb_record_count +
        status.raw_hmp_record_count +
        status.raw_lidar_record_count +
        status.raw_tcp_wave_record_count;
    if (!status.session_name.isEmpty() ||
        status.telemetry_record_count > 0 ||
        status.waveform_feature_record_count > 0 ||
        status.waveform_snapshot_record_count > 0 ||
        rawTotal > 0)
    {
        last_remote_recording_status_ = status;
        has_last_remote_recording_status_ = true;
    }
    if (tcp_wave_panel_)
    {
        tcp_wave_panel_->setRemoteFeatureRateHz(status.feature_rate_hz);
    }
    remote_last_status_ms_ = QDateTime::currentMSecsSinceEpoch();
    remote_recording_state_ = status.recording_state;
    for (const VaporView::DeviceStatusItem& item : status.devices)
    {
        remote_device_states_.insert(item.device_id, item.state);
        updateRemoteDeviceButtonText(item.device_id, item.state);
        if (item.state != VaporView::DeviceState::Connected)
        {
            remote_last_data_ms_.remove(item.device_id);
            if (item.device_id == VaporView::SkyDeviceId::Epsilon)
            {
                current_epsilon_ = VaporView::EpsilonData();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Ptb)
            {
                current_ptb_ = VaporView::PtbData();
                current_ptb_.error_message = remoteDisconnectedText(is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Hmp)
            {
                current_hmp_ = VaporView::HmpData();
                current_hmp_.error_message = remoteDisconnectedText(is_english_).toStdString();
            }
            else if (item.device_id == VaporView::SkyDeviceId::Lidar)
            {
                current_lidar_ = VaporView::LidarData();
                current_lidar_.error_message = remoteDisconnectedText(is_english_).toStdString();
            }
        }
    }
    if (remote_wave_stream_auto_start_ &&
        !remote_wave_stream_requested_ &&
        !remote_wave_stream_enable_pending_ &&
        remote_device_states_.value(VaporView::SkyDeviceId::WaveTcp, VaporView::DeviceState::Disconnected) == VaporView::DeviceState::Connected &&
        ground_telemetry_service_ && ground_telemetry_service_->isOpen())
    {
        remote_wave_stream_enable_pending_ = true;
        ground_telemetry_service_->sendCommand(VaporView::CommandId::EnableWaveformStreaming);
    }
    refreshRemoteSkyDataUi();
    status_label_->setText(QString(is_english_ ? "Remote Sky Online | %1" : "天空端在线 | %1").arg(status.session_name));
    status_label_->setProperty("status", "connected");
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
    updateRecordingStatusLabel();
    updateSourceModeUi();
}

void MainWindow::onRemoteCommandAckReceived(const VaporView::CommandAck& ack)
{
    const bool ok = ack.result == 0;
    const QString commandName = VaporView::commandIdName(ack.command_id);
    const QString errorText = VaporView::commandErrorCodeText(ack.error_code, is_english_);
    const bool noError = ack.error_code == VaporView::CommandErrorCode::Ok;
    const QString detailLabel = ok && noError
        ? (is_english_ ? QStringLiteral("detail") : QStringLiteral("详情"))
        : (is_english_ ? QStringLiteral("error") : QStringLiteral("错误"));
    const QString detailText = ok && noError
        ? (is_english_ ? QStringLiteral("no error") : QStringLiteral("无错误"))
        : errorText;
    log(QString(is_english_ ? "Remote ACK command=%1(%2) seq=%3 result=%4 %5=%6(%7)"
                            : "远程ACK 命令=%1(%2) 序号=%3 结果=%4 %5=%6(%7)")
            .arg(commandName)
            .arg(static_cast<quint16>(ack.command_id))
            .arg(ack.command_seq)
            .arg(ok ? (is_english_ ? QStringLiteral("ok") : QStringLiteral("成功"))
                    : (is_english_ ? QStringLiteral("error") : QStringLiteral("失败")))
            .arg(detailLabel)
            .arg(detailText)
            .arg(static_cast<quint32>(ack.error_code)));

    if (ack.command_id == VaporView::CommandId::EnableWaveformStreaming)
    {
        remote_wave_stream_enable_pending_ = false;
        remote_wave_stream_requested_ = ok;
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                     remote_device_states_.value(VaporView::SkyDeviceId::WaveTcp,
                                                                 VaporView::DeviceState::Disconnected));
        if (!ok)
        {
            log(is_english_ ? "Remote waveform stream was not enabled" : "远程波形流启用失败");
        }
    }
    else if (ack.command_id == VaporView::CommandId::DisableWaveformStreaming)
    {
        remote_wave_stream_enable_pending_ = false;
        remote_wave_stream_requested_ = false;
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::WaveTcp,
                                     remote_device_states_.value(VaporView::SkyDeviceId::WaveTcp,
                                                                 VaporView::DeviceState::Disconnected));
    }

    if (ack.command_id == VaporView::CommandId::SetPeakSearchRange)
    {
        const auto it = remote_peak_search_commands_.find(ack.command_seq);
        if (it != remote_peak_search_commands_.end())
        {
            const VaporView::PeakSearchRange range = it.value();
            remote_peak_search_commands_.erase(it);
            if (tcp_wave_panel_)
            {
                if (ok)
                {
                    tcp_wave_panel_->applyRemotePeakSearchRange(range.start_index, range.end_index);
                    log(is_english_
                            ? QStringLiteral("Peak search range accepted. Old remote peak trend was cleared.")
                            : QStringLiteral("峰值搜索区间已生效，旧远程峰值趋势已清空。"));
                }
                else
                {
                    tcp_wave_panel_->rejectRemotePeakSearchRange(errorText);
                }
            }
        }
    }
}

void MainWindow::onRemoteLinkOpenChanged(bool open)
{
    if (isRemoteSkyMode())
    {
        if (!open)
        {
            markRemoteSkyLinkClosed();
        }
        updateConnectionStatus(open);
    }
}

void MainWindow::onSkyDeviceConfigClicked()
{
    if (!ground_telemetry_service_ || !ground_telemetry_service_->isOpen())
    {
        log(is_english_ ? "Connect Remote Sky telemetry before opening the Sky Device Config dialog"
                        : "打开天空端设备配置前，请先连接天空端数传");
        return;
    }
    if (!sky_device_config_dialog_)
    {
        sky_device_config_dialog_ = new VaporView::SkyDeviceConfigDialog(ground_telemetry_service_, this);
        sky_device_config_dialog_->setEnglish(is_english_);
        sky_device_config_dialog_->setFontScale(font_scale_percent_);
    }
    sky_device_config_dialog_->show();
    sky_device_config_dialog_->raise();
    ground_telemetry_service_->requestSkyConfig();
}

void MainWindow::onClearLogClicked()
{
    log_text_edit_->clear();
    has_inline_progress_log_ = false;
    log(is_english_ ? "Log cleared" : "日志已清空");
}

bool MainWindow::applyEpsilonMainAntennaLeverArm(double xM, double yM, double zM, QString *errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return false;
    };

    if (connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_)
    {
        return fail(is_english_
            ? QStringLiteral("EPSILON is busy. Wait for the current connection or configuration task to finish.")
            : QStringLiteral("EPSILON 当前正忙，请等待连接或配置任务结束后再试。"));
    }

    if (recording_thread_running_.load())
    {
        return fail(is_english_
            ? QStringLiteral("Stop recording before configuring the EPSILON main antenna lever arm.")
            : QStringLiteral("请先结束记录，再配置 EPSILON 主天线杆臂。"));
    }

    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    if (epsilonPort.isEmpty() || epsilonPort.startsWith(QStringLiteral("--")))
    {
        return fail(is_english_
            ? QStringLiteral("Select the EPSILON main serial port first.")
            : QStringLiteral("请先选择 EPSILON 主串口。"));
    }

    const QString epsilonBaudText = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool baudOk = false;
    const int epsilonBaud = epsilonBaudText.toInt(&baudOk);
    if (!baudOk || epsilonBaud <= 0)
    {
        return fail(QString(is_english_ ? "Invalid EPSILON baud rate: %1" : "EPSILON 波特率无效: %1").arg(epsilonBaudText));
    }

    const bool english = is_english_;
    const QString values = QStringLiteral("X=%1 m, Y=%2 m, Z=%3 m")
        .arg(QString::number(xM, 'f', 4),
             QString::number(yM, 'f', 4),
             QString::number(zM, 'f', 4));
    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();

    epsilon_reconfigure_in_progress_ = true;
    showBusyStatusTaskProgress(english ? "Configuring EPSILON Lever Arm..." : "正在配置 EPSILON 主天线杆臂...");
    updateConnectionStatus(is_connected_);
    QApplication::processEvents();

    log(QString(english
                    ? "[EPSILON] Applying main antenna lever arm on %1 @ %2: %3"
                    : "[EPSILON] 正在通过主串口 %1 @ %2 下发主天线杆臂：%3")
            .arg(epsilonPort, epsilonBaudText, values));

    std::shared_ptr<VaporView::EpsilonCollector> collector = shouldRestartCollector && liveCollector
        ? liveCollector
        : std::make_shared<VaporView::EpsilonCollector>();

    collector->setEnglish(english);
    collector->setLogCallback([this](const std::string& msg) {
        const QString qmsg = QString::fromStdString(msg);
        QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
    });

    if (shouldRestartCollector)
    {
        log(english
                ? "[EPSILON] Temporarily stopping the live stream for main antenna lever-arm configuration."
                : "[EPSILON] 为配置主天线杆臂临时停止当前数据流。");
        collector->stop();
    }

    bool commandSucceeded = false;
    QString failureMessage;
    if (!collector->start(epsilonPort.toStdString(), VaporView::SerialConfig::N81(epsilonBaud)))
    {
        failureMessage = QString(english
                ? "[EPSILON] Failed to open %1 for main antenna lever-arm configuration: %2"
                : "[EPSILON] 打开 %1 进行主天线杆臂配置失败: %2")
            .arg(epsilonPort, QString::fromStdString(collector->getLastError()));
        log(failureMessage);
    }
    else if (!collector->configureMainAntennaLeverArm(xM, yM, zM))
    {
        failureMessage = QString(english
                ? "[EPSILON] Failed to configure main antenna lever arm on %1 @ %2."
                : "[EPSILON] 在 %1 @ %2 上配置主天线杆臂失败。")
            .arg(epsilonPort, epsilonBaudText);
        log(failureMessage);
    }
    else
    {
        commandSucceeded = true;
    }

    bool restartSucceeded = true;
    if (shouldRestartCollector)
    {
        if (!collector->startStreaming())
        {
            restartSucceeded = false;
            log(english
                    ? "[EPSILON] Main antenna lever-arm command finished, but failed to restart the live navigation stream."
                    : "[EPSILON] 主天线杆臂命令已结束，但重新启动实时导航流失败。");
            collector->stop();
        }
        else
        {
            log(QString(english
                            ? "[EPSILON] Main antenna lever arm applied and live stream restarted on %1."
                            : "[EPSILON] 主天线杆臂已下发，并已在 %1 上恢复实时数据流。")
                    .arg(epsilonPort));
        }
    }
    else
    {
        collector->stop();
        if (commandSucceeded)
        {
            log(QString(english
                            ? "[EPSILON] Main antenna lever arm applied on %1."
                            : "[EPSILON] 已在 %1 上完成主天线杆臂配置。")
                    .arg(epsilonPort));
        }
    }

    epsilon_reconfigure_in_progress_ = false;
    hideStatusTaskProgress();
    updateConnectionStatus(anyCollectorRunning());
    QApplication::processEvents();

    if (!commandSucceeded)
    {
        return fail(failureMessage.isEmpty()
            ? (english
                ? QStringLiteral("Failed to apply EPSILON main antenna lever arm.")
                : QStringLiteral("EPSILON 主天线杆臂下发失败。"))
            : failureMessage);
    }

    if (!restartSucceeded)
    {
        return fail(english
            ? QStringLiteral("The lever arm was sent, but the EPSILON live stream could not be restarted. Reconnect EPSILON manually.")
            : QStringLiteral("杆臂已下发，但 EPSILON 实时数据流未能恢复。请手动重新连接 EPSILON。"));
    }

    return true;
}

void MainWindow::onRtkConfigClicked()
{
    if (!rtk_config_dialog_)
    {
        rtk_config_dialog_ = new RtkConfigDialog(this);
    }
    rtk_config_dialog_->setEpsilonDataProvider([this]() {
        const CollectorSnapshot collectors = snapshotCollectors();
        return collectors.epsilon ? collectors.epsilon->getLatestData() : current_epsilon_;
    });
    rtk_config_dialog_->setEpsilonMainAntennaLeverArmApplier([this](double x, double y, double z, QString *errorMessage) {
        return applyEpsilonMainAntennaLeverArm(x, y, z, errorMessage);
    });
    {
        const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
        const QString epsilonBaud = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
        rtk_config_dialog_->setEpsilonMainPortAndBaud(epsilonPort, epsilonBaud);
    }
    {
        QSettings settings("VaporView", "MainWindow");
        const QString preferredOutputPort = settings.value("epsilon_rtcm_forward_port").toString().trimmed();
        const QString preferredBaud = settings.value("epsilon_rtcm_forward_baud", "115200").toString().trimmed();
        if (!preferredOutputPort.isEmpty())
        {
            rtk_config_dialog_->setPreferredOutputPortAndBaud(preferredOutputPort, preferredBaud);
        }
    }
    rtk_config_dialog_->setFontScale(font_scale_percent_);
    rtk_config_dialog_->setEnglish(is_english_);
    VaporView::centerWindowOnScreen(rtk_config_dialog_, this);
    rtk_config_dialog_->show();
    rtk_config_dialog_->raise();
    rtk_config_dialog_->activateWindow();
}

void MainWindow::onConfigureEpsilonRtcmPortClicked()
{
    if (connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_)
    {
        return;
    }

    if (recording_thread_running_.load())
    {
        log(is_english_ ? "Stop recording before configuring the EPSILON RTCM port."
                        : "请先结束记录，再配置 EPSILON RTCM 串口。");
        return;
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    if (epsilonPort.isEmpty() || epsilonPort == selectText)
    {
        log(is_english_ ? "Select the EPSILON main serial port first."
                        : "请先选择 EPSILON 主串口。");
        return;
    }

    const QString epsilonBaudText = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool epsilonBaudOk = false;
    const int epsilonBaud = epsilonBaudText.toInt(&epsilonBaudOk);
    if (!epsilonBaudOk || epsilonBaud <= 0)
    {
        log(QString(is_english_ ? "Invalid EPSILON baud rate: %1" : "EPSILON 波特率无效: %1").arg(epsilonBaudText));
        return;
    }

    QSettings settings("VaporView", "MainWindow");
    const QStringList availablePorts = getAvailablePorts();

    QDialog dialog(this);
    dialog.setModal(true);
    dialog.setWindowTitle(is_english_ ? "Configure EPSILON RTCM Port" : "配置 EPSILON RTCM 串口");

    auto *layout = new QVBoxLayout(&dialog);
    auto *hintLabel = new QLabel(
        is_english_
            ? QStringLiteral("This configures EPSILON communication port 2 as an RTCM input port, saves the output-forwarding serial port on this PC, and prepares the RTK dialog to stream RTCM continuously into EPSILON.")
            : QStringLiteral("这个功能会把 EPSILON 的第二通信串口配置为 RTCM 输入口，同时保存本机用于转发 RTCM 的串口与波特率，并为后续 RTK 配置对话框做好预填。"),
        &dialog);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addLayout(formLayout);

    auto *mainPortValue = new QLabel(QStringLiteral("%1 @ %2").arg(epsilonPort, epsilonBaudText), &dialog);
    formLayout->addRow(is_english_ ? "EPSILON Main Port:" : "EPSILON 主串口：", mainPortValue);

    auto *deviceRtcmPortValue = new QLabel(is_english_ ? "COMM2 (RTCM)" : "串口2（RTCM）", &dialog);
    formLayout->addRow(is_english_ ? "Device RTCM Port:" : "设备 RTCM 串口：", deviceRtcmPortValue);

    auto *forwardPortCombo = new QComboBox(&dialog);
    forwardPortCombo->setEditable(true);
    forwardPortCombo->addItem(selectText);
    forwardPortCombo->addItems(availablePorts);
    const QString savedForwardPort = settings.value("epsilon_rtcm_forward_port").toString().trimmed();
    if (!savedForwardPort.isEmpty())
    {
        forwardPortCombo->setCurrentText(savedForwardPort);
    }
    formLayout->addRow(is_english_ ? "PC RTCM Forward Port:" : "本机 RTCM 转发串口：", forwardPortCombo);

    auto *forwardBaudCombo = new QComboBox(&dialog);
    forwardBaudCombo->addItems({QStringLiteral("115200"),
                                QStringLiteral("230400"),
                                QStringLiteral("460800"),
                                QStringLiteral("921600")});
    forwardBaudCombo->setCurrentText(settings.value("epsilon_rtcm_forward_baud", "115200").toString());
    formLayout->addRow(is_english_ ? "RTCM Port Baud:" : "RTCM 串口波特率：", forwardBaudCombo);

    auto *openRtkConfigCheck = new QCheckBox(
        is_english_ ? "Open RTK Config after success" : "成功后打开 RTK 配置",
        &dialog);
    openRtkConfigCheck->setChecked(true);
    layout->addWidget(openRtkConfigCheck);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const QString forwardPort = forwardPortCombo->currentText().trimmed();
    if (forwardPort.isEmpty() || forwardPort == selectText)
    {
        log(is_english_ ? "Select the PC serial port that is wired to EPSILON port 2."
                        : "请选择连接到 EPSILON 第二串口的本机串口。");
        return;
    }

    bool forwardBaudOk = false;
    const QString forwardBaudText = forwardBaudCombo->currentText().trimmed();
    const int forwardBaud = forwardBaudText.toInt(&forwardBaudOk);
    if (!forwardBaudOk || forwardBaud <= 0)
    {
        log(QString(is_english_ ? "Invalid RTCM forwarding baud rate: %1" : "RTCM 转发波特率无效: %1").arg(forwardBaudText));
        return;
    }

    if (epsilon_reconfigure_thread_.joinable())
    {
        epsilon_reconfigure_thread_.join();
    }

    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();
    const bool shouldOpenRtkDialog = openRtkConfigCheck->isChecked();
    const bool english = is_english_;

    epsilon_reconfigure_in_progress_ = true;
    showBusyStatusTaskProgress(english ? "Configuring EPSILON RTCM Port..." : "正在配置 EPSILON RTCM 串口...");
    updateConnectionStatus(is_connected_);
    log(QString(english
                    ? "[EPSILON] Configuring communication port 2 as RTCM: main %1 @ %2, RTCM forward port %3 @ %4"
                    : "[EPSILON] 正在把第二通信串口配置为 RTCM：主串口 %1 @ %2，RTCM 转发串口 %3 @ %4")
            .arg(epsilonPort, epsilonBaudText, forwardPort, forwardBaudText));

    epsilon_reconfigure_thread_ = std::thread([this,
                                               english,
                                               epsilonPort,
                                               epsilonBaud,
                                               epsilonBaudText,
                                               forwardPort,
                                               forwardBaud,
                                               forwardBaudText,
                                               liveCollector,
                                               shouldRestartCollector,
                                               shouldOpenRtkDialog]() {
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](bool openRtkDialog) {
            QMetaObject::invokeMethod(this, [this, openRtkDialog]() {
                epsilon_reconfigure_in_progress_ = false;
                hideStatusTaskProgress();
                updateConnectionStatus(anyCollectorRunning());
                if (openRtkDialog)
                {
                    onRtkConfigClicked();
                }
            }, Qt::QueuedConnection);
        };

        std::shared_ptr<VaporView::EpsilonCollector> collector = shouldRestartCollector && liveCollector
            ? liveCollector
            : std::make_shared<VaporView::EpsilonCollector>();

        collector->setEnglish(english);
        collector->setLogCallback([this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        });

        if (shouldRestartCollector)
        {
            postLog(english
                        ? "[EPSILON] Temporarily stopping the live stream for RTCM-port configuration."
                        : "[EPSILON] 为配置 RTCM 串口临时停止当前数据流。");
            collector->stop();
        }

        if (!collector->start(epsilonPort.toStdString(), VaporView::SerialConfig::N81(epsilonBaud)))
        {
            postLog(QString(english ? "[EPSILON] Failed to open %1 for RTCM-port configuration: %2"
                                    : "[EPSILON] 打开 %1 进行 RTCM 串口配置失败: %2")
                        .arg(epsilonPort, QString::fromStdString(collector->getLastError())));
            finishOnUi(false);
            return;
        }

        if (!collector->configureRtcmPort(2, forwardBaud))
        {
            postLog(QString(english ? "[EPSILON] Failed to configure communication port 2 as RTCM on %1 @ %2."
                                    : "[EPSILON] 在 %1 @ %2 上把第二通信串口配置为 RTCM 失败。")
                        .arg(epsilonPort, epsilonBaudText));
            collector->stop();
            finishOnUi(false);
            return;
        }

        {
            QSettings mainSettings("VaporView", "MainWindow");
            mainSettings.setValue("epsilon_rtcm_forward_port", forwardPort);
            mainSettings.setValue("epsilon_rtcm_forward_baud", forwardBaudText);
            QSettings rtkSettings("VaporView", "RtkConfig");
            rtkSettings.setValue("output_port", forwardPort);
            rtkSettings.setValue("baudrate", forwardBaudText);
        }

        if (shouldRestartCollector)
        {
            if (!collector->startStreaming())
            {
                postLog(english
                            ? "[EPSILON] RTCM-port configuration succeeded, but failed to restart the live navigation stream."
                            : "[EPSILON] RTCM 串口配置已完成，但重新启动实时导航流失败。");
                collector->stop();
                finishOnUi(false);
                return;
            }

            postLog(QString(english
                                ? "[EPSILON] RTCM port is ready. EPSILON live stream restarted on %1, and RTK forwarding is prefilled for %2 @ %3."
                                : "[EPSILON] RTCM 串口已就绪，已在 %1 上恢复 EPSILON 实时数据流，并为 %2 @ %3 预填 RTK 转发配置。")
                        .arg(epsilonPort, forwardPort, forwardBaudText));
        }
        else
        {
            collector->stop();
            postLog(QString(english
                                ? "[EPSILON] RTCM port is ready on %1. RTK forwarding is prefilled for %2 @ %3."
                                : "[EPSILON] 已在 %1 上完成 RTCM 串口配置，并为 %2 @ %3 预填 RTK 转发配置。")
                        .arg(epsilonPort, forwardPort, forwardBaudText));
        }

        finishOnUi(shouldOpenRtkDialog);
    });
}

void MainWindow::onConfigureEpsilonPacketRatesClicked()
{
    if (connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_)
    {
        return;
    }

    const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    QSettings settings("VaporView", "MainWindow");
    const bool customEnabled = settings.value("epsilon_custom_packet_rates_enabled", false).toBool();
    const std::map<uint8_t, int> defaultRates = defaultEpsilonPacketRates();
    const std::map<uint8_t, int> groupedRates = groupedEpsilonPacketRates(groupedRateHz);
    const std::map<uint8_t, int> initialRates = customEnabled
        ? loadCustomEpsilonPacketRates(settings, groupedRateHz)
        : groupedRates;

    QDialog dialog(this);
    dialog.setModal(true);
    dialog.setWindowTitle(is_english_ ? "EPSILON Packet Rates" : "EPSILON 包频率设置");

    auto *layout = new QVBoxLayout(&dialog);

    auto *hintLabel = new QLabel(
        is_english_
            ? QStringLiteral("Configured from the local EPSILON ground-station profile. The recommended default profile prioritizes stable time and 3D navigation output. Each row shows the packet's maximum supported rate. If any packet differs from the grouped profile, the custom profile will be enabled automatically when you save.")
            : QStringLiteral("配置范围来自本地 EPSILON 官方地面站配置。推荐默认配置优先保证稳定的时间与三维导航输出。每一行都显示该数据包支持的最大频率。只要任一数据包偏离分组模式，保存时就会自动启用自定义配置。"),
        &dialog);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto *enableCustomCheck = new QCheckBox(
        is_english_
            ? QStringLiteral("Use custom EPSILON packet rates for future connect/reconfigure operations")
            : QStringLiteral("后续连接和重配时使用这组自定义 EPSILON 包频率"),
        &dialog);
    enableCustomCheck->setChecked(customEnabled);
    layout->addWidget(enableCustomCheck);

    auto *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(formLayout);

    std::map<uint8_t, QComboBox*> packetCombos;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        auto *combo = new QComboBox(&dialog);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        for (int rateHz : option.supported_rates_hz)
        {
            combo->addItem(rateHz == 0
                               ? (is_english_ ? QStringLiteral("No Output (0 Hz)") : QStringLiteral("不输出 (0 Hz)"))
                               : QStringLiteral("%1 Hz").arg(rateHz),
                           rateHz);
        }
        const int initialRateHz = initialRates.count(option.packet_id) ? initialRates.at(option.packet_id) : groupedRates.at(option.packet_id);
        const int comboIndex = combo->findData(initialRateHz);
        if (comboIndex >= 0)
        {
            combo->setCurrentIndex(comboIndex);
        }
        packetCombos[option.packet_id] = combo;
        formLayout->addRow(epsilonPacketDialogRowLabel(option, is_english_), combo);
    }

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *recommendedDefaultsButton = buttonBox->addButton(
        is_english_ ? QStringLiteral("Use Recommended Defaults") : QStringLiteral("恢复推荐默认值"),
        QDialogButtonBox::ResetRole);
    connect(recommendedDefaultsButton, &QPushButton::clicked, &dialog, [&packetCombos, defaultRates, enableCustomCheck]() {
        enableCustomCheck->setChecked(true);
        for (const auto& entry : packetCombos)
        {
            const auto it = defaultRates.find(entry.first);
            if (it == defaultRates.end())
            {
                continue;
            }
            QComboBox *combo = entry.second;
            const int index = combo ? combo->findData(it->second) : -1;
            if (combo && index >= 0)
            {
                combo->setCurrentIndex(index);
            }
        }
    });
    QPushButton *groupedDefaultsButton = buttonBox->addButton(
        is_english_ ? QStringLiteral("Use Grouped Profile") : QStringLiteral("切换到分组模式"),
        QDialogButtonBox::ActionRole);
    connect(groupedDefaultsButton, &QPushButton::clicked, &dialog, [&packetCombos, groupedRates, enableCustomCheck]() {
        enableCustomCheck->setChecked(false);
        for (const auto& entry : packetCombos)
        {
            const auto it = groupedRates.find(entry.first);
            if (it == groupedRates.end())
            {
                continue;
            }
            QComboBox *combo = entry.second;
            const int index = combo ? combo->findData(it->second) : -1;
            if (combo && index >= 0)
            {
                combo->setCurrentIndex(index);
            }
        }
    });
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    std::map<uint8_t, int> savedPacketRates;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        QComboBox *combo = packetCombos[option.packet_id];
        const int rateHz = combo ? combo->currentData().toInt() : groupedRates.at(option.packet_id);
        savedPacketRates[option.packet_id] = rateHz;
        settings.setValue(epsilonPacketRateSettingsKey(option.packet_id), rateHz);
    }
    bool hasCustomOverrides = false;
    for (const auto& entry : savedPacketRates)
    {
        const auto groupedIt = groupedRates.find(entry.first);
        if (groupedIt != groupedRates.end() && groupedIt->second != entry.second)
        {
            hasCustomOverrides = true;
            break;
        }
    }
    const bool effectiveCustomEnabled = enableCustomCheck->isChecked() || hasCustomOverrides;
    settings.setValue("epsilon_custom_packet_rates_enabled", effectiveCustomEnabled);
    settings.setValue("epsilon_custom_packet_rates_user_saved", effectiveCustomEnabled);
    settings.remove("epsilon_last_config_signature");
    settings.remove("epsilon_last_config_apply_version");

    if (hasCustomOverrides && !enableCustomCheck->isChecked())
    {
        log(is_english_
                ? "[EPSILON] Packet-rate overrides detected, so the custom packet-rate profile has been enabled automatically."
                : "[EPSILON] 检测到包频率已偏离分组模式，已自动启用自定义包频率配置。");
    }

    if (effectiveCustomEnabled)
    {
        log(QString(is_english_
                        ? ((savedPacketRates == defaultRates)
                               ? "[EPSILON] Recommended default packet-rate profile saved: %1"
                               : "[EPSILON] Custom packet-rate profile saved: %1")
                        : ((savedPacketRates == defaultRates)
                               ? "[EPSILON] 已保存推荐默认包频率配置: %1"
                               : "[EPSILON] 已保存自定义包频率配置: %1"))
                .arg(epsilonPacketRatesSummary(savedPacketRates)));
    }
    else
    {
        log(QString(is_english_
                        ? "[EPSILON] Custom packet-rate profile disabled. The grouped %1 Hz profile will be used."
                        : "[EPSILON] 已关闭自定义包频率，后续将使用分组 %1 Hz 配置。")
                .arg(groupedRateHz));
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    if (!recording_thread_running_.load() &&
        !epsilonPort.isEmpty() &&
        epsilonPort != selectText &&
        !isRateUnspecified(epsilonRateText))
    {
        log(is_english_
                ? "[EPSILON] Applying the saved packet-rate profile now..."
                : "[EPSILON] 正在应用刚保存的包频率配置...");
        onReconfigureEpsilonClicked();
    }
    else
    {
        log(is_english_
                ? "[EPSILON] Packet-rate profile saved. It will be used on the next connect/reconfigure."
                : "[EPSILON] 包频率配置已保存，将在下次连接或重配时生效。");
    }
}

void MainWindow::onReconfigureEpsilonClicked()
{
    if (connection_attempt_in_progress_ || port_detection_in_progress_ || epsilon_reconfigure_in_progress_)
    {
        return;
    }

    if (recording_thread_running_.load())
    {
        log(is_english_ ? "Stop recording before reconfiguring EPSILON output."
                        : "请先结束记录，再重新配置 EPSILON 输出。");
        return;
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString epsilonPort = epsilon_port_combo_ ? epsilon_port_combo_->currentText().trimmed() : QString();
    if (epsilonPort.isEmpty() || epsilonPort == selectText)
    {
        log(is_english_ ? "Select an EPSILON serial port first." : "请先选择 EPSILON 串口。");
        return;
    }

    const QString epsilonBaudText = epsilon_baud_combo_ ? epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool baudOk = false;
    const int epsilonBaud = epsilonBaudText.toInt(&baudOk);
    if (!baudOk || epsilonBaud <= 0)
    {
        log(QString(is_english_ ? "Invalid EPSILON baud rate: %1" : "EPSILON 波特率无效: %1").arg(epsilonBaudText));
        return;
    }

    const QString epsilonRateText = epsilon_rate_combo_ ? epsilon_rate_combo_->currentText() : QStringLiteral("100");
    if (isRateUnspecified(epsilonRateText))
    {
        log(is_english_
            ? "[EPSILON] Output-rate command is disabled because the EPSILON rate is set to No Set."
            : "[EPSILON] EPSILON 频率为“不设定”，已跳过输出频率下发。");
        return;
    }

    const int epsilonRate = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    epsilon_sample_rate_ = epsilonRate;
    QSettings settings("VaporView", "MainWindow");
    bool usingCustomPacketProfile = false;
    const std::map<uint8_t, int> desiredPacketRates = effectiveEpsilonPacketRates(settings, epsilonRate, &usingCustomPacketProfile);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(desiredPacketRates, epsilonRate);
    const QString desiredPacketRateSignature = epsilonPacketRatesSignature(desiredPacketRates);
    const QString desiredPacketRateSummary = epsilonPacketRatesSummary(desiredPacketRates);

    if (epsilon_reconfigure_thread_.joinable())
    {
        epsilon_reconfigure_thread_.join();
    }

    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();
    const bool english = is_english_;

    epsilon_reconfigure_in_progress_ = true;
    showBusyStatusTaskProgress(english ? "Reconfiguring EPSILON..." : "正在重配 EPSILON...");
    updateConnectionStatus(is_connected_);
    log(QString(english ? "[EPSILON] Starting manual output reconfiguration: %1 @ %2, %3 profile (%4)"
                        : "[EPSILON] 开始手动重配输出: %1 @ %2，使用%3配置（%4）")
            .arg(epsilonPort, epsilonBaudText)
            .arg(usingCustomPacketProfile ? (english ? "custom packet-rate" : "自定义包频率")
                                          : (english ? "grouped output-rate" : "分组输出频率"))
            .arg(desiredPacketRateSummary));

    epsilon_reconfigure_thread_ = std::thread([this,
                                               english,
                                               epsilonPort,
                                               epsilonBaud,
                                               epsilonBaudText,
                                               epsilonRate,
                                               epsilonCallbackRate,
                                               desiredPacketRates,
                                               desiredPacketRateSignature,
                                               liveCollector,
                                               shouldRestartCollector]() {
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                epsilon_reconfigure_in_progress_ = false;
                hideStatusTaskProgress();
                updateConnectionStatus(anyCollectorRunning());
            }, Qt::QueuedConnection);
        };

        std::shared_ptr<VaporView::EpsilonCollector> collector = shouldRestartCollector && liveCollector
            ? liveCollector
            : std::make_shared<VaporView::EpsilonCollector>();

        collector->setEnglish(english);
        collector->setLogCallback([this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        });
        collector->setSampleRate(epsilonCallbackRate);

        if (shouldRestartCollector)
        {
            postLog(english
                        ? "[EPSILON] Temporarily stopping the live stream for manual reconfiguration."
                        : "[EPSILON] 为手动重配临时停止当前数据流。");
            collector->stop();
        }

        if (!collector->start(epsilonPort.toStdString(), VaporView::SerialConfig::N81(epsilonBaud)))
        {
            postLog(QString(english ? "[EPSILON] Failed to open %1 for manual reconfiguration: %2"
                                    : "[EPSILON] 打开 %1 进行手动重配失败: %2")
                        .arg(epsilonPort, QString::fromStdString(collector->getLastError())));
            finishOnUi();
            return;
        }

        if (!collector->setOutputPacketRates(desiredPacketRates, true))
        {
            postLog(QString(english ? "[EPSILON] Manual reconfiguration failed on %1 @ %2."
                                    : "[EPSILON] 在 %1 @ %2 上执行手动重配失败。")
                        .arg(epsilonPort, epsilonBaudText));
            collector->stop();
            finishOnUi();
            return;
        }

        {
            QSettings settings("VaporView", "MainWindow");
            settings.setValue("epsilon_last_config_port", epsilonPort);
            settings.setValue("epsilon_last_config_baud", epsilonBaudText);
            settings.setValue("epsilon_last_config_rate_hz", epsilonRate);
            settings.setValue("epsilon_last_config_signature", desiredPacketRateSignature);
            settings.setValue("epsilon_last_config_apply_version", kEpsilonPacketConfigApplyVersion);
        }

        if (shouldRestartCollector)
        {
            if (!collector->startStreaming())
            {
                postLog(english
                            ? "[EPSILON] Reconfiguration succeeded, but failed to restart the live navigation stream."
                            : "[EPSILON] 重配已完成，但重新启动实时导航流失败。");
                collector->stop();
                finishOnUi();
                return;
            }

            postLog(QString(english ? "[EPSILON] Manual reconfiguration completed and live stream restarted on %1."
                                    : "[EPSILON] 手动重配完成，已在 %1 上恢复实时数据流。")
                        .arg(epsilonPort));
        }
        else
        {
            collector->stop();
            postLog(QString(english ? "[EPSILON] Manual reconfiguration completed on %1. You can connect normally now."
                                    : "[EPSILON] 已在 %1 上完成手动重配，现在可以正常连接。")
                        .arg(epsilonPort));
        }

        finishOnUi();
    });
}
