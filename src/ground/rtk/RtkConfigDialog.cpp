#include "shared/theme/AppTheme.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "RtkConfigDialog.h"
#include "ground/widgets/CustomTitleBar.h"
#include "ground/widgets/VisualTextLabel.h"
#include "ground/widgets/LabelTextSelection.h"
#include "ground/widgets/SerialPortComboSupport.h"
#include "ground/widgets/WindowSizing.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QDialog>
#include <QCloseEvent>
#include <QEvent>
#include <QColor>
#include <QDoubleValidator>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QIcon>
#include <QIntValidator>
#include <QLabel>
#include <QLocale>
#include <QElapsedTimer>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSvgRenderer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextOption>
#include <QTimer>
#include <QTimeZone>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QWidgetAction>
#include <algorithm>
#include <cmath>
#include <utility>

#include "serial_probe_utils.h"

using VaporView::AppThemeColor;
using VaporView::appThemeColor;
using VaporView::appThemeColorName;
using VaporView::configureComboBoxPopup;
using VaporView::configureTopLevelCard;
using VaporView::isDarkThemeEnabled;
using VaporView::isDarkThemePalette;
using VaporView::updateTopLevelCardShadows;

namespace
{
constexpr int kGgaSendCycleMs = 1000;
constexpr int kGgaPollIntervalMs = kGgaSendCycleMs / 2;
constexpr int kGgaReconnectIntervalMs = 1500;
constexpr int kGgaStaleTimeoutMs = 1500;
constexpr int kGgaMaxVisibleLines = 200;
constexpr int kRtkHttpTimeoutMs = 5000;
constexpr int kRtkDefaultDialogWidth = 1024;
constexpr int kRtkDefaultDialogHeight = 640;
constexpr int kRtkMinimumDialogWidth = 640;
constexpr int kRtkMinimumDialogHeight = 420;
constexpr int kRtkInputHeight = 32;
constexpr int kEmbeddedTopLevelCardGap = 12;
constexpr int kEmbeddedTopLevelCardChromeInset = 12;
constexpr int kEmbeddedTopLevelCardOuterVerticalInset = 4;
constexpr int kEmbeddedMainContentLeftCardInset = 18;
constexpr qreal kEmbeddedTopLevelCardShadowSafeInsetRaw =
    VaporView::kTopLevelCardShadowBlurRadius * 0.6;
constexpr int kEmbeddedMainContentRightCardInset =
    static_cast<int>(kEmbeddedTopLevelCardShadowSafeInsetRaw) +
    (kEmbeddedTopLevelCardShadowSafeInsetRaw >
             static_cast<int>(kEmbeddedTopLevelCardShadowSafeInsetRaw)
         ? 1
         : 0) +
    1;
constexpr int kEmbeddedMainContentVerticalScrollBarWidth = 8;
constexpr int kEmbeddedMainContentRightInsetWithHiddenScrollBar =
    kEmbeddedMainContentRightCardInset + kEmbeddedMainContentVerticalScrollBarWidth;
constexpr const char *kEpsilonMainGgaSourceKey = "__epsilon_main__";
constexpr const char *kMountpointDetectFirstKey = "__mountpoint_detect_first__";
constexpr const char *kMountpointSelectKey = "__mountpoint_select__";
constexpr const char *kDefaultRtkCasterServer = "203.107.45.154";
constexpr const char *kDefaultRtkCasterPortWgs84 = "8002";
constexpr const char *kAlternateRtkCasterPortCgcs2000 = "8003";
constexpr const char *kLegacyDefaultRtkCasterPort = "2101";
constexpr const char *kSectionTitleIconNameProperty = "_vv_section_title_icon_name";
constexpr int kSectionTitleIconBoxSize = 26;
constexpr int kSectionTitleIconSize = 22;
const QRegularExpression kGgaSentencePattern("^\\$..GGA,");

QFont numericFontFrom(const QFont& base)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (base.pointSizeF() > 0.0)
    {
        font.setPointSizeF(base.pointSizeF());
    }
    font.setWeight(static_cast<QFont::Weight>(base.weight()));
    font.setBold(base.bold());
    return font;
}

QString boldLabelColorStyle(AppThemeColor color)
{
    return QStringLiteral("QLabel { color: %1; font-weight: bold; }")
        .arg(appThemeColorName(color, isDarkThemeEnabled()));
}

QString textForLanguage(bool english, const QString& englishText, const QString& chineseText)
{
    return english ? englishText : chineseText;
}

QString mountpointDetectFirstLabel(bool english)
{
    return textForLanguage(english, QStringLiteral("Detect first"), QStringLiteral("请先检测"));
}

QString mountpointSelectLabel(bool english)
{
    return textForLanguage(english, QStringLiteral("Select one"), QStringLiteral("请选择挂载点"));
}

bool isLoopbackServerText(const QString& server)
{
    const QString normalized = server.trimmed();
    return normalized.compare(QStringLiteral("127.0.0.1"), Qt::CaseInsensitive) == 0 ||
        normalized.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0 ||
        normalized == QStringLiteral("::1") ||
        normalized == QStringLiteral("[::1]");
}

bool isMountpointPlaceholderText(const QString& text)
{
    const QString trimmed = text.trimmed();
    return trimmed.isEmpty() ||
        trimmed == QStringLiteral("请先检测") ||
        trimmed == QStringLiteral("Detect first") ||
        trimmed == QStringLiteral("请选择挂载点") ||
        trimmed == QStringLiteral("Select one");
}

bool isAutoMountpointText(const QString& text)
{
    return text.trimmed().compare(QStringLiteral("AUTO"), Qt::CaseInsensitive) == 0;
}

bool hasConfirmedSavedMountpoint(const QString& mountpoint, bool confirmed)
{
    return confirmed &&
        !isMountpointPlaceholderText(mountpoint) &&
        !isAutoMountpointText(mountpoint);
}

bool shouldClearSavedLoopbackCaster(const QString& server,
                                    const QString& savedMountpoint,
                                    bool savedMountpointConfirmed)
{
    return isLoopbackServerText(server) &&
        !hasConfirmedSavedMountpoint(savedMountpoint, savedMountpointConfirmed);
}

QString defaultRtkCasterServer()
{
    return QString::fromLatin1(kDefaultRtkCasterServer);
}

QString defaultRtkCasterPort()
{
    return QString::fromLatin1(kDefaultRtkCasterPortWgs84);
}

QString alternateRtkCasterPort()
{
    return QString::fromLatin1(kAlternateRtkCasterPortCgcs2000);
}

QString legacyDefaultRtkCasterPort()
{
    return QString::fromLatin1(kLegacyDefaultRtkCasterPort);
}

QString selectedMountpointText(const QComboBox *combo)
{
    if (!combo)
    {
        return QString();
    }
    const QString text = combo->currentText().trimmed();
    if (isMountpointPlaceholderText(text))
    {
        return QString();
    }

    const QString key = combo->currentData().toString();
    if ((key == QString::fromLatin1(kMountpointDetectFirstKey) ||
         key == QString::fromLatin1(kMountpointSelectKey)) &&
        combo->currentIndex() >= 0 &&
        text == combo->itemText(combo->currentIndex()).trimmed())
    {
        return QString();
    }
    return text;
}

void setMountpointPrompt(QComboBox *combo, bool english, bool mountpointsLoaded)
{
    if (!combo)
    {
        return;
    }

    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem(mountpointsLoaded ? mountpointSelectLabel(english) : mountpointDetectFirstLabel(english),
                   QString::fromLatin1(mountpointsLoaded ? kMountpointSelectKey : kMountpointDetectFirstKey));
    combo->setCurrentIndex(0);
    if (QLineEdit *edit = combo->lineEdit())
    {
        edit->setCursorPosition(0);
    }
}

void refreshMountpointPromptText(QComboBox *combo, bool english)
{
    if (!combo)
    {
        return;
    }

    for (int index = 0; index < combo->count(); ++index)
    {
        const QString key = combo->itemData(index).toString();
        if (key == QString::fromLatin1(kMountpointDetectFirstKey))
        {
            combo->setItemText(index, mountpointDetectFirstLabel(english));
        }
        else if (key == QString::fromLatin1(kMountpointSelectKey))
        {
            combo->setItemText(index, mountpointSelectLabel(english));
        }
    }
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

    QIcon icon;
    icon.addPixmap(renderLucidePixmap(file.readAll(), color), QIcon::Normal);
    return icon;
}

QColor sectionTitleIconColor(bool dark)
{
    return dark ? appThemeColor(AppThemeColor::TextTitle, true) : QColor(0, 0, 0);
}

void updateSectionTitleIcon(QLabel *iconLabel, bool dark)
{
    if (!iconLabel)
    {
        return;
    }

    const QString iconName = iconLabel->property(kSectionTitleIconNameProperty).toString();
    if (iconName.isEmpty())
    {
        iconLabel->clear();
        return;
    }

    iconLabel->setPixmap(createLucideIcon(iconName, sectionTitleIconColor(dark)).pixmap(
        QSize(kSectionTitleIconSize, kSectionTitleIconSize)));
}

QLabel *createSectionTitleCluster(QWidget *parent,
                                  const QString& iconName,
                                  int titleHeight,
                                  QWidget **clusterOut)
{
    auto *cluster = new QWidget(parent);
    cluster->setObjectName(QStringLiteral("sectionTitleCluster"));
    cluster->setFixedHeight(titleHeight);
    cluster->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(cluster);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *iconLabel = new QLabel(cluster);
    iconLabel->setObjectName(QStringLiteral("sectionTitleIcon"));
    iconLabel->setProperty(kSectionTitleIconNameProperty, iconName);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(kSectionTitleIconBoxSize, titleHeight);
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    updateSectionTitleIcon(iconLabel, isDarkThemeEnabled());
    layout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto *titleLabel = new VaporView::VisualTextLabel(cluster);
    titleLabel->setObjectName(QStringLiteral("sectionTitleLabel"));
    VaporView::configureSelectableCardTitle(titleLabel);
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleLabel->setMargin(0);
    titleLabel->setContentsMargins(0, 0, 0, 0);
    titleLabel->setFixedHeight(titleHeight);
    titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(titleLabel, 0, Qt::AlignVCenter);

    if (clusterOut)
    {
        *clusterOut = cluster;
    }
    return titleLabel;
}

QStringList buildProbeBaudList(const QComboBox *baudrateCombo)
{
    QStringList baudTexts;
    if (baudrateCombo)
    {
        const QString currentText = baudrateCombo->currentText().trimmed();
        if (!currentText.isEmpty())
        {
            baudTexts.append(currentText);
        }

        for (int i = 0; i < baudrateCombo->count(); ++i)
        {
            const QString text = baudrateCombo->itemText(i).trimmed();
            if (!text.isEmpty())
            {
                baudTexts.append(text);
            }
        }
    }

    baudTexts.removeDuplicates();
    return baudTexts;
}

struct HttpResponse
{
    int statusCode = 0;
    QString body;
    QString error;
    bool timedOut = false;
};

struct MountpointFetchResult
{
    HttpResponse response;
    QStringList mountpoints;
};

struct NoSignalTestResult
{
    bool cancelled = false;
    bool gotResponse = false;
    bool linkReady = false;
    QString startError;
    QString runtimeError;
    QString finalMessage;
    qint64 inputBytes = 0;
    qint64 outputBytes = 0;
    qint64 receivedRtcmBytes = 0;
    bool generatedGga = false;
};

QString describeNoSignalTestFailure(const NoSignalTestResult& result, bool english)
{
    if (!result.runtimeError.isEmpty())
    {
        return result.runtimeError;
    }

    if (!result.linkReady)
    {
        return english
            ? QStringLiteral("The local loopback serial link did not become ready within timeout.")
            : QStringLiteral("超时时间内本地 loopback 模拟串口链路未进入可用状态。");
    }

    const QString rtklibMessage = result.finalMessage.trimmed();
    const bool inputDisconnected = rtklibMessage.contains(QStringLiteral("(0) disconnected"), Qt::CaseInsensitive);
    if (inputDisconnected)
    {
        return english
            ? QStringLiteral("The local 127.0.0.1 loopback link is only the mock serial used by this test and is already connected. "
                             "The NTRIP input stream is disconnected, so no RTCM data can be returned. "
                             "Check the caster address, port, mountpoint, account/password, and network access.\n"
                             "RTKLIB status: %1")
                  .arg(rtklibMessage)
            : QStringLiteral("127.0.0.1 是本次测试使用的本地 loopback 模拟串口，已经连上；真正断开的是 NTRIP 输入流，"
                             "因此不会有 RTCM 数据返回。请检查差分服务器地址、端口、挂载点、账号密码和网络连接。\n"
                             "RTKLIB 状态: %1")
                  .arg(rtklibMessage);
    }

    if (result.inputBytes <= 0)
    {
        if (result.generatedGga)
        {
            return english
                ? QStringLiteral("The EPSILON main-port position was sent to the NTRIP caster as GGA, but no bytes were received. "
                                 "Check whether the caster requires a different mountpoint, valid credentials, or a different rover position.\n"
                                 "RTKLIB status: %1")
                      .arg(rtklibMessage.isEmpty() ? QStringLiteral("--") : rtklibMessage)
                : QStringLiteral("已把 EPSILON 主串口定位组装成 GGA 发给 NTRIP 差分服务器，但没有收到任何字节。"
                                 "请检查挂载点、账号密码，或该服务是否要求不同的流动站位置。\n"
                                 "RTKLIB 状态: %1")
                      .arg(rtklibMessage.isEmpty() ? QStringLiteral("--") : rtklibMessage);
        }

        return english
            ? QStringLiteral("The test GGA was injected through the local loopback, but no bytes were received from the NTRIP caster. "
                             "Check whether the caster requires a different mountpoint, valid credentials, or a real rover position.\n"
                             "RTKLIB status: %1")
                  .arg(rtklibMessage.isEmpty() ? QStringLiteral("--") : rtklibMessage)
            : QStringLiteral("测试 GGA 已通过本地 loopback 注入，但没有从 NTRIP 差分服务器收到任何字节。"
                             "请检查挂载点、账号密码，或该服务是否要求真实流动站位置。\n"
                             "RTKLIB 状态: %1")
                  .arg(rtklibMessage.isEmpty() ? QStringLiteral("--") : rtklibMessage);
    }

    return rtklibMessage.isEmpty()
        ? (english
              ? QStringLiteral("No RTCM data returned within timeout.")
              : QStringLiteral("超时时间内未收到 RTCM 返回数据。"))
        : rtklibMessage;
}

QComboBox *createTimingComboBox(QWidget *parent, const QString &defaultValue)
{
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    combo->addItems({"1000", "2000", "5000", "10000", "30000", "60000"});
    combo->setCurrentText(defaultValue);
    configureComboBoxPopup(combo, isDarkThemeEnabled());
    if (combo->lineEdit())
    {
        combo->lineEdit()->setValidator(new QIntValidator(1000, 60000, combo));
    }
    return combo;
}

int comboIntValue(const QComboBox *combo, int defaultValue)
{
    if (!combo)
    {
        return defaultValue;
    }

    bool ok = false;
    const int value = combo->currentText().toInt(&ok);
    return ok ? value : defaultValue;
}

QString selectedSerialPortText(const QComboBox *combo)
{
    if (!combo)
    {
        return QString();
    }
    const QString text = combo->currentText().trimmed();
    return text == QStringLiteral("未选择") || text.startsWith(QStringLiteral("--"))
        ? QString()
        : text;
}

bool isUsableEpsilonNmeaPosition(const VaporView::EpsilonData &data)
{
    return data.valid &&
        std::isfinite(data.latitude_deg) &&
        std::isfinite(data.longitude_deg) &&
        std::isfinite(data.height_m) &&
        std::isfinite(data.hdop) &&
        std::abs(data.latitude_deg) <= 90.0 &&
        std::abs(data.longitude_deg) <= 180.0 &&
        std::abs(data.height_m) <= 20000.0 &&
        data.gnss_fix_code > 0 &&
        data.gnss_satellites >= 4 &&
        data.hdop > 0.0 &&
        data.hdop <= 50.0 &&
        (std::abs(data.latitude_deg) > 1e-9 || std::abs(data.longitude_deg) > 1e-9);
}

QString wrapNmeaSentence(const QString &body)
{
    unsigned char checksum = 0;
    const QByteArray bytes = body.toLatin1();
    for (char ch : bytes)
    {
        checksum ^= static_cast<unsigned char>(ch);
    }

    return QStringLiteral("$%1*%2")
        .arg(body)
        .arg(static_cast<int>(checksum), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString formatNmeaCoordinate(double degrees, int degreeWidth)
{
    const double absoluteDegrees = std::abs(degrees);
    const int wholeDegrees = static_cast<int>(std::floor(absoluteDegrees));
    const double minutes = (absoluteDegrees - wholeDegrees) * 60.0;
    QString minutesText = QString::number(minutes, 'f', 6);
    if (minutes < 10.0)
    {
        minutesText.prepend(QLatin1Char('0'));
    }

    return QStringLiteral("%1%2")
        .arg(wholeDegrees, degreeWidth, 10, QLatin1Char('0'))
        .arg(minutesText);
}

QString ggaTimeFieldFromEpsilon(const VaporView::EpsilonData &data)
{
    QDateTime utc = QDateTime::currentDateTimeUtc();
    if (data.utc_unix_s > 0)
    {
        utc = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(data.utc_unix_s), QTimeZone::UTC)
            .addMSecs(static_cast<qint64>(data.utc_microseconds / 1000));
    }

    const QTime time = utc.time();
    return QStringLiteral("%1%2%3.%4")
        .arg(time.hour(), 2, 10, QLatin1Char('0'))
        .arg(time.minute(), 2, 10, QLatin1Char('0'))
        .arg(time.second(), 2, 10, QLatin1Char('0'))
        .arg(time.msec() / 10, 2, 10, QLatin1Char('0'));
}

QString buildEpsilonGgaSentence(const VaporView::EpsilonData &data)
{
    if (!isUsableEpsilonNmeaPosition(data))
    {
        return {};
    }

    const QString latitude = formatNmeaCoordinate(data.latitude_deg, 2);
    const QString longitude = formatNmeaCoordinate(data.longitude_deg, 3);
    const QString northSouth = data.latitude_deg < 0.0 ? QStringLiteral("S") : QStringLiteral("N");
    const QString eastWest = data.longitude_deg < 0.0 ? QStringLiteral("W") : QStringLiteral("E");
    const int satellites = std::clamp(data.gnss_satellites, 0, 99);
    const double hdop = std::isfinite(data.hdop) && data.hdop > 0.0 ? data.hdop : 1.0;
    const double altitude = std::isfinite(data.height_m) ? data.height_m : 0.0;
    const double diffAge = std::isfinite(data.diff_age_s) && data.diff_age_s > 0.0 ? data.diff_age_s : 0.0;

    const QString body = QStringLiteral("GPGGA,%1,%2,%3,%4,%5,1,%6,%7,%8,M,0.0,M,%9,")
        .arg(ggaTimeFieldFromEpsilon(data),
             latitude,
             northSouth,
             longitude,
             eastWest)
        .arg(satellites, 2, 10, QLatin1Char('0'))
        .arg(QString::number(hdop, 'f', 1),
             QString::number(altitude, 'f', 3),
             diffAge > 0.0 ? QString::number(diffAge, 'f', 1) : QString());

    return wrapNmeaSentence(body);
}

QString buildMockGgaSentence()
{
    const QTime utc = QDateTime::currentDateTimeUtc().time();
    const QString timeField = QStringLiteral("%1%2%3.%4")
        .arg(utc.hour(), 2, 10, QLatin1Char('0'))
        .arg(utc.minute(), 2, 10, QLatin1Char('0'))
        .arg(utc.second(), 2, 10, QLatin1Char('0'))
        .arg(utc.msec() / 10, 2, 10, QLatin1Char('0'));

    const QString body = QStringLiteral(
        "GPGGA,%1,3000.0000,N,12000.0000,E,1,12,1.0,0.0,M,0.0,M,,")
        .arg(timeField);

    return wrapNmeaSentence(body);
}

QString formatRtcmDiagnostic(const RtkStreamStats &stats, bool english)
{
    if (stats.inputBytes <= 0 && stats.rtcmDiagnosticBytes == 0)
    {
        return english
            ? QStringLiteral("RTCM diagnostic: no caster bytes received yet")
            : QStringLiteral("RTCM诊断: 尚未收到服务器字节");
    }

    if (stats.rtcmDiagnosticBytes == 0)
    {
        return english
            ? QStringLiteral("RTCM diagnostic: waiting for raw input bytes")
            : QStringLiteral("RTCM诊断: 正在等待原始输入字节");
    }

    QStringList parts;
    parts << (english
        ? QStringLiteral("RTCM diagnostic: inspected %1 B").arg(stats.rtcmDiagnosticBytes)
        : QStringLiteral("RTCM诊断: 已检查 %1 B").arg(stats.rtcmDiagnosticBytes));

    if (stats.rtcm3FrameCount > 0)
    {
        parts << (english
            ? QStringLiteral("RTCM3/D3 frames %1, CRC ok %2, bad %3")
                  .arg(stats.rtcm3FrameCount)
                  .arg(stats.rtcm3CrcOkCount)
                  .arg(stats.rtcm3CrcFailCount)
            : QStringLiteral("RTCM3/D3帧 %1，CRC正确 %2，错误 %3")
                  .arg(stats.rtcm3FrameCount)
                  .arg(stats.rtcm3CrcOkCount)
                  .arg(stats.rtcm3CrcFailCount));
    }
    else
    {
        parts << (english
            ? QStringLiteral("no complete RTCM3/D3 frame yet")
            : QStringLiteral("尚无完整RTCM3/D3帧"));
    }

    if (stats.nonRtcmByteCount > 0)
    {
        parts << (english
            ? QStringLiteral("non-RTCM bytes %1").arg(stats.nonRtcmByteCount)
            : QStringLiteral("非RTCM字节 %1").arg(stats.nonRtcmByteCount));
    }

    if (stats.rtcm3PendingBytes > 0)
    {
        parts << (english
            ? QStringLiteral("pending %1 B").arg(stats.rtcm3PendingBytes)
            : QStringLiteral("待拼帧 %1 B").arg(stats.rtcm3PendingBytes));
    }

    if (!stats.rtcmMessageTypes.isEmpty())
    {
        parts << (english
            ? QStringLiteral("MT %1").arg(stats.rtcmMessageTypes)
            : QStringLiteral("消息类型 %1").arg(stats.rtcmMessageTypes));
    }

    if (!stats.firstInputHex.isEmpty())
    {
        parts << (english
            ? QStringLiteral("first bytes %1").arg(stats.firstInputHex)
            : QStringLiteral("首字节 %1").arg(stats.firstInputHex));
    }

    if (stats.rtcm3FrameCount == 0 &&
        !stats.firstInputAscii.isEmpty() &&
        stats.firstInputAscii.contains(QRegularExpression(QStringLiteral("[A-Za-z]"))))
    {
        parts << (english
            ? QStringLiteral("ASCII preview \"%1\"").arg(stats.firstInputAscii.left(48))
            : QStringLiteral("文本预览 \"%1\"").arg(stats.firstInputAscii.left(48)));
    }

    return parts.join(QStringLiteral("; "));
}

QString formatRtkStatusLine(const RtkStreamStats &stats, const QString &fallbackMessage, bool english)
{
    const QString message = stats.message.isEmpty() ? fallbackMessage : stats.message;
    QStringList lines;
    lines << QStringLiteral("%1 [%2]")
                 .arg(QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss"),
                      stats.streamStateMask.isEmpty() ? QStringLiteral("-----") : stats.streamStateMask);
    lines << (english
        ? QStringLiteral("  Input: %1 B    Rate: %2 bps")
              .arg(stats.inputBytes)
              .arg(stats.inputBps)
        : QStringLiteral("  输入: %1 B    速率: %2 bps")
              .arg(stats.inputBytes)
              .arg(stats.inputBps));
    lines << (english
        ? QStringLiteral("  Status: %1").arg(message)
        : QStringLiteral("  状态: %1").arg(message));

    const QString diagnostic = formatRtcmDiagnostic(stats, english);
    if (!diagnostic.isEmpty())
    {
        lines << (english ? QStringLiteral("  RTCM diagnostic:") : QStringLiteral("  RTCM诊断:"));
        const QString diagnosticPrefix = english ? QStringLiteral("RTCM diagnostic: ") : QStringLiteral("RTCM诊断: ");
        QString normalizedDiagnostic = diagnostic;
        if (normalizedDiagnostic.startsWith(diagnosticPrefix))
        {
            normalizedDiagnostic.remove(0, diagnosticPrefix.size());
        }
        const QStringList parts = normalizedDiagnostic.split(QStringLiteral("; "), Qt::SkipEmptyParts);
        for (const QString& part : parts)
        {
            lines << QStringLiteral("    - %1").arg(part.trimmed());
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QStringList splitLogFragments(const QString& text)
{
    const QStringList semicolonParts = text.split(QRegularExpression(QStringLiteral("[;；]")), Qt::SkipEmptyParts);
    if (semicolonParts.size() > 1)
    {
        QStringList result;
        for (const QString& part : semicolonParts)
        {
            const QString trimmed = part.trimmed();
            if (!trimmed.isEmpty())
            {
                result << trimmed;
            }
        }
        return result;
    }

    const QStringList commaParts = text.split(QRegularExpression(QStringLiteral("[,，]")), Qt::SkipEmptyParts);
    if (commaParts.size() >= 3)
    {
        QStringList result;
        for (const QString& part : commaParts)
        {
            const QString trimmed = part.trimmed();
            if (!trimmed.isEmpty())
            {
                result << trimmed;
            }
        }
        return result;
    }

    return {text.trimmed()};
}

QString formatLogMessageBlock(const QString& message)
{
    const QString normalized = message.trimmed();
    if (normalized.isEmpty())
    {
        return {};
    }

    QStringList formattedLines;
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
        {
            continue;
        }

        const int colonIndex = line.indexOf(QRegularExpression(QStringLiteral("[:：]")));
        if (colonIndex > 0 && colonIndex < 24)
        {
            const QString title = line.left(colonIndex + 1).trimmed();
            const QString details = line.mid(colonIndex + 1).trimmed();
            formattedLines << title;
            const QStringList fragments = splitLogFragments(details);
            for (const QString& fragment : fragments)
            {
                if (!fragment.isEmpty())
                {
                    formattedLines << QStringLiteral("  - %1").arg(fragment);
                }
            }
            continue;
        }

        formattedLines << line;
    }

    return formattedLines.join(QLatin1Char('\n'));
}

QUrl buildRtkUrl(const QString &server, const QString &port, const QString &path = QString())
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(server.trimmed());
    bool portOk = false;
    const int parsedPort = port.trimmed().toInt(&portOk);
    if (portOk)
    {
        url.setPort(parsedPort);
    }
    url.setPath(path.isEmpty() ? QStringLiteral("/") : QStringLiteral("/") + path);
    return url;
}

HttpResponse performRtkHttpGet(
    QObject *context,
    const QUrl &url,
    const QString &username,
    const QString &password,
    const QString &acceptHeader = QStringLiteral("*/*"))
{
    Q_UNUSED(context);

    HttpResponse result;
    QTcpSocket socket;

    const QString host = url.host().trimmed();
    const int port = url.port(80);
    QString path = url.path();
    if (path.isEmpty())
    {
        path = QStringLiteral("/");
    }
    if (!url.query().isEmpty())
    {
        path += QStringLiteral("?") + url.query();
    }

    socket.connectToHost(host, static_cast<quint16>(port));
    if (!socket.waitForConnected(kRtkHttpTimeoutMs))
    {
        result.error = socket.errorString();
        result.timedOut = (socket.error() == QAbstractSocket::SocketTimeoutError);
        return result;
    }

    QByteArray requestData;
    requestData += "GET " + path.toUtf8() + " HTTP/1.0\r\n";
    requestData += "Host: " + host.toUtf8() + ":" + QByteArray::number(port) + "\r\n";
    requestData += "User-Agent: NTRIP VaporView/1.0\r\n";
    requestData += "Ntrip-Version: Ntrip/2.0\r\n";
    requestData += "Connection: close\r\n";
    requestData += "Accept: " + acceptHeader.toUtf8() + "\r\n";

    if (!username.trimmed().isEmpty())
    {
        const QByteArray credentials = QStringLiteral("%1:%2").arg(username.trimmed(), password).toUtf8().toBase64();
        requestData += "Authorization: Basic " + credentials + "\r\n";
    }

    requestData += "\r\n";

    if (socket.write(requestData) != requestData.size() || !socket.waitForBytesWritten(kRtkHttpTimeoutMs))
    {
        result.error = socket.errorString();
        result.timedOut = (socket.error() == QAbstractSocket::SocketTimeoutError);
        return result;
    }

    QByteArray rawResponse;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kRtkHttpTimeoutMs)
    {
        if (socket.waitForReadyRead(200))
        {
            rawResponse += socket.readAll();
            while (socket.bytesAvailable() > 0)
            {
                rawResponse += socket.readAll();
            }
        }

        if (socket.state() == QAbstractSocket::UnconnectedState)
        {
            break;
        }
    }

    rawResponse += socket.readAll();
    socket.disconnectFromHost();

    if (rawResponse.isEmpty())
    {
        result.error = QStringLiteral("No response from server");
        return result;
    }

    auto parseStatusCode = [](const QByteArray &statusLine) {
        const QRegularExpression statusPattern(QStringLiteral("(^|\\s)(\\d{3})(\\s|$)"));
        const QRegularExpressionMatch match = statusPattern.match(QString::fromLatin1(statusLine));
        return match.hasMatch() ? match.captured(2).toInt() : 0;
    };

    const int headerEnd = rawResponse.indexOf("\r\n\r\n");
    QByteArray headerBytes = rawResponse;
    QByteArray bodyBytes;
    if (headerEnd >= 0)
    {
        headerBytes = rawResponse.left(headerEnd);
        bodyBytes = rawResponse.mid(headerEnd + 4);
    }

    const QList<QByteArray> headerLines = headerBytes.split('\n');
    const QByteArray statusLine = headerLines.isEmpty() ? QByteArray() : headerLines.first().trimmed();
    result.statusCode = parseStatusCode(statusLine);

    if (headerEnd >= 0)
    {
        result.body = QString::fromLatin1(bodyBytes);
    }
    else if (statusLine.startsWith("STR;") || statusLine.startsWith("CAS;") || statusLine.startsWith("NET;"))
    {
        result.statusCode = 200;
        result.body = QString::fromLatin1(rawResponse);
    }
    else
    {
        result.body = QString::fromLatin1(rawResponse);
    }

    return result;
}

QStringList parseMountpoints(const QString &responseBody)
{
    QStringList mountpoints;
    const QStringList lines = responseBody.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    for (const QString &line : lines)
    {
        if (!line.startsWith(QStringLiteral("STR;")))
        {
            continue;
        }

        const QStringList parts = line.split(';');
        if (parts.size() > 1 && !parts.at(1).trimmed().isEmpty())
        {
            mountpoints.append(parts.at(1).trimmed());
        }
    }

    mountpoints.removeDuplicates();
    mountpoints.sort();
    return mountpoints;
}
}

RtkConfigDialog::RtkConfigDialog(QWidget *parent, bool embedded)
    : QDialog(parent)
    , main_layout_(nullptr)
    , config_layout_(nullptr)
    , output_layout_(nullptr)
    , button_layout_(nullptr)
    , log_layout_(nullptr)
    , log_button_layout_(nullptr)
    , gga_layout_(nullptr)
    , gga_controls_layout_(nullptr)
    , gga_header_layout_(nullptr)
    , gga_text_container_layout_(nullptr)
    , log_text_container_layout_(nullptr)
    , gga_button_spacer_(nullptr)
    , config_group_(nullptr)
    , output_group_(nullptr)
    , gga_group_(nullptr)
    , log_group_(nullptr)
    , action_group_(nullptr)
    , config_title_label_(nullptr)
    , output_title_label_(nullptr)
    , gga_title_label_(nullptr)
    , log_title_label_(nullptr)
    , action_title_label_(nullptr)
    , action_status_widget_(nullptr)
    , gga_text_container_(nullptr)
    , gga_controls_container_(nullptr)
    , log_text_container_(nullptr)
    , server_label_(nullptr)
    , port_label_(nullptr)
    , username_label_(nullptr)
    , password_label_(nullptr)
    , mountpoint_label_(nullptr)
    , output_port_label_(nullptr)
    , baudrate_label_(nullptr)
    , main_antenna_lever_label_(nullptr)
    , timeout_label_(nullptr)
    , reconnect_label_(nullptr)
    , gga_port_info_label_(nullptr)
    , gga_status_label_(nullptr)
    , gga_frequency_label_(nullptr)
    , server_edit_(nullptr)
    , port_edit_(nullptr)
    , username_edit_(nullptr)
    , password_edit_(nullptr)
    , mountpoint_combo_(nullptr)
    , main_antenna_lever_x_edit_(nullptr)
    , main_antenna_lever_y_edit_(nullptr)
    , main_antenna_lever_z_edit_(nullptr)
    , output_port_combo_(nullptr)
    , baudrate_combo_(nullptr)
    , timeout_combo_(nullptr)
    , reconnect_combo_(nullptr)
    , gga_port_combo_(nullptr)
    , gga_text_edit_(nullptr)
    , log_text_edit_(nullptr)
    , start_btn_(nullptr)
    , stop_btn_(nullptr)
    , test_btn_(nullptr)
    , gga_toggle_btn_(nullptr)
    , refresh_ports_btn_(nullptr)
    , auto_detect_ports_btn_(nullptr)
    , fetch_mountpoints_btn_(nullptr)
    , main_antenna_lever_help_btn_(nullptr)
    , main_antenna_lever_help_popup_(nullptr)
    , main_antenna_lever_help_popup_label_(nullptr)
    , apply_main_antenna_lever_btn_(nullptr)
    , clear_log_btn_(nullptr)
    , status_icon_label_(nullptr)
    , status_label_(nullptr)
    , service_status_icon_name_(QStringLiteral("circle-x"))
    , service_status_color_(AppThemeColor::TextSecondary)
    , embedded_(embedded)
    , rtk_service_(std::make_unique<RtkStreamService>())
    , is_running_(false)
    , is_english_(false)
    , font_scale_percent_(100)
    , epsilon_main_baudrate_(921600)
    , base_dialog_size_(kRtkDefaultDialogWidth, kRtkDefaultDialogHeight)
    , base_minimum_dialog_size_(kRtkMinimumDialogWidth, kRtkMinimumDialogHeight)
    , rtk_status_timer_(nullptr)
    , gga_poll_timer_(nullptr)
    , last_rtk_status_message_()
    , gga_status_healthy_(false)
    , gga_last_open_attempt_()
    , gga_last_sentence_time_()
    , gga_last_epsilon_sample_time_()
    , gga_last_epsilon_device_timestamp_us_(0)
    , gga_has_sentence_time_(false)
    , gga_monitor_enabled_(false)
    , metrics_refresh_pending_(false)
{
    if (embedded_)
    {
        setWindowFlags(Qt::Widget);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    else
    {
        setWindowFlag(Qt::Window, true);
    }
    setObjectName(QStringLiteral("rtkConfigDialog"));
    setSizeGripEnabled(false);

    setupUi();
    if (!embedded_)
    {
        VaporView::installCustomTitleBar(this);
    }
    loadSettings();
    setFontScale(100);
    setEnglish(false);
    if (!embedded_)
    {
        VaporView::centerWindowOnScreen(this, parent);
    }

    config_file_path_ = QDir::homePath() + "/.config/VaporView/rtk_config.ini";

    rtk_status_timer_ = new QTimer(this);
    rtk_status_timer_->setInterval(1000);
    connect(rtk_status_timer_, &QTimer::timeout, this, &RtkConfigDialog::onRtkStatusTimer);

    gga_poll_timer_ = new QTimer(this);
    connect(gga_poll_timer_, &QTimer::timeout, this, &RtkConfigDialog::onGgaPollTimer);
    connect(baudrate_combo_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        stopGgaMonitor();
    });
    connect(gga_port_combo_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        stopGgaMonitor();
    });
}

RtkConfigDialog::~RtkConfigDialog()
{
    shutdown_requested_.store(true);
    if (rtk_status_timer_ && rtk_status_timer_->isActive())
    {
        rtk_status_timer_->stop();
    }
    if (rtk_service_)
    {
        if (is_running_ || rtk_service_->isRunning())
        {
            emit rtkRunningChanged(false);
        }
        is_running_ = false;
        rtk_service_->stop();
    }
    stopGgaMonitor();
    joinBackgroundTasks();
    saveSettings();
}

void RtkConfigDialog::closeEvent(QCloseEvent *event)
{
    saveSettings();
    if (embedded_)
    {
        event->ignore();
        return;
    }
    event->ignore();
    hide();
    if (is_running_ || gga_monitor_enabled_)
    {
        appendLog(textFor("RTK config window hidden; running tasks continue in background.",
                          "RTK 配置窗口已隐藏；运行中的任务会继续在后台执行。"));
    }
}

void RtkConfigDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (!event)
    {
        return;
    }

    if (event->type() != QEvent::ApplicationPaletteChange &&
        event->type() != QEvent::PaletteChange &&
        event->type() != QEvent::StyleChange &&
        event->type() != QEvent::FontChange)
    {
        return;
    }

    if (metrics_refresh_pending_)
    {
        return;
    }

    metrics_refresh_pending_ = true;
    QTimer::singleShot(0, this, [this]() {
        metrics_refresh_pending_ = false;
        applyScaledUiMetrics();
        updateGeometry();
        if (QLayout *dialogLayout = layout())
        {
            dialogLayout->invalidate();
            dialogLayout->activate();
        }
    });
}

void RtkConfigDialog::joinBackgroundTasks()
{
    if (fetch_mountpoints_thread_.joinable())
    {
        fetch_mountpoints_thread_.join();
    }
    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }
    if (test_thread_.joinable())
    {
        test_thread_.join();
    }
}

bool RtkConfigDialog::isBackgroundTaskRunning() const
{
    return fetch_mountpoints_in_progress_.load() ||
        port_detection_in_progress_.load() ||
        test_in_progress_.load() ||
        lever_arm_apply_in_progress_;
}

QVBoxLayout *RtkConfigDialog::createCardLayout(QGroupBox *group,
                                               QLabel *&titleLabel,
                                               const QString& iconName,
                                               QWidget **titleBarOut)
{
    group->setTitle(QString());
    group->setObjectName(QStringLiteral("sensorGroupBox"));
    configureTopLevelCard(group);
    group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *cardLayout = new QVBoxLayout(group);
    cardLayout->setContentsMargins(1, 0, 1, 1);
    cardLayout->setSpacing(0);

    auto *titleBar = new QWidget(group);
    titleBar->setObjectName(QStringLiteral("sectionTitleBar"));
    titleBar->setFixedHeight(40);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(8, 2, 8, 2);
    titleLayout->setSpacing(6);

    QWidget *titleCluster = nullptr;
    titleLabel = createSectionTitleCluster(titleBar, iconName, 36, &titleCluster);
    titleLayout->addWidget(titleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    titleLayout->addStretch(1);
    if (titleBarOut)
    {
        *titleBarOut = titleBar;
    }

    cardLayout->addWidget(titleBar);
    return cardLayout;
}

void RtkConfigDialog::setServiceStatus(const QString& text, const QString& iconName, AppThemeColor color)
{
    service_status_icon_name_ = iconName;
    service_status_color_ = color;
    if (status_label_)
    {
        status_label_->setText(text);
        status_label_->setToolTip(text);
    }
    refreshServiceStatusAppearance();
}

void RtkConfigDialog::refreshServiceStatusAppearance()
{
    const bool dark = isDarkThemeEnabled();
    const QColor statusColor = appThemeColor(service_status_color_, dark);
    if (status_icon_label_)
    {
        const int iconBoxSize = scalePixels(18);
        const int iconSize = std::max(14, scalePixels(16));
        status_icon_label_->setFixedSize(iconBoxSize, iconBoxSize);
        status_icon_label_->setPixmap(createLucideIcon(service_status_icon_name_, statusColor).pixmap(
            QSize(iconSize, iconSize)));
    }
    if (status_label_)
    {
        status_label_->setFixedHeight(scalePixels(24));
        status_label_->setStyleSheet(QStringLiteral(
            "QLabel#rtkStatusLabel { color: %1; font-weight: bold; background: transparent; border: none; padding: 0px; }")
            .arg(statusColor.name(QColor::HexRgb)));
    }
}

void RtkConfigDialog::setupUi()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("rtkConfigScrollArea"));
    scrollArea->viewport()->setObjectName(QStringLiteral("rtkConfigViewport"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outerLayout->addWidget(scrollArea);

    auto *contentWidget = new QWidget(scrollArea);
    contentWidget->setObjectName(QStringLiteral("rtkConfigContent"));
    scrollArea->setWidget(contentWidget);

    main_layout_ = new QVBoxLayout(contentWidget);
    main_layout_->setSpacing(kEmbeddedTopLevelCardGap);
    main_layout_->setContentsMargins(embedded_ ? kEmbeddedMainContentLeftCardInset
                                               : kEmbeddedTopLevelCardChromeInset,
                                     embedded_ ? kEmbeddedTopLevelCardOuterVerticalInset
                                               : kEmbeddedTopLevelCardChromeInset,
                                     embedded_ ? kEmbeddedMainContentRightInsetWithHiddenScrollBar
                                               : kEmbeddedTopLevelCardChromeInset,
                                     embedded_ ? kEmbeddedTopLevelCardOuterVerticalInset
                                               : kEmbeddedTopLevelCardChromeInset);
    main_layout_->setAlignment(Qt::AlignTop);

    auto configureFieldLabel = [](QLabel *label) {
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    };
    auto createFieldLabel = [this, configureFieldLabel](const QString& text = QString()) {
        auto *label = new QLabel(text, this);
        configureFieldLabel(label);
        return label;
    };

    config_group_ = new QGroupBox(this);
    auto *configCardLayout = createCardLayout(config_group_, config_title_label_, QStringLiteral("satellite"));
    config_group_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    config_layout_ = new QGridLayout();
    config_layout_->setSpacing(6);
    config_layout_->setContentsMargins(10, 10, 10, 10);
    config_layout_->setSizeConstraint(QLayout::SetFixedSize);
    configCardLayout->addLayout(config_layout_);

    int row = 0;
    server_label_ = createFieldLabel();
    config_layout_->addWidget(server_label_, row, 0);
    server_edit_ = new QLineEdit(this);
    server_edit_->setObjectName(QStringLiteral("rtkServerEdit"));
    config_layout_->addWidget(server_edit_, row, 1);

    port_label_ = createFieldLabel();
    config_layout_->addWidget(port_label_, row, 2);
    port_edit_ = new QLineEdit(this);
    port_edit_->setObjectName(QStringLiteral("rtkPortEdit"));
    port_edit_->setText(defaultRtkCasterPort());
    config_layout_->addWidget(port_edit_, row, 3);

    mountpoint_label_ = createFieldLabel();
    config_layout_->addWidget(mountpoint_label_, row, 4);
    auto *mountpointCombo = new VaporView::SingleLevelPopupComboBox(this);
    mountpointCombo->setShowSelectionCheck(false);
    mountpointCombo->setPopupFitContents(true);
    mountpoint_combo_ = mountpointCombo;
    mountpoint_combo_->setObjectName(QStringLiteral("rtkMountpointCombo"));
    mountpoint_combo_->setEditable(true);
    mountpoint_combo_->setInsertPolicy(QComboBox::NoInsert);
    setMountpointPrompt(mountpoint_combo_, is_english_, false);
    config_layout_->addWidget(mountpoint_combo_, row, 5);
    fetch_mountpoints_btn_ = new QPushButton(this);
    fetch_mountpoints_btn_->setObjectName(QStringLiteral("rtkFetchMountpointsButton"));
    connect(fetch_mountpoints_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onFetchMountpointsClicked);
    row++;

    username_label_ = createFieldLabel();
    config_layout_->addWidget(username_label_, row, 0);
    username_edit_ = new QLineEdit(this);
    username_edit_->setObjectName(QStringLiteral("rtkUsernameEdit"));
    config_layout_->addWidget(username_edit_, row, 1);

    password_label_ = createFieldLabel();
    config_layout_->addWidget(password_label_, row, 2);
    password_edit_ = new QLineEdit(this);
    password_edit_->setObjectName(QStringLiteral("rtkPasswordEdit"));
    config_layout_->addWidget(password_edit_, row, 3, 1, 2);
    config_layout_->addWidget(fetch_mountpoints_btn_, row, 5, Qt::AlignLeft | Qt::AlignVCenter);
    row++;

    output_group_ = new QGroupBox(this);
    auto *outputCardLayout = createCardLayout(output_group_, output_title_label_, QStringLiteral("usb"));
    output_group_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    output_layout_ = new QVBoxLayout();
    output_layout_->setSpacing(6);
    output_layout_->setContentsMargins(10, 10, 10, 10);
    output_layout_->setSizeConstraint(QLayout::SetFixedSize);
    outputCardLayout->addLayout(output_layout_);

    auto createOutputRow = [this]() {
        auto *rowWidget = new QWidget(this);
        rowWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);
        return std::pair<QWidget *, QHBoxLayout *>(rowWidget, rowLayout);
    };

    auto firstOutputRow = createOutputRow();
    output_port_label_ = createFieldLabel();
    firstOutputRow.second->addWidget(output_port_label_);
    output_port_combo_ = new QComboBox(this);
    output_port_combo_->setObjectName(QStringLiteral("rtkOutputPortCombo"));
    output_port_combo_->setEditable(true);
    configureComboBoxPopup(output_port_combo_, isDarkThemeEnabled());
    VaporView::installSerialPortPopupDelegate(output_port_combo_);
    firstOutputRow.second->addWidget(output_port_combo_);

    baudrate_label_ = createFieldLabel();
    firstOutputRow.second->addWidget(baudrate_label_);
    baudrate_combo_ = new QComboBox(this);
    baudrate_combo_->setObjectName(QStringLiteral("rtkBaudrateCombo"));
    baudrate_combo_->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"});
    baudrate_combo_->setCurrentText("115200");
    configureComboBoxPopup(baudrate_combo_, isDarkThemeEnabled());
    firstOutputRow.second->addWidget(baudrate_combo_);
    output_layout_->addWidget(firstOutputRow.first, 0, Qt::AlignLeft);

    auto secondOutputRow = createOutputRow();
    timeout_label_ = createFieldLabel();
    secondOutputRow.second->addWidget(timeout_label_);
    timeout_combo_ = createTimingComboBox(this, "5000");
    timeout_combo_->setObjectName(QStringLiteral("rtkTimeoutCombo"));
    secondOutputRow.second->addWidget(timeout_combo_);

    reconnect_label_ = createFieldLabel();
    secondOutputRow.second->addWidget(reconnect_label_);
    reconnect_combo_ = createTimingComboBox(this, "1000");
    reconnect_combo_->setObjectName(QStringLiteral("rtkReconnectCombo"));
    secondOutputRow.second->addWidget(reconnect_combo_);
    output_layout_->addWidget(secondOutputRow.first, 0, Qt::AlignLeft);

    auto *lever_label_widget = new QWidget(this);
    lever_label_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *lever_label_layout = new QHBoxLayout(lever_label_widget);
    lever_label_layout->setContentsMargins(0, 0, 0, 0);
    lever_label_layout->setSpacing(3);
    main_antenna_lever_label_ = createFieldLabel();
    lever_label_layout->addWidget(main_antenna_lever_label_);
    main_antenna_lever_help_btn_ = new QToolButton(this);
    main_antenna_lever_help_btn_->setObjectName(QStringLiteral("rtkLeverHelpButton"));
    main_antenna_lever_help_btn_->setIcon(createLucideIcon(
        QStringLiteral("help-circle"),
        appThemeColor(AppThemeColor::Primary, isDarkThemeEnabled())));
    main_antenna_lever_help_btn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    main_antenna_lever_help_btn_->setAutoRaise(true);
    main_antenna_lever_help_btn_->setFocusPolicy(Qt::NoFocus);
    main_antenna_lever_help_btn_->setCursor(Qt::PointingHandCursor);
    connect(main_antenna_lever_help_btn_, &QToolButton::clicked, this, &RtkConfigDialog::onMainAntennaLeverHelpClicked);
    lever_label_layout->addWidget(main_antenna_lever_help_btn_);

    auto *lever_edit_widget = new QWidget(this);
    lever_edit_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *lever_edit_layout = new QHBoxLayout(lever_edit_widget);
    lever_edit_layout->setContentsMargins(0, 0, 0, 0);
    lever_edit_layout->setSpacing(3);
    auto createLeverEdit = [this]() {
        auto *edit = new QLineEdit(this);
        auto *validator = new QDoubleValidator(-10000.0, 10000.0, 4, edit);
        validator->setNotation(QDoubleValidator::StandardNotation);
        edit->setValidator(validator);
        edit->setAlignment(Qt::AlignRight);
        edit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return edit;
    };
    lever_edit_layout->addWidget(createFieldLabel(QStringLiteral("X")));
    main_antenna_lever_x_edit_ = createLeverEdit();
    main_antenna_lever_x_edit_->setObjectName(QStringLiteral("rtkLeverXEdit"));
    lever_edit_layout->addWidget(main_antenna_lever_x_edit_);
    lever_edit_layout->addWidget(createFieldLabel(QStringLiteral("Y")));
    main_antenna_lever_y_edit_ = createLeverEdit();
    main_antenna_lever_y_edit_->setObjectName(QStringLiteral("rtkLeverYEdit"));
    lever_edit_layout->addWidget(main_antenna_lever_y_edit_);
    lever_edit_layout->addWidget(createFieldLabel(QStringLiteral("Z")));
    main_antenna_lever_z_edit_ = createLeverEdit();
    main_antenna_lever_z_edit_->setObjectName(QStringLiteral("rtkLeverZEdit"));
    lever_edit_layout->addWidget(main_antenna_lever_z_edit_);

    apply_main_antenna_lever_btn_ = new QPushButton(this);
    apply_main_antenna_lever_btn_->setObjectName(QStringLiteral("rtkApplyLeverArmButton"));
    connect(apply_main_antenna_lever_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onApplyMainAntennaLeverArmClicked);

    refresh_ports_btn_ = new QPushButton(this);
    refresh_ports_btn_->setObjectName(QStringLiteral("rtkRefreshPortsButton"));
    connect(refresh_ports_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onRefreshPortsClicked);

    auto_detect_ports_btn_ = new QPushButton(this);
    auto_detect_ports_btn_->setObjectName(QStringLiteral("rtkAutoDetectPortsButton"));
    connect(auto_detect_ports_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onAutoDetectPortsClicked);

    auto leverOutputRow = createOutputRow();
    leverOutputRow.second->addWidget(lever_label_widget);
    leverOutputRow.second->addWidget(lever_edit_widget);
    output_layout_->addWidget(leverOutputRow.first, 0, Qt::AlignLeft);

    auto buttonOutputRow = createOutputRow();
    buttonOutputRow.second->addWidget(apply_main_antenna_lever_btn_);
    buttonOutputRow.second->addWidget(refresh_ports_btn_);
    buttonOutputRow.second->addWidget(auto_detect_ports_btn_);
    output_layout_->addWidget(buttonOutputRow.first, 0, Qt::AlignLeft);

    gga_group_ = new QGroupBox(this);
    auto *ggaCardLayout = createCardLayout(gga_group_, gga_title_label_, QStringLiteral("activity"));
    gga_layout_ = new QHBoxLayout();
    gga_layout_->setSpacing(6);
    gga_layout_->setContentsMargins(10, 10, 10, 12);
    gga_group_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ggaCardLayout->addLayout(gga_layout_);

    gga_controls_container_ = new QWidget(gga_group_);
    gga_controls_container_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    gga_controls_layout_ = new QVBoxLayout(gga_controls_container_);
    gga_controls_layout_->setContentsMargins(0, 0, 0, 0);
    gga_controls_layout_->setSpacing(4);

    gga_header_layout_ = new QGridLayout();
    gga_header_layout_->setHorizontalSpacing(8);
    gga_header_layout_->setVerticalSpacing(4);
    gga_header_layout_->setColumnStretch(0, 0);
    gga_header_layout_->setColumnStretch(1, 0);

    gga_port_info_label_ = createFieldLabel();
    gga_port_info_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    gga_header_layout_->addWidget(gga_port_info_label_, 0, 0);

    gga_port_combo_ = new QComboBox(this);
    gga_port_combo_->setObjectName(QStringLiteral("rtkGgaPortCombo"));
    gga_port_combo_->setEditable(true);
    configureComboBoxPopup(gga_port_combo_, isDarkThemeEnabled());
    VaporView::installSerialPortPopupDelegate(gga_port_combo_);
    gga_header_layout_->addWidget(gga_port_combo_, 0, 1);

    gga_toggle_btn_ = new QPushButton(this);
    gga_toggle_btn_->setObjectName(QStringLiteral("rtkGgaToggleButton"));
    connect(gga_toggle_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onGgaToggleClicked);
    gga_header_layout_->addWidget(gga_toggle_btn_, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);

    gga_frequency_label_ = new VaporView::VisualTextLabel(this);
    gga_frequency_label_->setObjectName(QStringLiteral("fieldLabel"));
    gga_frequency_label_->setFont(numericFontFrom(gga_frequency_label_->font()));
    const QFontMetrics ggaFrequencyMetrics(gga_frequency_label_->font());
    gga_frequency_label_->setFixedWidth(
        std::max(
            ggaFrequencyMetrics.horizontalAdvance(QStringLiteral("Rate: -999.99 Hz")),
            ggaFrequencyMetrics.horizontalAdvance(QStringLiteral("频率: -999.99 Hz"))) + scalePixels(8));
    gga_frequency_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    gga_header_layout_->addWidget(gga_frequency_label_, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
    gga_controls_layout_->addLayout(gga_header_layout_);

    gga_status_label_ = new QLabel(this);
    gga_status_label_->setObjectName(QStringLiteral("rtkGgaStatusLabel"));
    gga_status_label_->setWordWrap(true);
    gga_controls_layout_->addWidget(gga_status_label_);
    gga_controls_layout_->addStretch(1);
    gga_layout_->addWidget(gga_controls_container_, 0, Qt::AlignTop | Qt::AlignLeft);

    gga_text_container_ = new QWidget(gga_group_);
    gga_text_container_->setObjectName(QStringLiteral("rtkGgaOutputContainer"));
    gga_text_container_layout_ = new QVBoxLayout(gga_text_container_);
    gga_text_container_layout_->setContentsMargins(0, 0, 0, 0);
    gga_text_container_layout_->setSpacing(0);

    gga_text_edit_ = new QTextEdit(gga_text_container_);
    gga_text_edit_->setObjectName(QStringLiteral("rtkGgaTextEdit"));
    gga_text_edit_->setReadOnly(true);
    gga_text_edit_->document()->setMaximumBlockCount(kGgaMaxVisibleLines);
    gga_text_container_layout_->addWidget(gga_text_edit_);
    gga_layout_->addWidget(gga_text_container_, 1);

    auto *topRowWidget = new QWidget(this);
    topRowWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *topRowLayout = new QHBoxLayout(topRowWidget);
    topRowLayout->setContentsMargins(0, 0, 0, 0);
    topRowLayout->setSpacing(kEmbeddedTopLevelCardGap);
    topRowLayout->setAlignment(Qt::AlignTop);
    topRowLayout->addWidget(config_group_, 0, Qt::AlignTop | Qt::AlignLeft);
    topRowLayout->addWidget(gga_group_, 1);
    main_layout_->addWidget(topRowWidget);

    log_group_ = new QGroupBox(this);
    auto *logCardLayout = createCardLayout(log_group_, log_title_label_, QStringLiteral("scroll-text"));
    log_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    log_layout_ = new QVBoxLayout();
    log_layout_->setSpacing(4);
    log_layout_->setContentsMargins(10, 10, 10, 4);
    logCardLayout->addLayout(log_layout_);

    log_text_container_ = new QWidget(log_group_);
    log_text_container_layout_ = new QVBoxLayout(log_text_container_);
    log_text_container_layout_->setContentsMargins(0, 0, 0, 2);
    log_text_container_layout_->setSpacing(0);

    log_text_edit_ = new QTextEdit(log_text_container_);
    log_text_edit_->setObjectName(QStringLiteral("rtkServiceLogTextEdit"));
    log_text_edit_->setReadOnly(true);
    log_text_edit_->setLineWrapMode(QTextEdit::WidgetWidth);
    log_text_edit_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    log_text_container_layout_->addWidget(log_text_edit_);
    log_layout_->addWidget(log_text_container_);

    auto *rtcmLogRowWidget = new QWidget(this);
    rtcmLogRowWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *rtcmLogRowLayout = new QHBoxLayout(rtcmLogRowWidget);
    rtcmLogRowLayout->setContentsMargins(0, 0, 0, 0);
    rtcmLogRowLayout->setSpacing(kEmbeddedTopLevelCardGap);
    rtcmLogRowLayout->setAlignment(Qt::AlignTop);
    rtcmLogRowLayout->addWidget(output_group_, 0, Qt::AlignTop | Qt::AlignLeft);
    rtcmLogRowLayout->addWidget(log_group_, 1, Qt::AlignTop);
    main_layout_->addWidget(rtcmLogRowWidget);

    action_group_ = new QGroupBox(this);
    QWidget *actionTitleBar = nullptr;
    auto *actionCardLayout = createCardLayout(action_group_,
                                              action_title_label_,
                                              QStringLiteral("play"),
                                              &actionTitleBar);
    action_status_widget_ = new QWidget(actionTitleBar);
    action_status_widget_->setObjectName(QStringLiteral("rtkActionStatusGroup"));
    action_status_widget_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    action_status_widget_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *actionStatusLayout = new QHBoxLayout(action_status_widget_);
    actionStatusLayout->setContentsMargins(0, 0, 0, 0);
    actionStatusLayout->setSpacing(4);
    status_icon_label_ = new QLabel(action_status_widget_);
    status_icon_label_->setObjectName(QStringLiteral("rtkStatusIcon"));
    status_icon_label_->setAlignment(Qt::AlignCenter);
    actionStatusLayout->addWidget(status_icon_label_, 0, Qt::AlignVCenter);
    status_label_ = new QLabel(action_status_widget_);
    status_label_->setObjectName(QStringLiteral("rtkStatusLabel"));
    status_label_->setWordWrap(false);
    status_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    actionStatusLayout->addWidget(status_label_, 0, Qt::AlignVCenter);
    if (auto *actionTitleLayout = actionTitleBar ? qobject_cast<QHBoxLayout *>(actionTitleBar->layout()) : nullptr)
    {
        actionTitleLayout->insertWidget(std::max(1, actionTitleLayout->count() - 1),
                                        action_status_widget_,
                                        0,
                                        Qt::AlignVCenter | Qt::AlignLeft);
    }
    action_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button_layout_ = new QHBoxLayout();
    button_layout_->setSpacing(6);
    button_layout_->setContentsMargins(8, 8, 8, 8);

    start_btn_ = new QPushButton(this);
    connect(start_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onStartClicked);

    stop_btn_ = new QPushButton(this);
    stop_btn_->setEnabled(false);
    connect(stop_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onStopClicked);

    test_btn_ = new QPushButton(this);
    connect(test_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onTestClicked);

    clear_log_btn_ = new QPushButton(this);
    connect(clear_log_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onClearLogClicked);

    button_layout_->addWidget(start_btn_);
    button_layout_->addWidget(stop_btn_);
    button_layout_->addWidget(test_btn_);
    button_layout_->addWidget(clear_log_btn_);
    button_layout_->addStretch();

    actionCardLayout->addLayout(button_layout_);
    main_layout_->addWidget(action_group_);
}

QString RtkConfigDialog::textFor(const QString& english, const QString& chinese) const
{
    return is_english_ ? english : chinese;
}

void RtkConfigDialog::setEnglish(bool english)
{
    is_english_ = english;
    refreshPortCombos();

    setWindowTitle(textFor("RTK NTRIP Configuration", "RTK NTRIP 配置"));
    if (config_title_label_) config_title_label_->setText(textFor("NTRIP Server Configuration", "NTRIP 服务器配置"));
    if (output_title_label_) output_title_label_->setText(textFor("RTCM Output Configuration", "RTCM 输出配置"));
    if (gga_title_label_) gga_title_label_->setText(textFor("GGA Monitor", "GGA 监视"));
    if (log_title_label_) log_title_label_->setText(textFor("RTK Service Log", "RTK 服务日志"));
    if (action_title_label_) action_title_label_->setText(textFor("Service Actions", "服务操作"));

    server_label_->setText(textFor("Server Address:", "服务器地址:"));
    port_label_->setText(textFor("Port:", "端口:"));
    username_label_->setText(textFor("Username:", "用户名:"));
    password_label_->setText(textFor("Password:", "密码:"));
    mountpoint_label_->setText(textFor("Mountpoint:", "挂载点:"));
    output_port_label_->setText(textFor("Output Port:", "输出串口:"));
    baudrate_label_->setText(textFor("Baudrate:", "波特率:"));
    main_antenna_lever_label_->setText(textFor("Main Antenna Lever Arm (m):", "主天线杆臂 (m):"));
    timeout_label_->setText(textFor("Timeout (ms):", "超时 (ms):"));
    reconnect_label_->setText(textFor("Reconnect (ms):", "重连间隔 (ms):"));

    server_edit_->setPlaceholderText(defaultRtkCasterServer());
    port_edit_->setPlaceholderText(textFor(QStringLiteral("%1 = WGS84, %2 = CGCS2000")
                                               .arg(defaultRtkCasterPort(), alternateRtkCasterPort()),
                                           QStringLiteral("%1 = WGS84，%2 = CGCS2000")
                                               .arg(defaultRtkCasterPort(), alternateRtkCasterPort())));
    port_edit_->setToolTip(textFor(QStringLiteral("Default: %1 (WGS84). Use %2 only if the downstream workflow requires CGCS2000.")
                                       .arg(defaultRtkCasterPort(), alternateRtkCasterPort()),
                                   QStringLiteral("默认使用 %1（WGS84）。只有下游流程明确要求 CGCS2000 时才改用 %2。")
                                       .arg(defaultRtkCasterPort(), alternateRtkCasterPort())));
    if (mountpoint_combo_->lineEdit())
    {
        mountpoint_combo_->lineEdit()->setPlaceholderText(textFor("Detect first or type a mountpoint",
                                                                  "请先检测，或手动输入挂载点"));
    }
    refreshMountpointPromptText(mountpoint_combo_, is_english_);
    main_antenna_lever_x_edit_->setPlaceholderText(textFor("forward", "前向"));
    main_antenna_lever_y_edit_->setPlaceholderText(textFor("right", "右向"));
    main_antenna_lever_z_edit_->setPlaceholderText(textFor("down", "下向"));
    main_antenna_lever_help_btn_->setToolTip(mainAntennaLeverArmHelpText());

    refresh_ports_btn_->setText(textFor("Refresh", "刷新"));
    auto_detect_ports_btn_->setText(textFor("Auto Detect", "自动识别"));
    fetch_mountpoints_btn_->setText(textFor("Detect Mountpoints", "检测挂载点"));
    apply_main_antenna_lever_btn_->setText(textFor("Apply Lever Arm", "下发杆臂"));
    start_btn_->setText(textFor("Start", "启动"));
    stop_btn_->setText(textFor("Stop", "停止"));
    test_btn_->setText(textFor("Test Connection", "测试连接"));
    clear_log_btn_->setText(textFor("Clear Log", "清空日志"));

    updateGgaMonitorText();
    updateGgaMonitorButton();
    applyScaledUiMetrics();
    updateButtonStates();
}

int RtkConfigDialog::scalePixels(int pixels) const
{
    return static_cast<int>(std::lround(pixels * font_scale_percent_ / 100.0));
}

void RtkConfigDialog::applyScaledUiMetrics()
{
    auto applyButtonWidth = [this](QPushButton *button, int baseWidth) {
        if (!button)
        {
            return;
        }

        const QFontMetrics metrics(button->font());
        const int textWidth = metrics.horizontalAdvance(button->text());
        const int targetWidth = std::max(scalePixels(baseWidth), textWidth + scalePixels(28));
        const int targetHeight = scalePixels(kRtkInputHeight);
        button->setFixedWidth(targetWidth);
        button->setFixedHeight(targetHeight);
        button->setStyleSheet(QString(
            "QPushButton { padding: 0px %1px; min-height: %2px; max-height: %2px; }")
            .arg(scalePixels(10))
            .arg(targetHeight));
    };
    auto applyComboWidth = [this](QComboBox *combo, int baseWidth) {
        if (!combo)
        {
            return;
        }

        const int targetWidth = scalePixels(baseWidth);
        combo->setFixedWidth(targetWidth);
        combo->setFixedHeight(scalePixels(kRtkInputHeight));
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    };
    auto applyFieldLabelWidth = [this](QLabel *label, int baseWidth) {
        if (!label)
        {
            return;
        }

        label->setFixedWidth(scalePixels(baseWidth));
        label->setMinimumHeight(scalePixels(30));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    };
    auto applyFieldLabelContentWidth = [this](QLabel *label) {
        if (!label)
        {
            return;
        }

        const QFontMetrics metrics(label->font());
        label->setFixedWidth(metrics.horizontalAdvance(label->text()) + scalePixels(10));
        label->setMinimumHeight(scalePixels(kRtkInputHeight));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    };

    if (main_layout_)
    {
        main_layout_->setSpacing(scalePixels(kEmbeddedTopLevelCardGap));
        main_layout_->setContentsMargins(scalePixels(embedded_
                                             ? kEmbeddedMainContentLeftCardInset
                                             : kEmbeddedTopLevelCardChromeInset),
                                         scalePixels(embedded_
                                             ? kEmbeddedTopLevelCardOuterVerticalInset
                                             : kEmbeddedTopLevelCardChromeInset),
                                         scalePixels(embedded_
                                             ? kEmbeddedMainContentRightInsetWithHiddenScrollBar
                                             : kEmbeddedTopLevelCardChromeInset),
                                         scalePixels(embedded_
                                             ? kEmbeddedTopLevelCardOuterVerticalInset
                                             : kEmbeddedTopLevelCardChromeInset));
        main_layout_->setAlignment(Qt::AlignTop);
    }

    if (config_layout_)
    {
        config_layout_->setHorizontalSpacing(scalePixels(5));
        config_layout_->setVerticalSpacing(scalePixels(4));
        config_layout_->setContentsMargins(scalePixels(8), scalePixels(4), scalePixels(8), scalePixels(8));
        for (int row = 0; row < 2; ++row)
        {
            config_layout_->setRowMinimumHeight(row, scalePixels(34));
        }
    }

    if (output_layout_)
    {
        output_layout_->setSpacing(scalePixels(6));
        output_layout_->setContentsMargins(scalePixels(8), scalePixels(8), scalePixels(8), scalePixels(8));
        output_layout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    }

    if (button_layout_)
    {
        button_layout_->setSpacing(scalePixels(6));
        button_layout_->setContentsMargins(scalePixels(8), scalePixels(8), scalePixels(8), scalePixels(8));
    }

    if (gga_layout_)
    {
        gga_layout_->setSpacing(scalePixels(5));
        gga_layout_->setContentsMargins(scalePixels(8), scalePixels(8), scalePixels(8), scalePixels(8));
    }

    if (gga_controls_layout_)
    {
        gga_controls_layout_->setSpacing(scalePixels(4));
        gga_controls_layout_->setContentsMargins(0, 0, 0, 0);
    }

    if (gga_text_container_layout_)
    {
        gga_text_container_layout_->setContentsMargins(0, 0, 0, 0);
    }

    if (gga_button_spacer_)
    {
        gga_button_spacer_->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    }

    if (gga_header_layout_)
    {
        gga_header_layout_->setHorizontalSpacing(scalePixels(6));
        gga_header_layout_->setVerticalSpacing(scalePixels(4));
    }

    if (log_layout_)
    {
        log_layout_->setSpacing(scalePixels(4));
        log_layout_->setContentsMargins(scalePixels(8), scalePixels(8), scalePixels(8), scalePixels(4));
    }

    if (log_text_container_layout_)
    {
        log_text_container_layout_->setContentsMargins(0, 0, 0, scalePixels(2));
        log_text_container_layout_->setSpacing(0);
    }

    const bool darkTheme = qApp ? isDarkThemePalette(qApp->palette()) : isDarkThemeEnabled();
    const QList<QLabel*> sectionIconLabels = findChildren<QLabel *>(QStringLiteral("sectionTitleIcon"));
    for (QLabel *iconLabel : sectionIconLabels)
    {
        updateSectionTitleIcon(iconLabel, darkTheme);
    }

    applyFieldLabelContentWidth(server_label_);
    applyFieldLabelContentWidth(username_label_);
    applyFieldLabelContentWidth(port_label_);
    applyFieldLabelContentWidth(password_label_);
    applyFieldLabelContentWidth(mountpoint_label_);
    server_edit_->setFixedWidth(scalePixels(140));
    server_edit_->setFixedHeight(scalePixels(kRtkInputHeight));
    port_edit_->setFixedWidth(scalePixels(76));
    port_edit_->setFixedHeight(scalePixels(kRtkInputHeight));
    username_edit_->setFixedWidth(scalePixels(140));
    username_edit_->setFixedHeight(scalePixels(kRtkInputHeight));
    const int passwordWidth =
        scalePixels(76) + (config_layout_ ? config_layout_->horizontalSpacing() : scalePixels(5)) +
        (mountpoint_label_ ? mountpoint_label_->width() : 0);
    password_edit_->setFixedWidth(std::max(scalePixels(140), passwordWidth));
    password_edit_->setFixedHeight(scalePixels(kRtkInputHeight));
    applyButtonWidth(fetch_mountpoints_btn_, 112);
    updateMountpointComboWidth();

    applyFieldLabelContentWidth(output_port_label_);
    applyFieldLabelContentWidth(baudrate_label_);
    applyFieldLabelContentWidth(timeout_label_);
    applyFieldLabelContentWidth(reconnect_label_);
    applyFieldLabelContentWidth(main_antenna_lever_label_);
    applyComboWidth(output_port_combo_, 96);
    applyComboWidth(baudrate_combo_, 112);
    if (main_antenna_lever_help_btn_)
    {
        const int helpSize = scalePixels(kRtkInputHeight);
        main_antenna_lever_help_btn_->setFixedSize(helpSize, helpSize);
        const int iconSize = scalePixels(24);
        main_antenna_lever_help_btn_->setIconSize(QSize(iconSize, iconSize));
        main_antenna_lever_help_btn_->setIcon(createLucideIcon(
            QStringLiteral("help-circle"),
            appThemeColor(AppThemeColor::Primary, darkTheme)));
        main_antenna_lever_help_btn_->setStyleSheet(
            QString("QToolButton { background-color: transparent; border: none; border-radius: 6px; padding: 0px; }"
                    "QToolButton:hover, QToolButton:pressed { background-color: %1; }")
                .arg(appThemeColorName(AppThemeColor::TitleBarHover, darkTheme)));
    }
    for (QLineEdit *edit : {main_antenna_lever_x_edit_, main_antenna_lever_y_edit_, main_antenna_lever_z_edit_})
    {
        if (edit)
        {
            edit->setFixedWidth(scalePixels(64));
            edit->setFixedHeight(scalePixels(kRtkInputHeight));
        }
    }
    applyComboWidth(timeout_combo_, 96);
    applyComboWidth(reconnect_combo_, 104);
    applyFieldLabelContentWidth(gga_port_info_label_);
    if (gga_port_info_label_)
    {
        gga_port_info_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (gga_port_combo_)
    {
        const QFontMetrics metrics(gga_port_combo_->font());
        const int targetWidth = metrics.horizontalAdvance(mainGgaSourceLabel()) + scalePixels(72);
        gga_port_combo_->setFixedWidth(targetWidth);
        gga_port_combo_->setFixedHeight(scalePixels(kRtkInputHeight));
        gga_port_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        if (QLineEdit *edit = gga_port_combo_->lineEdit())
        {
            edit->setCursorPosition(0);
            edit->setSelection(0, 0);
        }
    }
    applyButtonWidth(gga_toggle_btn_, 72);

    const bool ggaStatusVisible = gga_status_label_ && !gga_status_label_->text().trimmed().isEmpty();
    gga_status_label_->setVisible(ggaStatusVisible);
    gga_status_label_->setMinimumHeight(ggaStatusVisible ? scalePixels(24) : 0);
    const QMargins ggaMargins = gga_layout_ ? gga_layout_->contentsMargins() : QMargins();
    const int headerHeight = gga_header_layout_
        ? gga_header_layout_->sizeHint().height()
        : std::max({gga_port_info_label_->sizeHint().height(),
                    gga_port_combo_->sizeHint().height(),
                    gga_frequency_label_->sizeHint().height()});
    const int statusHeight = ggaStatusVisible
        ? std::max(gga_status_label_->minimumHeight(), gga_status_label_->sizeHint().height())
        : 0;
    const int controlsSpacing = gga_controls_layout_ ? gga_controls_layout_->spacing() : 0;
    const int controlsHeight = headerHeight + (ggaStatusVisible ? controlsSpacing + statusHeight : 0);
    if (gga_controls_container_)
    {
        gga_controls_container_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        gga_controls_container_->setFixedSize(gga_header_layout_ ? gga_header_layout_->sizeHint().width() : scalePixels(220),
                                              controlsHeight);
    }
    const int ggaTextHeight = std::max(scalePixels(72), controlsHeight);
    gga_text_edit_->setFixedHeight(ggaTextHeight);
    gga_text_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (gga_text_container_)
    {
        gga_text_container_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        gga_text_container_->setMinimumWidth(scalePixels(120));
        gga_text_container_->setFixedHeight(ggaTextHeight);
    }
    gga_text_edit_->document()->setDocumentMargin(scalePixels(8));
    const int bodyHeight = std::max(controlsHeight, ggaTextHeight);
    const int cardTitleBarHeight = 40;
    const int ggaGroupHeight = cardTitleBarHeight
        + ggaMargins.top()
        + bodyHeight
        + ggaMargins.bottom();
    gga_group_->setFixedHeight(ggaGroupHeight);

    applyButtonWidth(refresh_ports_btn_, 72);
    applyButtonWidth(auto_detect_ports_btn_, 88);
    applyButtonWidth(apply_main_antenna_lever_btn_, 112);
    applyButtonWidth(start_btn_, 80);
    applyButtonWidth(stop_btn_, 80);
    applyButtonWidth(test_btn_, 120);
    applyButtonWidth(clear_log_btn_, 96);

    const int logTextBottomGap = scalePixels(2);
    const QMargins logMargins = log_layout_ ? log_layout_->contentsMargins() : QMargins();
    int logGroupHeight =
        cardTitleBarHeight + logMargins.top() + scalePixels(72) + logTextBottomGap + logMargins.bottom();
    if (output_group_)
    {
        if (QLayout *outputGroupLayout = output_group_->layout())
        {
            outputGroupLayout->invalidate();
            outputGroupLayout->activate();
        }
        logGroupHeight = std::max(logGroupHeight, output_group_->sizeHint().height());
    }
    const int logDocumentMargin = scalePixels(8);
    const int logTextHeight = std::max(
        scalePixels(72),
        logGroupHeight - cardTitleBarHeight - logMargins.top() - logTextBottomGap - logMargins.bottom());
    log_text_edit_->setMinimumWidth(scalePixels(200));
    log_text_edit_->setFixedHeight(logTextHeight);
    log_text_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    log_text_edit_->document()->setDocumentMargin(logDocumentMargin);
    if (log_group_ && log_layout_)
    {
        log_group_->setFixedHeight(logGroupHeight);
        log_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    if (log_text_container_)
    {
        log_text_container_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        log_text_container_->setFixedHeight(logTextHeight + logTextBottomGap);
    }
    gga_text_edit_->setMinimumWidth(scalePixels(120));

    if (main_layout_)
    {
        main_layout_->invalidate();
    }
    refreshServiceStatusAppearance();

    const QSize minimumDialogSize(scalePixels(base_minimum_dialog_size_.width()), scalePixels(base_minimum_dialog_size_.height()));
    const QSize targetMinimumSize = minimumDialogSize;
    setMinimumSize(targetMinimumSize);
    if (!embedded_ && !isMaximized() && !isFullScreen())
    {
        const QSize preferredDialogSize(
            scalePixels(base_dialog_size_.width()),
            scalePixels(base_dialog_size_.height()));
        const QSize targetSize = preferredDialogSize.expandedTo(targetMinimumSize);
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }
}

void RtkConfigDialog::updateMountpointComboWidth()
{
    if (!mountpoint_combo_)
    {
        return;
    }

    const int targetHeight = scalePixels(kRtkInputHeight);
    const int targetWidth = std::max(scalePixels(112),
                                     fetch_mountpoints_btn_ ? fetch_mountpoints_btn_->width() : 0);

    mountpoint_combo_->setFixedWidth(targetWidth);
    mountpoint_combo_->setFixedHeight(targetHeight);
    mountpoint_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    if (fetch_mountpoints_btn_)
    {
        fetch_mountpoints_btn_->setFixedWidth(targetWidth);
        fetch_mountpoints_btn_->setFixedHeight(targetHeight);
    }
    if (auto *singleLevelCombo = dynamic_cast<VaporView::SingleLevelPopupComboBox *>(mountpoint_combo_))
    {
        singleLevelCombo->setPopupFitContents(true);
    }
}

void RtkConfigDialog::setFontScale(int percent)
{
    if (percent < 70 || percent > 150)
    {
        percent = 100;
    }

    QSize targetSize = size();
    if (!embedded_ && !isMaximized() && !isFullScreen())
    {
        const QSize scaledMinimumSize(
            std::max(1, static_cast<int>(std::lround(base_minimum_dialog_size_.width() * percent / 100.0))),
            std::max(1, static_cast<int>(std::lround(base_minimum_dialog_size_.height() * percent / 100.0))));
        targetSize = QSize(
            std::max(1, static_cast<int>(std::lround(base_dialog_size_.width() * percent / 100.0))),
            std::max(1, static_cast<int>(std::lround(base_dialog_size_.height() * percent / 100.0)))
        ).expandedTo(scaledMinimumSize);
    }

    if (font_scale_percent_ == percent)
    {
        applyScaledUiMetrics();
        updateTopLevelCardShadows(this, font_scale_percent_ / 100.0);
        if (!embedded_ && !isMaximized() && !isFullScreen())
        {
            targetSize = targetSize.expandedTo(minimumSize());
            if (targetSize != size())
            {
                resize(targetSize);
            }
        }
        return;
    }

    font_scale_percent_ = percent;
    applyScaledUiMetrics();
    updateTopLevelCardShadows(this, font_scale_percent_ / 100.0);
    if (!embedded_ && !isMaximized() && !isFullScreen())
    {
        targetSize = targetSize.expandedTo(minimumSize());
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }
}

void RtkConfigDialog::loadSettings()
{
    QSettings settings("VaporView", "RtkConfig");

    QString savedServer = settings.value("server", QString()).toString().trimmed();
    QString savedPort = settings.value("port", QString()).toString().trimmed();
    const QString savedMountpoint = settings.value("mountpoint", "").toString().trimmed();
    const bool savedMountpointConfirmed = settings.value("mountpoint_confirmed", false).toBool();
    if (shouldClearSavedLoopbackCaster(savedServer, savedMountpoint, savedMountpointConfirmed))
    {
        settings.remove("server");
        settings.remove("port");
        savedServer = defaultRtkCasterServer();
        savedPort = defaultRtkCasterPort();
    }
    if (savedServer.isEmpty())
    {
        savedServer = defaultRtkCasterServer();
    }
    if (savedPort.isEmpty() ||
        (savedServer.compare(defaultRtkCasterServer(), Qt::CaseInsensitive) == 0 &&
         savedPort.compare(legacyDefaultRtkCasterPort(), Qt::CaseInsensitive) == 0))
    {
        savedPort = defaultRtkCasterPort();
    }

    server_edit_->setText(savedServer);
    port_edit_->setText(savedPort);
    username_edit_->setText(settings.value("username", "").toString());
    password_edit_->setText(settings.value("password", "").toString());
    if (isMountpointPlaceholderText(savedMountpoint) || isAutoMountpointText(savedMountpoint))
    {
        setMountpointPrompt(mountpoint_combo_, is_english_, false);
    }
    else
    {
        mountpoint_combo_->setCurrentText(savedMountpoint);
    }
    main_antenna_lever_x_edit_->setText(settings.value("main_antenna_lever_x_m", "").toString());
    main_antenna_lever_y_edit_->setText(settings.value("main_antenna_lever_y_m", "").toString());
    main_antenna_lever_z_edit_->setText(settings.value("main_antenna_lever_z_m", "").toString());
    setPreferredOutputPortAndBaud(settings.value("output_port").toString(), QString());
    applySavedGgaSource(settings.value("gga_source", settings.value("gga_port", QString::fromLatin1(kEpsilonMainGgaSourceKey))).toString());
    baudrate_combo_->setCurrentText(settings.value("baudrate", "115200").toString());
    timeout_combo_->setCurrentText(settings.value("timeout", "5000").toString());
    reconnect_combo_->setCurrentText(settings.value("reconnect", "1000").toString());
    updateGgaMonitorText();
}

void RtkConfigDialog::saveSettings()
{
    QSettings settings("VaporView", "RtkConfig");

    settings.setValue("server", server_edit_->text());
    settings.setValue("port", port_edit_->text());
    settings.setValue("username", username_edit_->text());
    settings.setValue("password", password_edit_->text());
    const QString savedMountpoint = selectedMountpointText(mountpoint_combo_);
    settings.setValue("mountpoint", savedMountpoint);
    settings.setValue("mountpoint_confirmed", !savedMountpoint.isEmpty());
    settings.setValue("main_antenna_lever_x_m", main_antenna_lever_x_edit_->text());
    settings.setValue("main_antenna_lever_y_m", main_antenna_lever_y_edit_->text());
    settings.setValue("main_antenna_lever_z_m", main_antenna_lever_z_edit_->text());
    const QString savedOutputPort = selectedSerialPortText(output_port_combo_);
    VaporView::rememberSerialPort(savedOutputPort);
    settings.setValue("output_port", savedOutputPort);
    settings.setValue("gga_source", savedGgaSourceValue());
    const QString savedGgaPort = isMainGgaSourceSelected() ? QString() : ggaPortName();
    VaporView::rememberSerialPort(savedGgaPort);
    settings.setValue("gga_port", savedGgaPort);
    settings.setValue("baudrate", baudrate_combo_->currentText());
    settings.setValue("timeout", timeout_combo_->currentText());
    settings.setValue("reconnect", reconnect_combo_->currentText());
}

void RtkConfigDialog::setPreferredOutputPortAndBaud(const QString& portName, const QString& baudText)
{
    refreshPortCombos();
    if (output_port_combo_)
    {
        const QString preferredPort = portName.trimmed();
        int preferredIndex = -1;
        for (int index = 0; index < output_port_combo_->count(); ++index)
        {
            if (VaporView::serialPortNamesMatch(output_port_combo_->itemText(index), preferredPort))
            {
                preferredIndex = index;
                break;
            }
        }
        if (preferredIndex < 0 && VaporView::isRememberedSerialPort(preferredPort))
        {
            preferredIndex = output_port_combo_->count();
            output_port_combo_->addItem(preferredPort, preferredPort);
            output_port_combo_->setItemData(
                preferredIndex,
                true,
                VaporView::kSerialPortHistoryItemRole);
        }
        output_port_combo_->setCurrentIndex(preferredIndex >= 0 ? preferredIndex : 0);
    }
    if (!baudText.trimmed().isEmpty() && baudrate_combo_)
    {
        baudrate_combo_->setCurrentText(baudText.trimmed());
    }
}

void RtkConfigDialog::setEpsilonMainPortAndBaud(const QString& portName, const QString& baudText)
{
    epsilon_main_port_ = portName.trimmed();
    bool ok = false;
    const int baudrate = baudText.trimmed().toInt(&ok);
    epsilon_main_baudrate_ = ok ? baudrate : 921600;
}

void RtkConfigDialog::setEpsilonDataProvider(std::function<VaporView::EpsilonData()> provider)
{
    epsilon_data_provider_ = std::move(provider);
}

void RtkConfigDialog::setEpsilonMainAntennaLeverArmApplier(EpsilonLeverArmApplier applier)
{
    epsilon_main_antenna_lever_arm_applier_ = std::move(applier);
}

bool RtkConfigDialog::buildRtkStreamConfig(RtkStreamConfig *config,
                                           QString *description,
                                           QString *validationError) const
{
    const QString server = server_edit_->text().trimmed();
    const QString port = port_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();
    const QString mountpoint = selectedMountpointText(mountpoint_combo_);
    const QString outputPort = selectedSerialPortText(output_port_combo_);
    bool baudrateOk = false;
    const int baudrate = baudrate_combo_->currentText().toInt(&baudrateOk);
    const int timeout = comboIntValue(timeout_combo_, 5000);
    const int reconnect = comboIntValue(reconnect_combo_, 1000);
    const VaporView::EpsilonData epsilonData = epsilon_data_provider_
        ? epsilon_data_provider_()
        : VaporView::EpsilonData();
    const bool hasEpsilonPosition = isUsableEpsilonNmeaPosition(epsilonData);

    if (mountpoint.isEmpty())
    {
        if (validationError)
        {
            *validationError = textFor("Please detect and select a mountpoint, or type a valid mountpoint manually.",
                                       "请先检测并选择挂载点，或手动输入有效挂载点。");
        }
        return false;
    }

    if (server.isEmpty() || outputPort.isEmpty())
    {
        if (validationError)
        {
            *validationError = textFor("Please fill in server and output port.",
                                       "请填写服务器和输出串口。");
        }
        return false;
    }

    if (serialPortNamesReferToSamePort(outputPort, epsilon_main_port_))
    {
        if (validationError)
        {
            *validationError = textFor(
                "The RTCM forwarding port must differ from the EPSILON main port. The main port reads real GNSS/FDILink data for NTRIP GGA, while a separate PC serial port must write RTCM to EPSILON COMM2.",
                "RTCM 转发串口不能与 EPSILON 主串口相同。主串口用于读取真实 GNSS/FDILink 数据并生成 NTRIP GGA，另一条本机串口必须连接 EPSILON COMM2 写入 RTCM。");
        }
        return false;
    }

    if (config)
    {
        config->server = server;
        config->port = port.isEmpty() ? defaultRtkCasterPort() : port;
        config->username = username;
        config->password = password;
        config->mountpoint = mountpoint;
        config->outputPort = outputPort;
        config->baudrate = baudrateOk ? baudrate : 115200;
        config->timeoutMs = timeout;
        config->reconnectMs = reconnect;
        config->relayBack = hasEpsilonPosition ? 0 : 1;
        config->sendNmeaGga = hasEpsilonPosition;
        config->nmeaGgaCycleMs = kGgaSendCycleMs;
        config->nmeaLatitudeDeg = epsilonData.latitude_deg;
        config->nmeaLongitudeDeg = epsilonData.longitude_deg;
        config->nmeaHeightM = epsilonData.height_m;
    }

    QString ntripUrl;
    if (!username.isEmpty())
    {
        ntripUrl = QString("ntrip://%1:%2@%3:%4/%5")
            .arg(username)
            .arg(password)
            .arg(server)
            .arg(port.isEmpty() ? defaultRtkCasterPort() : port)
            .arg(mountpoint);
    }
    else
    {
        ntripUrl = QString("ntrip://%1:%2/%3")
            .arg(server)
            .arg(port.isEmpty() ? defaultRtkCasterPort() : port)
            .arg(mountpoint);
    }

    if (description)
    {
        const QString serialUrl = QString("serial://%1:%2:8:n:1:off")
            .arg(outputPort)
            .arg(baudrateOk ? baudrate : 115200);
        const QString ggaSource = hasEpsilonPosition
            ? textFor("GGA source: EPSILON main-port position [%1, %2, %3 m] at 1 Hz",
                      "GGA 来源: EPSILON 主串口定位 [%1, %2, %3 m]，1Hz")
                  .arg(QString::number(epsilonData.latitude_deg, 'f', 8),
                       QString::number(epsilonData.longitude_deg, 'f', 8),
                       QString::number(epsilonData.height_m, 'f', 3))
            : textFor("GGA source: output-port relay fallback; connect EPSILON main port first to generate GGA without reading from the RTCM port.",
                      "GGA 来源: 输出口回读兼容模式；请先连接 EPSILON 主串口，才能不依赖 RTCM 串口生成 GGA。");
        *description = textFor("Embedded RTK stream: %1 -> %2\n%3",
                               "内嵌 RTK 流服务: %1 -> %2\n%3")
            .arg(ntripUrl, serialUrl)
            .arg(ggaSource);
    }

    return true;
}

void RtkConfigDialog::updateButtonStates()
{
    const bool busy = isBackgroundTaskRunning();
    start_btn_->setEnabled(!is_running_ && !busy);
    stop_btn_->setEnabled(is_running_ && !busy);
    test_btn_->setEnabled(!is_running_ && !busy);
    apply_main_antenna_lever_btn_->setEnabled(!is_running_ && !busy);
    fetch_mountpoints_btn_->setEnabled(!busy);
    refresh_ports_btn_->setEnabled(!busy);
    auto_detect_ports_btn_->setEnabled(!busy);
    gga_toggle_btn_->setEnabled(!busy);

    if (busy)
    {
        const QString busyText = fetch_mountpoints_in_progress_.load()
            ? textFor("Status: Fetching mountpoints", "状态: 正在获取挂载点")
            : port_detection_in_progress_.load()
                ? textFor("Status: Detecting serial ports", "状态: 正在识别串口")
            : lever_arm_apply_in_progress_
                ? textFor("Status: Applying EPSILON lever arm", "状态: 正在下发 EPSILON 杆臂")
            : textFor("Status: Running no-signal RTK test", "状态: 正在执行无信号 RTK 测试");
        setServiceStatus(busyText, QStringLiteral("timer"), AppThemeColor::Warning);
    }
    else if (is_running_)
    {
        setServiceStatus(textFor("Status: Running", "状态: 运行中"),
                         QStringLiteral("link"),
                         AppThemeColor::Success);
    }
    else
    {
        setServiceStatus(textFor("Status: Stopped", "状态: 已停止"),
                         QStringLiteral("circle-x"),
                         AppThemeColor::TextSecondary);
    }
}

void RtkConfigDialog::pollRtkServiceStatus(bool forceLog)
{
    if (!rtk_service_)
    {
        return;
    }

    const RtkStreamStats stats = rtk_service_->stats();
    const QString message = stats.message.isEmpty()
        ? textFor("Streaming RTCM data", "正在转发 RTCM 数据")
        : stats.message;

    const QString summaryLine = formatRtkStatusLine(
        stats,
        textFor("Streaming RTCM data", "正在转发 RTCM 数据"),
        is_english_);

    if ((forceLog || is_running_) && !message.isEmpty())
    {
        appendRawLogLine(summaryLine);
    }
    last_rtk_status_message_ = message;

    if (!stats.running && is_running_)
    {
        is_running_ = false;
        if (rtk_status_timer_ && rtk_status_timer_->isActive())
        {
            rtk_status_timer_->stop();
        }
        emit rtkRunningChanged(false);
        updateButtonStates();
        appendLog(textFor("RTK service stopped unexpectedly", "RTK 服务已意外停止"));
        return;
    }

    if (is_running_)
    {
        setServiceStatus(
            textFor("Status: Running (%1 bps in / %2 bps out)", "状态: 运行中 (%1 bps 输入 / %2 bps 输出)")
                .arg(stats.inputBps)
                .arg(stats.outputBps),
            QStringLiteral("link"),
            AppThemeColor::Success);
    }
}

void RtkConfigDialog::onRtkStatusTimer()
{
    pollRtkServiceStatus(false);
}

QString RtkConfigDialog::mainGgaSourceLabel() const
{
    return textFor("Epsilon generated", "Epsilon生成");
}

bool RtkConfigDialog::isMainGgaSourceSelected() const
{
    if (!gga_port_combo_)
    {
        return true;
    }

    const QVariant currentData = gga_port_combo_->currentData();
    if (currentData.toString() == QString::fromLatin1(kEpsilonMainGgaSourceKey))
    {
        return true;
    }

    const QString text = gga_port_combo_->currentText().trimmed();
    return text == QString::fromLatin1(kEpsilonMainGgaSourceKey) ||
        text == mainGgaSourceLabel() ||
        (text.contains(QStringLiteral("EPSILON"), Qt::CaseInsensitive) &&
         (text.contains(QStringLiteral("main"), Qt::CaseInsensitive) ||
          text.contains(QStringLiteral("主串口"), Qt::CaseInsensitive)));
}

QString RtkConfigDialog::savedGgaSourceValue() const
{
    return isMainGgaSourceSelected()
        ? QString::fromLatin1(kEpsilonMainGgaSourceKey)
        : ggaPortName();
}

void RtkConfigDialog::applySavedGgaSource(const QString& source)
{
    if (!gga_port_combo_)
    {
        return;
    }

    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty() ||
        trimmed == QString::fromLatin1(kEpsilonMainGgaSourceKey) ||
        trimmed == mainGgaSourceLabel() ||
        (trimmed.contains(QStringLiteral("EPSILON"), Qt::CaseInsensitive) &&
         (trimmed.contains(QStringLiteral("main"), Qt::CaseInsensitive) ||
          trimmed.contains(QStringLiteral("主串口"), Qt::CaseInsensitive))))
    {
        const int mainIndex = gga_port_combo_->findData(QString::fromLatin1(kEpsilonMainGgaSourceKey));
        gga_port_combo_->setCurrentIndex(mainIndex >= 0 ? mainIndex : 0);
        if (QLineEdit *edit = gga_port_combo_->lineEdit())
        {
            edit->setCursorPosition(0);
            edit->setSelection(0, 0);
        }
        return;
    }

    int portIndex = -1;
    for (int index = 0; index < gga_port_combo_->count(); ++index)
    {
        if (VaporView::serialPortNamesMatch(gga_port_combo_->itemText(index), trimmed))
        {
            portIndex = index;
            break;
        }
    }
    if (portIndex < 0 && VaporView::isRememberedSerialPort(trimmed))
    {
        portIndex = gga_port_combo_->count();
        gga_port_combo_->addItem(trimmed, trimmed);
        gga_port_combo_->setItemData(
            portIndex,
            true,
            VaporView::kSerialPortHistoryItemRole);
    }
    if (portIndex < 0)
    {
        portIndex = gga_port_combo_->findData(QString::fromLatin1(kEpsilonMainGgaSourceKey));
    }
    gga_port_combo_->setCurrentIndex(portIndex >= 0 ? portIndex : 0);
    if (QLineEdit *edit = gga_port_combo_->lineEdit())
    {
        edit->setCursorPosition(0);
        edit->setSelection(0, 0);
    }
}

QString RtkConfigDialog::ggaPortName() const
{
    if (!gga_port_combo_)
    {
        return QString();
    }

    return selectedSerialPortText(gga_port_combo_);
}

int RtkConfigDialog::currentGgaBaudrate() const
{
    bool ok = false;
    const int baudrate = baudrate_combo_ ? baudrate_combo_->currentText().toInt(&ok) : 115200;
    return ok ? baudrate : 115200;
}

int RtkConfigDialog::currentOutputBaudrate() const
{
    bool ok = false;
    const int baudrate = baudrate_combo_ ? baudrate_combo_->currentText().toInt(&ok) : 115200;
    return ok ? baudrate : 115200;
}

void RtkConfigDialog::updateGgaFrequency(double hz)
{
    if (!gga_frequency_label_)
    {
        return;
    }

    const QString rateText = QString::number(std::max(0.0, hz), 'f', 2)
        .rightJustified(7, QLatin1Char(' '));
    gga_frequency_label_->setText(textFor("Rate: %1 Hz", "频率: %1 Hz").arg(rateText));
}

void RtkConfigDialog::updateGgaStatusLabel(const QString& message, bool healthy)
{
    if (!gga_status_label_)
    {
        return;
    }

    const bool wasVisible = gga_status_label_->isVisible();
    const bool visible = !message.trimmed().isEmpty();
    gga_status_message_ = message;
    gga_status_healthy_ = healthy;
    gga_status_label_->setText(message);
    gga_status_label_->setVisible(visible);
    gga_status_label_->setMinimumHeight(visible ? scalePixels(24) : 0);
    gga_status_label_->setStyleSheet(boldLabelColorStyle(healthy ? AppThemeColor::RtkHealthy : AppThemeColor::RtkWarning));
    if (wasVisible != visible)
    {
        applyScaledUiMetrics();
    }
}

void RtkConfigDialog::updateGgaMonitorButton()
{
    if (!gga_toggle_btn_)
    {
        return;
    }

    gga_toggle_btn_->setText(gga_monitor_enabled_
        ? textFor("Stop Reading", "停止读取")
        : textFor("Read", "读取"));
    gga_toggle_btn_->setEnabled(true);
}

void RtkConfigDialog::updateGgaMonitorText()
{
    if (!gga_port_info_label_)
    {
        return;
    }

    gga_port_info_label_->setText(textFor("GGA Source:", "GGA来源:"));

    if (gga_status_message_.isEmpty())
    {
        const bool mainSource = isMainGgaSourceSelected();
        updateGgaStatusLabel(
            gga_monitor_enabled_
                ? (mainSource
                    ? textFor("Status: Waiting for EPSILON main-port position", "状态: 正在等待 EPSILON 主串口定位")
                    : textFor("Status: Waiting for serial data", "状态: 正在等待串口数据"))
                : QString(),
            false);
    }
    else if (gga_status_message_.startsWith("Status:") || gga_status_message_.startsWith("状态:"))
    {
        updateGgaStatusLabel(gga_status_message_, gga_status_healthy_);
    }

    if (gga_frequency_label_->text().isEmpty())
    {
        updateGgaFrequency(0.0);
    }
}

void RtkConfigDialog::startGgaMonitor()
{
    if (!gga_poll_timer_)
    {
        return;
    }

    gga_monitor_enabled_ = true;
    gga_buffer_.clear();
    gga_recent_intervals_sec_.clear();
    gga_has_sentence_time_ = false;
    gga_last_epsilon_sample_time_ = std::chrono::steady_clock::time_point();
    gga_last_epsilon_device_timestamp_us_ = 0;
    updateGgaFrequency(0.0);
    gga_status_message_.clear();
    updateGgaMonitorText();
    updateGgaMonitorButton();
    gga_last_open_attempt_ = std::chrono::steady_clock::time_point();
    if (!gga_poll_timer_->isActive())
    {
        gga_poll_timer_->start(kGgaPollIntervalMs);
    }
    onGgaPollTimer();
}

void RtkConfigDialog::stopGgaMonitor()
{
    if (gga_poll_timer_ && gga_poll_timer_->isActive())
    {
        gga_poll_timer_->stop();
    }

    if (gga_serial_.isOpen())
    {
        gga_serial_.close();
    }

    gga_buffer_.clear();
    gga_recent_intervals_sec_.clear();
    gga_has_sentence_time_ = false;
    gga_last_epsilon_sample_time_ = std::chrono::steady_clock::time_point();
    gga_last_epsilon_device_timestamp_us_ = 0;
    gga_monitor_enabled_ = false;
    gga_status_message_.clear();
    updateGgaFrequency(0.0);
    updateGgaMonitorText();
    updateGgaMonitorButton();
}

bool RtkConfigDialog::tryOpenGgaPort()
{
    if (!gga_monitor_enabled_)
    {
        return false;
    }

    if (isMainGgaSourceSelected())
    {
        if (gga_serial_.isOpen())
        {
            gga_serial_.close();
        }
        return true;
    }

    if (gga_serial_.isOpen())
    {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (gga_last_open_attempt_.time_since_epoch().count() > 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - gga_last_open_attempt_).count() < kGgaReconnectIntervalMs)
    {
        return false;
    }

    gga_last_open_attempt_ = now;
    const std::string port = ggaPortName().toStdString();
    if (port.empty())
    {
        updateGgaStatusLabel(textFor("Status: Please select a GGA source", "状态: 请选择 GGA 来源"), false);
        return false;
    }

    if (!gga_serial_.open(port, currentGgaBaudrate()))
    {
        updateGgaStatusLabel(
            textFor("Status: %1 unavailable, retrying...", "状态: %1 不可用，正在重试...").arg(ggaPortName()),
            false);
        return false;
    }

    gga_serial_.setNonBlocking(true);
    gga_buffer_.clear();
    gga_recent_intervals_sec_.clear();
    gga_has_sentence_time_ = false;
    updateGgaFrequency(0.0);
    updateGgaStatusLabel(textFor("Status: Listening on %1", "状态: 正在监听 %1").arg(ggaPortName()), true);
    updateGgaMonitorText();
    return true;
}

void RtkConfigDialog::pollMainGgaSource()
{
    if (!gga_monitor_enabled_)
    {
        return;
    }

    if (!epsilon_data_provider_)
    {
        updateGgaStatusLabel(
            textFor("Status: EPSILON main-port source is not connected", "状态: EPSILON 主串口来源未接入"),
            false);
        return;
    }

    const VaporView::EpsilonData epsilonData = epsilon_data_provider_();
    if (!isUsableEpsilonNmeaPosition(epsilonData))
    {
        updateGgaStatusLabel(
            textFor("Status: Waiting for valid EPSILON main-port position", "状态: 正在等待有效的 EPSILON 主串口定位"),
            false);
        return;
    }

    const bool sameSample =
        epsilonData.timestamp == gga_last_epsilon_sample_time_ &&
        epsilonData.device_timestamp_us == gga_last_epsilon_device_timestamp_us_;
    if (sameSample && gga_has_sentence_time_)
    {
        return;
    }

    const QString sentence = buildEpsilonGgaSentence(epsilonData);
    if (sentence.isEmpty())
    {
        updateGgaStatusLabel(
            textFor("Status: Failed to build GGA from EPSILON position", "状态: EPSILON 定位无法组装 GGA"),
            false);
        return;
    }

    gga_last_epsilon_sample_time_ = epsilonData.timestamp;
    gga_last_epsilon_device_timestamp_us_ = epsilonData.device_timestamp_us;
    handleGgaSentence(sentence);
    updateGgaStatusLabel(
        textFor("Status: Reading generated GGA from EPSILON main port", "状态: 正在读取 EPSILON 主串口生成的 GGA"),
        true);
}

void RtkConfigDialog::onGgaToggleClicked()
{
    if (gga_monitor_enabled_)
    {
        stopGgaMonitor();
        return;
    }

    startGgaMonitor();
}

bool RtkConfigDialog::sendReceiverCommands(const QStringList& commands, QString *errorMessage)
{
    const QString outputPort = selectedSerialPortText(output_port_combo_);
    if (outputPort.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = textFor("Please select an RTK output port first.", "请先选择 RTK 输出串口。");
        }
        return false;
    }

    VaporView::SerialPort serial;
    if (!serial.open(outputPort.toStdString(), currentOutputBaudrate()))
    {
        if (errorMessage)
        {
            *errorMessage = textFor("Failed to open %1: %2", "打开 %1 失败: %2")
                .arg(outputPort, QString::fromStdString(serial.lastError()));
        }
        return false;
    }

    auto appendResponseLines = [this](const QByteArray &buffer) {
        const QList<QByteArray> lines = buffer.split('\n');
        bool logged = false;
        for (const QByteArray &rawLine : lines)
        {
            const QString line = QString::fromLatin1(rawLine).trimmed();
            if (line.isEmpty())
            {
                continue;
            }
            appendLog(QStringLiteral("[RTK 接收] %1").arg(line));
            logged = true;
        }
        if (!logged)
        {
            appendLog(textFor("[RTK 接收] No response (command may have been accepted)",
                              "[RTK 接收] 无返回（命令可能已被接受）"));
        }
    };

    for (const QString &command : commands)
    {
        const QString trimmedCommand = command.trimmed();
        if (trimmedCommand.isEmpty())
        {
            continue;
        }

        const QByteArray payload = (trimmedCommand + QStringLiteral("\r\n")).toLatin1();
        appendLog(QStringLiteral("[RTK 发送] %1").arg(trimmedCommand));
        const ssize_t written = serial.write(payload.constData(), static_cast<size_t>(payload.size()));
        if (written != payload.size())
        {
            if (errorMessage)
            {
                *errorMessage = textFor("Failed to send command: %1", "命令发送失败: %1").arg(trimmedCommand);
            }
            serial.close();
            return false;
        }

        serial.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        QByteArray responseBuffer;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline)
        {
            char buffer[512];
            const ssize_t readBytes = serial.read(buffer, sizeof(buffer));
            if (readBytes > 0)
            {
                responseBuffer.append(buffer, static_cast<int>(readBytes));
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
                continue;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        appendResponseLines(responseBuffer);
    }

    serial.close();
    return true;
}

QString RtkConfigDialog::mainAntennaLeverArmHelpText() const
{
    return textFor(
        QStringLiteral("Main antenna lever arm is the GNSS main antenna phase-center position in the EPSILON/IMU frame, used to compensate the offset between the antenna and IMU during GNSS/INS fusion.\n\n"
                       "Measure from the EPSILON module center to the GNSS main antenna phase center. Enter X/Y/Z in meters in the module frame: X forward, Y right, Z down. If the antenna is above the module, Z is negative.\n\n"
                       "Command sent to EPSILON main port: #fconfig -> #fantearm x y z -> #fsave -> #fdeconfig."),
        QStringLiteral("主天线杆臂是 GNSS 主天线相位中心在 EPSILON/IMU 模组坐标系下的位置，用来补偿天线与惯导不重合带来的 GNSS/INS 杆臂误差。\n\n"
                       "测量时从 EPSILON 模组中心量到 GNSS 主天线相位中心，分别填写 X/Y/Z，单位米；坐标系为模组坐标系：X 向前、Y 向右、Z 向下。天线在模组上方时，Z 为负值。\n\n"
                       "下发到 EPSILON 主串口的命令：#fconfig -> #fantearm x y z -> #fsave -> #fdeconfig。"));
}

void RtkConfigDialog::onMainAntennaLeverHelpClicked()
{
    if (main_antenna_lever_help_popup_ && main_antenna_lever_help_popup_->isVisible())
    {
        main_antenna_lever_help_popup_->hide();
        return;
    }

    if (!main_antenna_lever_help_popup_)
    {
        auto *popup = new VaporView::SingleLevelPopupMenu(this);
        popup->setObjectName(QStringLiteral("rtkLeverHelpPopup"));

        auto *content = new QWidget(popup);
        auto *contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(scalePixels(8), scalePixels(8), scalePixels(8), scalePixels(8));
        contentLayout->setSpacing(0);
        main_antenna_lever_help_popup_label_ = new QLabel(content);
        main_antenna_lever_help_popup_label_->setObjectName(QStringLiteral("rtkLeverHelpPopupText"));
        main_antenna_lever_help_popup_label_->setWordWrap(true);
        main_antenna_lever_help_popup_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        contentLayout->addWidget(main_antenna_lever_help_popup_label_);
        auto *contentAction = new QWidgetAction(popup);
        contentAction->setDefaultWidget(content);
        popup->addAction(contentAction);
        main_antenna_lever_help_popup_ = popup;
    }

    main_antenna_lever_help_popup_->setCornerRadius(scalePixels(10));
    main_antenna_lever_help_popup_->setPanelPadding(scalePixels(12));
    main_antenna_lever_help_popup_->setShadowMargin(scalePixels(40));
    main_antenna_lever_help_popup_->setBottomShadowMargin(scalePixels(50));
    main_antenna_lever_help_popup_->setPanelContentWidth(scalePixels(456));
    main_antenna_lever_help_popup_label_->setStyleSheet(QStringLiteral(
        "QLabel#rtkLeverHelpPopupText {"
        " color: %1;"
        " background: transparent;"
        " border: none;"
        " padding: 0px;"
        "}")
        .arg(appThemeColorName(AppThemeColor::Text, isDarkThemeEnabled())));

    main_antenna_lever_help_popup_label_->setText(mainAntennaLeverArmHelpText());
    main_antenna_lever_help_popup_label_->setMinimumWidth(scalePixels(320));
    main_antenna_lever_help_popup_label_->setMaximumWidth(scalePixels(440));

    if (main_antenna_lever_help_btn_)
    {
        main_antenna_lever_help_popup_->popupFrom(
            main_antenna_lever_help_btn_, VaporView::SingleLevelPopupAnchor::Left, QPoint(0, scalePixels(4)));
    }
    else
    {
        main_antenna_lever_help_popup_->popup(mapToGlobal(rect().center()));
    }
}

bool RtkConfigDialog::parseMainAntennaLeverArm(double *x, double *y, double *z, QString *errorMessage) const
{
    auto parseValue = [this, errorMessage](const QLineEdit *edit, const QString& axis, double *value) {
        const QString original = edit ? edit->text().trimmed() : QString();
        if (original.isEmpty())
        {
            if (errorMessage)
            {
                *errorMessage = textFor("Enter %1 lever-arm value in meters. Use 0 if the offset is unknown.",
                                        "请输入 %1 方向杆臂值，单位米；未知可填 0。").arg(axis);
            }
            return false;
        }

        QString normalized = original;
        normalized.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        double parsed = QLocale::c().toDouble(normalized, &ok);
        if (!ok)
        {
            parsed = normalized.toDouble(&ok);
        }

        if (!ok || !std::isfinite(parsed))
        {
            if (errorMessage)
            {
                *errorMessage = textFor("Invalid %1 lever-arm value: %2",
                                        "%1 方向杆臂值无效: %2").arg(axis, original);
            }
            return false;
        }

        *value = parsed;
        return true;
    };

    return parseValue(main_antenna_lever_x_edit_, QStringLiteral("X"), x) &&
        parseValue(main_antenna_lever_y_edit_, QStringLiteral("Y"), y) &&
        parseValue(main_antenna_lever_z_edit_, QStringLiteral("Z"), z);
}

void RtkConfigDialog::onApplyMainAntennaLeverArmClicked()
{
    if (is_running_)
    {
        QMessageBox::warning(
            this,
            textFor("RTK Running", "RTK 运行中"),
            textFor("Stop the RTK service before changing the EPSILON main antenna lever arm.",
                    "请先停止 RTK 服务，再修改 EPSILON 主天线杆臂。"));
        return;
    }

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    QString errorMessage;
    if (!parseMainAntennaLeverArm(&x, &y, &z, &errorMessage))
    {
        QMessageBox::warning(
            this,
            textFor("Invalid Lever Arm", "杆臂无效"),
            errorMessage);
        return;
    }

    if (!epsilon_main_antenna_lever_arm_applier_)
    {
        QMessageBox::warning(
            this,
            textFor("EPSILON Unavailable", "EPSILON 不可用"),
            textFor("EPSILON main-port command channel is not available. Open this dialog from the main window after selecting the EPSILON main port.",
                    "EPSILON 主串口命令通道不可用。请在主页面选择 EPSILON 主串口后再打开此配置。"));
        return;
    }

    const QString values = QStringLiteral("X=%1 m, Y=%2 m, Z=%3 m")
        .arg(QString::number(x, 'f', 4),
             QString::number(y, 'f', 4),
             QString::number(z, 'f', 4));
    const QString target = epsilon_main_port_.isEmpty()
        ? textFor("selected EPSILON main port", "已选择的 EPSILON 主串口")
        : QStringLiteral("%1 @ %2").arg(epsilon_main_port_).arg(epsilon_main_baudrate_);
    appendLog(textFor("Applying EPSILON main antenna lever arm via %1: %2",
                      "正在通过 %1 下发 EPSILON 主天线杆臂: %2").arg(target, values));

    lever_arm_apply_in_progress_ = true;
    updateButtonStates();

    QPointer<RtkConfigDialog> self(this);
    epsilon_main_antenna_lever_arm_applier_(
        x,
        y,
        z,
        [self, x, y, z, values](bool succeeded, const QString& resultError) {
            if (!self)
            {
                return;
            }
            QMetaObject::invokeMethod(self.data(), [self, x, y, z, values, succeeded, resultError]() {
                if (!self)
                {
                    return;
                }

                self->lever_arm_apply_in_progress_ = false;
                self->updateButtonStates();
                if (!succeeded)
                {
                    QMessageBox::warning(
                        self.data(),
                        self->textFor("Command Failed", "命令发送失败"),
                        resultError.isEmpty()
                            ? self->textFor("Failed to apply EPSILON main antenna lever arm.",
                                            "EPSILON 主天线杆臂下发失败。")
                            : resultError);
                    return;
                }

                self->saveSettings();
                self->appendLog(self->textFor("EPSILON main antenna lever arm updated: %1",
                                              "EPSILON 主天线杆臂已更新: %1").arg(values));
                QMessageBox::information(
                    self.data(),
                    self->textFor("Lever Arm Updated", "杆臂已更新"),
                    self->textFor("EPSILON has been sent: #fantearm %1 %2 %3",
                                  "已向 EPSILON 下发: #fantearm %1 %2 %3")
                        .arg(QString::number(x, 'f', 4),
                             QString::number(y, 'f', 4),
                             QString::number(z, 'f', 4)));
            }, Qt::QueuedConnection);
        });
}

void RtkConfigDialog::processGgaBuffer()
{
    while (true)
    {
        int newlineIndex = gga_buffer_.indexOf('\n');
        if (newlineIndex < 0)
        {
            break;
        }

        QString line = gga_buffer_.left(newlineIndex);
        gga_buffer_.remove(0, newlineIndex + 1);
        line = line.trimmed();
        if (line.isEmpty())
        {
            continue;
        }

        if (kGgaSentencePattern.match(line).hasMatch())
        {
            handleGgaSentence(line);
        }
    }
}

void RtkConfigDialog::handleGgaSentence(const QString& sentence)
{
    const auto now = std::chrono::steady_clock::now();
    if (gga_has_sentence_time_)
    {
        const double intervalSeconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - gga_last_sentence_time_).count();
        if (intervalSeconds > 0.0)
        {
            gga_recent_intervals_sec_.push_back(intervalSeconds);
            while (gga_recent_intervals_sec_.size() > 20)
            {
                gga_recent_intervals_sec_.pop_front();
            }

            double total = 0.0;
            for (double value : gga_recent_intervals_sec_)
            {
                total += value;
            }

            if (!gga_recent_intervals_sec_.empty() && total > 0.0)
            {
                updateGgaFrequency(static_cast<double>(gga_recent_intervals_sec_.size()) / total);
            }
        }
    }
    else
    {
        updateGgaFrequency(0.0);
    }

    gga_has_sentence_time_ = true;
    gga_last_sentence_time_ = now;
    updateGgaStatusLabel(textFor("Status: Receiving GGA data", "状态: 正在接收 GGA 数据"), true);

    if (gga_text_edit_)
    {
        QScrollBar *scrollBar = gga_text_edit_->verticalScrollBar();
        const bool stickToBottom = !scrollBar || scrollBar->value() >= (scrollBar->maximum() - 2);
        const int previousValue = scrollBar ? scrollBar->value() : 0;
        const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        gga_text_edit_->append(QString("[%1] %2").arg(timestamp, sentence));
        trimGgaDisplay();
        QTimer::singleShot(0, this, [this, stickToBottom, previousValue]() {
            if (!gga_text_edit_)
            {
                return;
            }

            QScrollBar *updatedScrollBar = gga_text_edit_->verticalScrollBar();
            if (!updatedScrollBar)
            {
                return;
            }

            if (stickToBottom)
            {
                updatedScrollBar->setValue(updatedScrollBar->maximum());
            }
            else
            {
                updatedScrollBar->setValue(std::min(previousValue, updatedScrollBar->maximum()));
            }
        });
    }
}

void RtkConfigDialog::trimGgaDisplay()
{
    if (!gga_text_edit_)
    {
        return;
    }

    QTextDocument *document = gga_text_edit_->document();
    if (!document)
    {
        return;
    }

    while (document->blockCount() > kGgaMaxVisibleLines)
    {
        QTextBlock firstBlock = document->begin();
        if (!firstBlock.isValid())
        {
            break;
        }

        QTextCursor cursor(firstBlock);
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar();
    }
}

void RtkConfigDialog::onGgaPollTimer()
{
    if (isMainGgaSourceSelected())
    {
        if (gga_serial_.isOpen())
        {
            gga_serial_.close();
        }
        pollMainGgaSource();
        if (gga_has_sentence_time_)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto staleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - gga_last_sentence_time_).count();
            if (staleMs > kGgaStaleTimeoutMs)
            {
                gga_recent_intervals_sec_.clear();
                updateGgaFrequency(0.0);
                updateGgaStatusLabel(textFor("Status: Waiting for next EPSILON main-port position", "状态: 正在等待下一帧 EPSILON 主串口定位"), false);
            }
        }
        return;
    }

    if (!tryOpenGgaPort())
    {
        return;
    }

    char buffer[512];
    while (true)
    {
        const ssize_t bytesRead = gga_serial_.read(buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            gga_buffer_.append(QString::fromLatin1(buffer, static_cast<int>(bytesRead)));
            processGgaBuffer();
            continue;
        }

        if (bytesRead < 0)
        {
            gga_serial_.close();
            updateGgaFrequency(0.0);
            updateGgaStatusLabel(textFor("Status: %1 read failed, reconnecting...", "状态: %1 读取失败，正在重连...").arg(ggaPortName()), false);
        }
        break;
    }

    if (gga_has_sentence_time_)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto staleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - gga_last_sentence_time_).count();
        if (staleMs > kGgaStaleTimeoutMs)
        {
            gga_recent_intervals_sec_.clear();
            updateGgaFrequency(0.0);
            updateGgaStatusLabel(textFor("Status: Waiting for next GGA sentence", "状态: 正在等待下一条 GGA 语句"), false);
        }
    }
}

void RtkConfigDialog::refreshPortCombos()
{
    const QStringList ports = getAvailablePorts();
    const QString currentOutput = selectedSerialPortText(output_port_combo_);
    const QString currentGga = isMainGgaSourceSelected() ? QString() : ggaPortName();

    if (output_port_combo_)
    {
        const QSignalBlocker blocker(output_port_combo_);
        output_port_combo_->clear();
        output_port_combo_->addItem(textFor("-- Select --", "未选择"));
        for (const QString& port : ports)
        {
            output_port_combo_->addItem(port, port);
        }

        int selectedIndex = -1;
        for (int index = 1; index < output_port_combo_->count(); ++index)
        {
            if (VaporView::serialPortNamesMatch(output_port_combo_->itemText(index), currentOutput))
            {
                selectedIndex = index;
                break;
            }
        }
        if (selectedIndex < 0 && VaporView::isRememberedSerialPort(currentOutput))
        {
            selectedIndex = output_port_combo_->count();
            output_port_combo_->addItem(currentOutput, currentOutput);
            output_port_combo_->setItemData(
                selectedIndex,
                true,
                VaporView::kSerialPortHistoryItemRole);
        }
        output_port_combo_->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    }

    if (gga_port_combo_)
    {
        const QSignalBlocker blocker(gga_port_combo_);
        gga_port_combo_->clear();
        gga_port_combo_->addItem(mainGgaSourceLabel(), QString::fromLatin1(kEpsilonMainGgaSourceKey));
        for (const QString& port : ports)
        {
            gga_port_combo_->addItem(port, port);
        }
        applySavedGgaSource(currentGga);
    }
}

QStringList RtkConfigDialog::getAvailablePorts() const
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

void RtkConfigDialog::onRefreshPortsClicked()
{
    refreshPortCombos();
    appendLog(textFor("Ports refreshed: %1 found", "串口已刷新: 发现 %1 个").arg(getAvailablePorts().size()));
}

void RtkConfigDialog::onAutoDetectPortsClicked()
{
    if (is_running_ || isBackgroundTaskRunning())
    {
        return;
    }

    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }

    refreshPortCombos();
    const QStringList portNames = getAvailablePorts();
    if (portNames.isEmpty())
    {
        appendLog(textFor("Auto detect stopped: no serial ports found.", "自动识别结束：当前没有发现可用串口。"));
        return;
    }

    const QStringList baudTexts = buildProbeBaudList(baudrate_combo_);
    port_detection_in_progress_.store(true);
    updateButtonStates();
    appendLog(textFor("Starting RTK output port auto detect...", "开始自动识别 RTK 输出串口..."));

    QPointer<RtkConfigDialog> self(this);
    const bool english = is_english_;
    port_detection_thread_ = std::thread([self, portNames, baudTexts, english]() {
        if (!self)
        {
            return;
        }

        auto queueLog = [self](const QString& message) {
            if (!self)
            {
                return;
            }
            QMetaObject::invokeMethod(self.data(), [self, message]() {
                if (self)
                {
                    self->appendLog(message);
                }
            }, Qt::QueuedConnection);
        };

        QString detectedPort;
        QString detectedBaud;
        for (const QString& portName : portNames)
        {
            if (!self || self->shutdown_requested_.load())
            {
                break;
            }

            queueLog(textForLanguage(english, "[Auto Detect] Probing GGA on %1...", "[自动识别] 正在探测 GGA: %1 ...").arg(portName));
            const auto probeResult = VaporView::probeSerialPortForHeader(
                portName,
                baudTexts,
                VaporView::SerialHeaderProbeKind::Gga);
            if (!probeResult.matched)
            {
                continue;
            }

            detectedPort = portName;
            detectedBaud = probeResult.baudText;
            queueLog(textForLanguage(english,
                                     "[Auto Detect] Identified GGA output on %1 @ %2",
                                     "[自动识别] 已识别 GGA 输出串口: %1 @ %2")
                         .arg(detectedPort, detectedBaud));
            break;
        }

        if (!self)
        {
            return;
        }

        QMetaObject::invokeMethod(self.data(), [self, detectedPort, detectedBaud]() {
            if (!self)
            {
                return;
            }

            self->port_detection_in_progress_.store(false);
            if (!detectedPort.isEmpty())
            {
                self->applyDetectedOutputAndGgaPort(detectedPort, detectedBaud);
            }
            else
            {
                self->appendLog(self->textFor("Auto detect finished: no GGA output port found.",
                                              "自动识别完成：未找到 GGA 输出串口。"));
            }
            self->updateButtonStates();
        }, Qt::QueuedConnection);
    });
}

void RtkConfigDialog::applyDetectedOutputAndGgaPort(const QString& portName, const QString& baudText)
{
    if (portName.isEmpty())
    {
        return;
    }

    VaporView::rememberSerialPort(portName);
    setPreferredOutputPortAndBaud(portName, baudText);
    if (gga_port_combo_)
    {
        applySavedGgaSource(QString::fromLatin1(kEpsilonMainGgaSourceKey));
    }

    const QString appliedBaud = !baudText.isEmpty() && baudrate_combo_
        ? baudText
        : (baudrate_combo_ ? baudrate_combo_->currentText() : QString());
    appendLog(textFor("Auto detect applied: output port set to %1 @ %2; GGA source remains EPSILON main port.",
                      "自动识别已应用：输出串口已设置为 %1 @ %2；GGA 来源保持 EPSILON 主串口。")
                  .arg(portName, appliedBaud));
}

void RtkConfigDialog::onFetchMountpointsClicked()
{
    if (isBackgroundTaskRunning())
    {
        return;
    }

    const QString server = server_edit_->text().trimmed();
    const QString port = port_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();

    if (server.isEmpty() || port.isEmpty())
    {
        QMessageBox::warning(this, textFor("Error", "错误"), textFor("Please enter server address and port first.", "请先填写服务器地址和端口。"));
        return;
    }

    appendLog(textFor("Fetching mountpoint list from %1:%2...", "正在从 %1:%2 获取挂载点列表...").arg(server, port));
    if (fetch_mountpoints_thread_.joinable())
    {
        fetch_mountpoints_thread_.join();
    }

    fetch_mountpoints_in_progress_.store(true);
    updateButtonStates();

    QPointer<RtkConfigDialog> self(this);
    fetch_mountpoints_thread_ = std::thread([self, server, port, username, password]() {
        MountpointFetchResult result;
        result.response = performRtkHttpGet(
            nullptr,
            buildRtkUrl(server, port),
            username,
            password,
            QStringLiteral("text/plain, */*"));

        if (!result.response.timedOut &&
            (result.response.error.isEmpty() || !result.response.body.trimmed().isEmpty()))
        {
            result.mountpoints = parseMountpoints(result.response.body);
        }

        if (!self)
        {
            return;
        }

        QObject *receiver = self.data();
        QMetaObject::invokeMethod(receiver, [self, result = std::move(result)]() mutable {
            if (!self)
            {
                return;
            }

            self->fetch_mountpoints_in_progress_.store(false);
            self->updateButtonStates();

            const HttpResponse &response = result.response;
            if (response.timedOut || (!response.error.isEmpty() && response.body.trimmed().isEmpty()))
            {
                const QString errorText = response.timedOut
                    ? self->textFor("Request timed out", "请求超时")
                    : response.error;
                self->appendLog(self->textFor("Failed to fetch mountpoint list: %1", "获取挂载点列表失败: %1").arg(errorText));
                self->setServiceStatus(
                    self->textFor("Status: Failed to fetch mountpoints", "状态: 获取挂载点失败"),
                    QStringLiteral("triangle-alert"),
                    AppThemeColor::Warning);
                return;
            }

            if (result.mountpoints.isEmpty())
            {
                self->appendLog(self->textFor("No mountpoints found in sourcetable response.", "返回的源表中未找到挂载点。"));
                self->setServiceStatus(
                    self->textFor("Status: No mountpoints found", "状态: 未找到挂载点"),
                    QStringLiteral("triangle-alert"),
                    AppThemeColor::Warning);
                return;
            }

            const QString currentMountpoint = selectedMountpointText(self->mountpoint_combo_);
            const int currentIndex = result.mountpoints.indexOf(currentMountpoint);
            const QSignalBlocker blocker(self->mountpoint_combo_);
            self->mountpoint_combo_->clear();
            self->mountpoint_combo_->addItem(mountpointSelectLabel(self->is_english_),
                                             QString::fromLatin1(kMountpointSelectKey));
            self->mountpoint_combo_->addItems(result.mountpoints);
            if (currentIndex >= 0)
            {
                self->mountpoint_combo_->setCurrentText(currentMountpoint);
            }
            else
            {
                self->mountpoint_combo_->setCurrentIndex(0);
                if (QLineEdit *edit = self->mountpoint_combo_->lineEdit())
                {
                    edit->setText(mountpointSelectLabel(self->is_english_));
                    edit->setCursorPosition(0);
                }
            }
            self->updateMountpointComboWidth();
            self->appendLog(self->textFor("Fetched %1 mountpoints.", "已获取 %1 个挂载点。").arg(result.mountpoints.size()));
            self->appendLog(currentIndex >= 0
                                ? self->textFor("Mountpoint dropdown updated; current: %1",
                                                "挂载点下拉框已更新，当前: %1").arg(currentMountpoint)
                                : self->textFor("Mountpoint dropdown updated; please select one.",
                                                "挂载点下拉框已更新，请选择一个挂载点。"));
            self->setServiceStatus(self->textFor("Status: Mountpoints loaded", "状态: 挂载点已载入"),
                                   QStringLiteral("check"),
                                   AppThemeColor::Success);
        }, Qt::QueuedConnection);
    });
}

void RtkConfigDialog::onStartClicked()
{
    RtkStreamConfig config;
    QString description;
    QString validationError;
    if (!buildRtkStreamConfig(&config, &description, &validationError))
    {
        QMessageBox::warning(this, textFor("Error", "错误"), validationError);
        return;
    }

    appendLog(textFor("Starting RTK service...", "正在启动 RTK 服务..."));
    appendLog(description);
    if (config.sendNmeaGga)
    {
        appendLog(textFor("NTRIP GGA will be generated from the EPSILON main-port position; RTCM is written only to the configured output port.",
                          "将使用 EPSILON 主串口定位生成 NTRIP GGA；RTCM 只写入配置的输出串口。"));
    }
    else
    {
        appendLog(textFor("No valid EPSILON main-port position is available, so RTK service keeps output-port GGA relay fallback.",
                          "当前没有可用的 EPSILON 主串口定位，RTK 服务保留输出口回读 GGA 的兼容模式。"));
    }

    QString errorMessage;
    if (rtk_service_ && rtk_service_->start(config, &errorMessage))
    {
        is_running_ = true;
        emit rtkRunningChanged(true);
        last_rtk_status_message_.clear();
        updateButtonStates();
        if (rtk_status_timer_ && !rtk_status_timer_->isActive())
        {
            rtk_status_timer_->start();
        }
        pollRtkServiceStatus(true);
        appendLog(textFor("RTK service started successfully", "RTK 服务启动成功"));
    }
    else
    {
        appendLog(textFor("Failed to start RTK service: %1", "RTK 服务启动失败: %1")
            .arg(errorMessage.isEmpty() ? textFor("Unknown error", "未知错误") : errorMessage));
    }
}

void RtkConfigDialog::onStopClicked()
{
    if (rtk_service_ && rtk_service_->isRunning())
    {
        appendLog(textFor("Stopping RTK service...", "正在停止 RTK 服务..."));
        rtk_service_->stop();
        if (rtk_status_timer_ && rtk_status_timer_->isActive())
        {
            rtk_status_timer_->stop();
        }
        is_running_ = false;
        emit rtkRunningChanged(false);
        last_rtk_status_message_.clear();
        updateButtonStates();
        appendLog(textFor("RTK service stopped", "RTK 服务已停止"));
    }
}

void RtkConfigDialog::onTestClicked()
{
    if (isBackgroundTaskRunning())
    {
        return;
    }

    if (is_running_)
    {
        QMessageBox::information(this, textFor("Busy", "请先停止"),
            textFor("Stop the running RTK service before starting a no-signal test.", "请先停止当前 RTK 服务，再启动无信号测试。"));
        return;
    }

    RtkStreamConfig config;
    QString description;
    QString validationError;
    if (!buildRtkStreamConfig(&config, &description, &validationError))
    {
        QMessageBox::warning(this, textFor("Error", "错误"), validationError);
        return;
    }

    appendLog(textFor("Starting no-signal RTK test...", "正在启动无信号 RTK 测试..."));
    appendLog(description);

    if (test_thread_.joinable())
    {
        test_thread_.join();
    }

    test_in_progress_.store(true);
    updateButtonStates();

    QPointer<RtkConfigDialog> self(this);
    const bool english = is_english_;
    test_thread_ = std::thread([self, config, english]() mutable {
        auto queueLog = [self](const QString &message) {
            if (!self)
            {
                return;
            }
            QObject *receiver = self.data();
            QMetaObject::invokeMethod(receiver, [self, message]() {
                if (!self)
                {
                    return;
                }
                self->appendLog(message);
            }, Qt::QueuedConnection);
        };

        auto queueRawLog = [self](const QString &message) {
            if (!self)
            {
                return;
            }
            QObject *receiver = self.data();
            QMetaObject::invokeMethod(receiver, [self, message]() {
                if (!self)
                {
                    return;
                }
                self->appendRawLogLine(message);
            }, Qt::QueuedConnection);
        };

        NoSignalTestResult result;
        QTcpServer mockSerialServer;
        if (!mockSerialServer.listen(QHostAddress::LocalHost))
        {
            result.startError = mockSerialServer.errorString();
        }
        else
        {
            const bool useGeneratedGga = config.sendNmeaGga;
            config.outputMode = RtkStreamConfig::OutputMode::TcpClient;
            config.outputPathOverride = QStringLiteral("127.0.0.1:%1").arg(mockSerialServer.serverPort());
            config.relayBack = useGeneratedGga ? 0 : 1;
            queueLog(textForLanguage(english,
                                     "Using loopback mock serial on 127.0.0.1:%1",
                                     "正在使用 127.0.0.1:%1 的 loopback 模拟串口")
                .arg(mockSerialServer.serverPort()));
            if (useGeneratedGga)
            {
                queueLog(textForLanguage(english,
                                         "The test will send EPSILON-position GGA directly to NTRIP; loopback only receives RTCM.",
                                         "本次测试将把 EPSILON 定位 GGA 直接发给 NTRIP；loopback 只接收 RTCM。"));
            }

            std::unique_ptr<RtkStreamService> testService = std::make_unique<RtkStreamService>();
            QString errorMessage;
            if (!testService->start(config, &errorMessage))
            {
                result.startError = errorMessage.isEmpty()
                    ? textForLanguage(english, "Unknown error", "未知错误")
                    : errorMessage;
            }
            else
            {
                QElapsedTimer timer;
                timer.start();
                RtkStreamStats finalStats;
                qint64 lastInjectMs = -1000;
                qint64 lastStatusLogMs = -1000;
                int rtcmResponseBursts = 0;
                bool loggedMockGgaTemplate = false;
                std::unique_ptr<QTcpSocket> mockSerialPeer;
                result.generatedGga = useGeneratedGga;

                while (!self->shutdown_requested_.load() && timer.elapsed() < 15000)
                {
                    finalStats = testService->stats();

                    if (!mockSerialPeer &&
                        (mockSerialServer.hasPendingConnections() || mockSerialServer.waitForNewConnection(100)))
                    {
                        mockSerialPeer.reset(mockSerialServer.nextPendingConnection());
                        if (mockSerialPeer)
                        {
                            queueLog(textForLanguage(english, "Mock serial loopback connected.", "模拟串口 loopback 已连接。"));
                        }
                    }

                    if (timer.elapsed() - lastStatusLogMs >= 1000)
                    {
                        queueRawLog(formatRtkStatusLine(
                            finalStats,
                            textForLanguage(english, "Running no-signal RTK test", "正在执行无信号 RTK 测试"),
                            english));
                        lastStatusLogMs = timer.elapsed();
                    }

                    const QString messageLower = finalStats.message.toLower();
                    const bool stillConnecting =
                        messageLower.contains(QStringLiteral("connecting")) ||
                        messageLower.contains(QStringLiteral("disconnected"));
                    if (!result.linkReady && mockSerialPeer &&
                        mockSerialPeer->state() == QAbstractSocket::ConnectedState && !stillConnecting)
                    {
                        result.linkReady = true;
                        if (!useGeneratedGga)
                        {
                            const QString mockGga = buildMockGgaSentence();
                            queueLog(textForLanguage(english,
                                                     "Injecting GGA at 1 Hz: %1",
                                                     "已按 1Hz 频率注入 GGA 数据: %1")
                                .arg(mockGga));
                            loggedMockGgaTemplate = true;
                        }
                    }

                    if (mockSerialPeer)
                    {
                        if (mockSerialPeer->waitForReadyRead(20) || mockSerialPeer->bytesAvailable() > 0)
                        {
                            QByteArray rtcmData = mockSerialPeer->readAll();
                            while (mockSerialPeer->bytesAvailable() > 0)
                            {
                                rtcmData += mockSerialPeer->readAll();
                            }
                            if (!rtcmData.isEmpty())
                            {
                                result.receivedRtcmBytes += rtcmData.size();
                                ++rtcmResponseBursts;
                            }
                        }
                    }

                    if (!useGeneratedGga && result.linkReady && mockSerialPeer && timer.elapsed() - lastInjectMs >= 1000)
                    {
                        const QString mockGga = buildMockGgaSentence();
                        if (!loggedMockGgaTemplate)
                        {
                            queueLog(textForLanguage(english,
                                                     "Injecting GGA at 1 Hz: %1",
                                                     "已按 1Hz 频率注入 GGA 数据: %1")
                                .arg(mockGga));
                            loggedMockGgaTemplate = true;
                        }
                        QByteArray payload = mockGga.toLatin1();
                        payload += "\r\n";
                        const qint64 written = mockSerialPeer->write(payload);
                        if (written != payload.size() || !mockSerialPeer->waitForBytesWritten(500))
                        {
                            result.runtimeError = mockSerialPeer->errorString().isEmpty()
                                ? textForLanguage(english, "Unknown error", "未知错误")
                                : mockSerialPeer->errorString();
                            break;
                        }
                        lastInjectMs = timer.elapsed();
                    }

                    if (rtcmResponseBursts >= 8)
                    {
                        result.gotResponse = true;
                        break;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                finalStats = testService->stats();
                result.inputBytes = finalStats.inputBytes;
                result.outputBytes = finalStats.outputBytes;
                result.finalMessage = finalStats.message;
                result.cancelled = self->shutdown_requested_.load();
                testService->stop();
                if (mockSerialPeer)
                {
                    mockSerialPeer->disconnectFromHost();
                }
            }
        }

        if (!self)
        {
            return;
        }

        QObject *receiver = self.data();
        QMetaObject::invokeMethod(receiver, [self, result = std::move(result)]() mutable {
            if (!self)
            {
                return;
            }

            self->test_in_progress_.store(false);
            self->updateButtonStates();

            if (result.cancelled)
            {
                return;
            }

            if (!result.startError.isEmpty())
            {
                self->appendLog(self->textFor("No-signal RTK test failed to start: %1", "无信号 RTK 测试启动失败: %1").arg(result.startError));
                QMessageBox::warning(
                    self,
                    self->textFor("Failed", "失败"),
                    self->textFor("Failed to start no-signal RTK test: %1", "无信号 RTK 测试启动失败: %1").arg(result.startError));
                return;
            }

            if (result.gotResponse)
            {
                self->appendLog(self->textFor("No-signal RTK test succeeded: input %1 B, output %2 B, loopback %3 B",
                                              "无信号 RTK 测试成功: 输入 %1 B, 输出 %2 B, loopback %3 B")
                    .arg(result.inputBytes)
                    .arg(result.outputBytes)
                    .arg(result.receivedRtcmBytes));
                QMessageBox::information(
                    self,
                    self->textFor("Success", "成功"),
                    self->textFor("Mock GGA test succeeded. RTCM data was received multiple times.", "模拟 GGA 测试成功，已多次收到 RTCM 返回数据。"));
                return;
            }

            const QString detail = describeNoSignalTestFailure(result, self->is_english_);
            self->appendLog(self->textFor("No-signal RTK test finished without RTCM response: %1", "无信号 RTK 测试结束，未收到 RTCM 返回: %1").arg(detail));
            QMessageBox::warning(
                self,
                self->textFor("No Response", "无返回"),
                self->textFor("Mock GGA test did not receive RTCM data.\n%1", "模拟 GGA 测试未收到 RTCM 返回数据。\n%1").arg(detail));
        }, Qt::QueuedConnection);
    });
}

void RtkConfigDialog::onClearLogClicked()
{
    log_text_edit_->clear();
}

void RtkConfigDialog::appendLog(const QString& message)
{
    if (!log_text_edit_) return;

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    const QString formattedMessage = formatLogMessageBlock(message);
    if (formattedMessage.isEmpty())
    {
        return;
    }
    log_text_edit_->append(QString("[%1]\n%2").arg(timestamp, formattedMessage));

    QTextCursor cursor = log_text_edit_->textCursor();
    cursor.movePosition(QTextCursor::End);
    log_text_edit_->setTextCursor(cursor);
}

void RtkConfigDialog::appendRawLogLine(const QString& line)
{
    if (!log_text_edit_ || line.isEmpty()) return;

    log_text_edit_->append(line.trimmed());

    QTextCursor cursor = log_text_edit_->textCursor();
    cursor.movePosition(QTextCursor::End);
    log_text_edit_->setTextCursor(cursor);
}

bool RtkConfigDialog::isRunning() const
{
    return is_running_;
}

