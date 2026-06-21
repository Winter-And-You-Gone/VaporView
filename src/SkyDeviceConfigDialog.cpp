#include "SkyDeviceConfigDialog.h"
#include "AppTheme.h"
#include "CustomTitleBar.h"
#include "WindowSizing.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QEasingCurve>
#include <QEvent>
#include <QFontMetrics>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QSvgRenderer>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QApplication>

#include <algorithm>
#include <functional>

namespace VaporView
{
namespace
{
constexpr int kFieldDigitCount = 9;
constexpr int kFieldHeight = 36;
constexpr int kCardFormHorizontalMargin = 10;
constexpr int kCardFormSpacing = 6;
constexpr int kEnableToggleSize = 30;
constexpr int kEnableToggleIconSize = 22;
constexpr int kModeSwitchAnimationMs = 220;
constexpr int kModeSwitchDeferredWorkDelayMs = kModeSwitchAnimationMs + 20;
constexpr int kDialogPreferredWidth = 980;
constexpr int kDialogMinimumWidth = 640;
constexpr int kDialogMinimumHeight = 420;
constexpr const char *kModeSwitchCurrentIndexProperty = "currentIndex";
QLabel *addLabeledRow(QFormLayout *layout, const QString& text, QWidget *widget)
{
    auto *label = new QLabel(text);
    layout->addRow(label, widget);
    return label;
}

QLabel *addLabeledGridCell(QGridLayout *layout, int row, int column, const QString& text, QWidget *widget)
{
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const int gridColumn = column * 2;
    layout->addWidget(label, row, gridColumn, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(widget, row, gridColumn + 1, Qt::AlignLeft | Qt::AlignVCenter);
    return label;
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

QIcon enableToggleIcon(bool enabled)
{
    static const QIcon onIcon = createLucideIcon(QStringLiteral("check"), appThemeColor(AppThemeColor::TrackStart, false));
    static const QIcon offIcon = createLucideIcon(QStringLiteral("x"), appThemeColor(AppThemeColor::TrackEnd, false));
    return enabled ? onIcon : offIcon;
}

void configureEnableToggleButton(QPushButton *button)
{
    if (!button)
    {
        return;
    }
    button->setCheckable(true);
    button->setFlat(true);
    button->setFixedSize(kEnableToggleSize, kEnableToggleSize);
    button->setIconSize(QSize(kEnableToggleIconSize, kEnableToggleIconSize));
    button->setCursor(Qt::PointingHandCursor);
}

int skyConfigFieldWidth(QWidget *widget)
{
    const QFontMetrics metrics(widget->font());
    return metrics.horizontalAdvance(QString(kFieldDigitCount, QLatin1Char('8'))) + 52;
}

void polishConfigField(QWidget *widget)
{
    if (!widget)
    {
        return;
    }
    const int width = skyConfigFieldWidth(widget);
    widget->setMinimumWidth(width);
    widget->setMaximumWidth(width);
    widget->setMinimumHeight(kFieldHeight);
    widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void applyComboText(QComboBox *combo, const QString& value)
{
    const int index = combo->findText(value);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
    else
    {
        combo->setCurrentText(value);
    }
}

void setupFormLayout(QFormLayout *layout)
{
    layout->setContentsMargins(kCardFormHorizontalMargin, 12, kCardFormHorizontalMargin, 12);
    layout->setSpacing(kCardFormSpacing);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
}

QWidget *createEnableTitleAction(QWidget *parent, QLabel *&label, QPushButton *button)
{
    auto *container = new QWidget(parent);
    container->setObjectName(QStringLiteral("skyConfigEnableTitleAction"));
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    label = new QLabel(container);
    label->setObjectName(QStringLiteral("skyConfigEnableTitleLabel"));
    layout->addWidget(label, 0, Qt::AlignVCenter);
    layout->addWidget(button, 0, Qt::AlignVCenter);
    return container;
}

QWidget *createCardBody(QGroupBox *group, const QString& title, QWidget *titleAction = nullptr)
{
    group->setTitle(QString());
    group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    auto *cardLayout = new QVBoxLayout(group);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    auto *titleBar = new QWidget(group);
    titleBar->setObjectName(QStringLiteral("skyConfigGroupTitleBar"));
    titleBar->setFixedHeight(40);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 12, 0);
    titleLayout->setSpacing(0);

    auto *titleLabel = new QLabel(title, titleBar);
    titleLabel->setObjectName(QStringLiteral("skyConfigGroupTitleLabel"));
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    if (titleAction)
    {
        titleAction->setParent(titleBar);
        titleLayout->addWidget(titleAction, 0, Qt::AlignVCenter | Qt::AlignRight);
    }
    cardLayout->addWidget(titleBar);

    auto *body = new QWidget(group);
    body->setObjectName(QStringLiteral("skyConfigGroupBody"));
    body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    cardLayout->addWidget(body);
    return body;
}

QFormLayout *createCardFormLayout(QGroupBox *group, const QString& title, QWidget *titleAction = nullptr)
{
    auto *body = createCardBody(group, title, titleAction);
    auto *formLayout = new QFormLayout(body);
    setupFormLayout(formLayout);
    return formLayout;
}

void setCardTitle(QGroupBox *group, const QString& title)
{
    if (!group)
    {
        return;
    }
    group->setTitle(QString());
    if (auto *titleLabel = group->findChild<QLabel*>(QStringLiteral("skyConfigGroupTitleLabel")))
    {
        titleLabel->setText(title);
    }
}

bool isDarkApplicationPalette()
{
    return qApp && isDarkThemePalette(qApp->palette());
}

QString skyDeviceConfigStyleSheet(bool dark)
{
    if (!dark)
    {
        return applyAppThemeTokens(QStringLiteral(
            "QDialog#skyDeviceConfigDialog { background-color: @vv-config-window; }"
            "QDialog#skyDeviceConfigDialog QScrollArea { background-color: @vv-config-window; border: none; }"
            "QDialog#skyDeviceConfigDialog QScrollArea > QWidget { background-color: @vv-config-window; }"
            "QDialog#skyDeviceConfigDialog QWidget#skyConfigContent { background-color: @vv-config-window; }"
            "QDialog#skyDeviceConfigDialog QWidget#skyConfigRawPage { background-color: @vv-config-window; }"
            "QDialog#skyDeviceConfigDialog QGroupBox { background-color: @vv-config-surface; border: 1px solid @vv-config-border; border-radius: 8px; margin-top: 0px; padding: 0px; color: @vv-config-title-text; }"
            "QDialog#skyDeviceConfigDialog QWidget#skyConfigGroupTitleBar { background-color: @vv-white; border-bottom: 1px solid @vv-config-border; border-top-left-radius: 7px; border-top-right-radius: 7px; }"
            "QDialog#skyDeviceConfigDialog QLabel#skyConfigGroupTitleLabel { color: @vv-config-title-text; font-size: 15px; font-weight: bold; background-color: transparent; }"
            "QDialog#skyDeviceConfigDialog QLabel#skyConfigEnableTitleLabel { color: @vv-config-title-text; font-size: 14px; font-weight: normal; background-color: transparent; }"
            "QDialog#skyDeviceConfigDialog QWidget#skyConfigGroupBody { background-color: transparent; }"
            "QDialog#skyDeviceConfigDialog QLabel { color: @vv-config-text; }"
            "QDialog#skyDeviceConfigDialog QLabel#skyConfigRawStatus { color: @vv-config-muted-text; padding: 2px 4px; }"
            "QDialog#skyDeviceConfigDialog QLabel#skyConfigRawStatus[status=\"error\"] { color: @vv-error-text; }"
            "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle { background-color: transparent; border: none; border-radius: 6px; padding: 0; min-width: 30px; max-width: 30px; min-height: 30px; max-height: 30px; }"
            "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:hover { background-color: @vv-title-hover; border: none; }"
            "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:pressed { background-color: @vv-title-hover; border: none; }"
            "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:checked { background-color: transparent; border: none; }"
            "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:checked:hover { background-color: @vv-title-hover; border: none; }"
            "QDialog#skyDeviceConfigDialog QPlainTextEdit { background-color: @vv-white; color: @vv-text-strong; border: 1px solid @vv-config-border; border-radius: 8px; padding: 8px; font-family: Consolas, \"Cascadia Mono\", monospace; selection-background-color: @vv-primary-subtle; selection-color: @vv-primary; }"
        ), false);
    }

    return applyAppThemeTokens(QStringLiteral(
        "QDialog#skyDeviceConfigDialog { background-color: @vv-config-window; }"
        "QDialog#skyDeviceConfigDialog QScrollArea { background-color: @vv-config-window; border: none; }"
        "QDialog#skyDeviceConfigDialog QScrollArea > QWidget { background-color: @vv-config-window; }"
        "QDialog#skyDeviceConfigDialog QWidget#skyConfigContent { background-color: @vv-config-window; }"
        "QDialog#skyDeviceConfigDialog QWidget#skyConfigRawPage { background-color: @vv-config-window; }"
        "QDialog#skyDeviceConfigDialog QGroupBox { background-color: @vv-config-surface; border: 1px solid @vv-config-border; border-radius: 8px; margin-top: 0px; padding: 0px; color: @vv-config-title-text; }"
        "QDialog#skyDeviceConfigDialog QWidget#skyConfigGroupTitleBar { background-color: @vv-config-surface; border-bottom: 1px solid @vv-config-border; border-top-left-radius: 7px; border-top-right-radius: 7px; }"
        "QDialog#skyDeviceConfigDialog QLabel#skyConfigGroupTitleLabel { color: @vv-config-title-text; font-size: 15px; font-weight: bold; background-color: transparent; }"
        "QDialog#skyDeviceConfigDialog QLabel#skyConfigEnableTitleLabel { color: @vv-config-text; font-size: 14px; font-weight: normal; background-color: transparent; }"
        "QDialog#skyDeviceConfigDialog QWidget#skyConfigGroupBody { background-color: transparent; }"
        "QDialog#skyDeviceConfigDialog QLabel { color: @vv-config-text; background-color: transparent; }"
        "QDialog#skyDeviceConfigDialog QLabel#skyConfigRawStatus { color: @vv-config-muted-text; padding: 2px 4px; }"
        "QDialog#skyDeviceConfigDialog QLabel#skyConfigRawStatus[status=\"error\"] { color: @vv-error-text; }"
        "QDialog#skyDeviceConfigDialog QLineEdit,"
        "QDialog#skyDeviceConfigDialog QComboBox,"
        "QDialog#skyDeviceConfigDialog QSpinBox,"
        "QDialog#skyDeviceConfigDialog QDoubleSpinBox { background-color: @vv-config-surface; border: 1px solid @vv-config-border; border-radius: 6px; color: @vv-config-title-text; selection-background-color: @vv-primary-subtle-pressed; selection-color: @vv-white; }"
        "QDialog#skyDeviceConfigDialog QLineEdit:hover,"
        "QDialog#skyDeviceConfigDialog QComboBox:hover,"
        "QDialog#skyDeviceConfigDialog QSpinBox:hover,"
        "QDialog#skyDeviceConfigDialog QDoubleSpinBox:hover { border-color: @vv-config-border; }"
        "QDialog#skyDeviceConfigDialog QLineEdit:focus,"
        "QDialog#skyDeviceConfigDialog QComboBox:focus,"
        "QDialog#skyDeviceConfigDialog QSpinBox:focus,"
        "QDialog#skyDeviceConfigDialog QDoubleSpinBox:focus { border-color: @vv-focus; }"
        "QDialog#skyDeviceConfigDialog QComboBox QAbstractItemView { background-color: @vv-config-surface; border: 1px solid @vv-config-border; color: @vv-config-title-text; selection-background-color: @vv-primary-subtle; selection-color: @vv-white; }"
        "QDialog#skyDeviceConfigDialog QPushButton { background-color: @vv-primary; color: @vv-white; border: none; }"
        "QDialog#skyDeviceConfigDialog QPushButton:hover,"
        "QDialog#skyDeviceConfigDialog QPushButton:pressed,"
        "QDialog#skyDeviceConfigDialog QPushButton:checked { background-color: @vv-primary; color: @vv-white; }"
        "QDialog#skyDeviceConfigDialog QPushButton:disabled { background-color: @vv-config-border; color: @vv-text-disabled-strong; }"
        "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle { background-color: transparent; border: none; border-radius: 6px; padding: 0; min-width: 30px; max-width: 30px; min-height: 30px; max-height: 30px; }"
        "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:hover { background-color: @vv-title-hover; border: none; }"
        "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:pressed { background-color: @vv-title-hover; border: none; }"
        "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:checked { background-color: transparent; border: none; }"
        "QDialog#skyDeviceConfigDialog QPushButton#skyEnableToggle:checked:hover { background-color: @vv-title-hover; border: none; }"
        "QDialog#skyDeviceConfigDialog QPlainTextEdit { background-color: @vv-config-surface; color: @vv-config-title-text; border: 1px solid @vv-config-border; border-radius: 8px; padding: 8px; font-family: Consolas, \"Cascadia Mono\", monospace; selection-background-color: @vv-primary-subtle-pressed; selection-color: @vv-white; }"
    ), true);
}
}

class ConfigModeSwitch : public QWidget
{
public:
    explicit ConfigModeSwitch(QWidget *parent = nullptr)
        : QWidget(parent)
        , animation_(new QVariantAnimation(this))
    {
        setObjectName(QStringLiteral("skyConfigModeSwitch"));
        setFixedHeight(34);
        setMinimumWidth(244);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, false);
        setProperty(kModeSwitchCurrentIndexProperty, current_index_);
        animation_->setDuration(kModeSwitchAnimationMs);
        animation_->setEasingCurve(QEasingCurve::OutQuint);
        connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            thumb_position_ = value.toDouble();
            update();
        });

        refreshTheme();
    }

    void setLabels(const QString& visualLabel, const QString& rawLabel)
    {
        labels_[0] = visualLabel;
        labels_[1] = rawLabel;

        QFontMetrics metrics(font());
        const int textWidth = std::max(metrics.horizontalAdvance(labels_[0]), metrics.horizontalAdvance(labels_[1]));
        setMinimumWidth(std::max(244, textWidth * 2 + 64));
        updateGeometry();
        update();
    }

    void setCurrentIndex(int index, bool animate = true)
    {
        index = std::clamp(index, 0, 1);
        if (current_index_ == index)
        {
            update();
            return;
        }

        current_index_ = index;
        setProperty(kModeSwitchCurrentIndexProperty, current_index_);
        moveThumb(animate);
        update();
    }

    void refreshTheme()
    {
        const bool dark = isDarkApplicationPalette();
        background_color_ = appThemeColor(dark ? AppThemeColor::ConfigSurface : AppThemeColor::Surface, dark);
        border_color_ = appThemeColor(dark ? AppThemeColor::ConfigBorder : AppThemeColor::Border, dark);
        thumb_color_ = appThemeColor(AppThemeColor::Primary, dark);
        inactive_text_color_ = appThemeColor(AppThemeColor::ConfigToggleInactiveText, dark);
        active_text_color_ = appThemeColor(AppThemeColor::White, dark);
        update();
    }

    std::function<void(int)> onModeRequested;

protected:
    QSize sizeHint() const override
    {
        return QSize(std::max(minimumWidth(), 244), 34);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(border_color_, 1));
        painter.setBrush(background_color_);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                height() / 2.0,
                                height() / 2.0);

        const QRectF thumbRect = thumbGeometryForPosition(thumb_position_);
        painter.setPen(Qt::NoPen);
        painter.setBrush(thumb_color_);
        painter.drawRoundedRect(thumbRect, thumbRect.height() / 2.0, thumbRect.height() / 2.0);

        QFont textFont = font();
        textFont.setWeight(QFont::DemiBold);
        painter.setFont(textFont);
        const QRect leftRect(0, 0, width() / 2, height());
        const QRect rightRect(width() / 2, 0, width() - width() / 2, height());
        for (int i = 0; i < 2; ++i)
        {
            painter.setPen(i == current_index_ ? active_text_color_ : inactive_text_color_);
            painter.drawText(i == 0 ? leftRect : rightRect, Qt::AlignCenter, labels_[i]);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        {
            const int index = event->position().x() < width() / 2.0 ? 0 : 1;
            if (index != current_index_)
            {
                setCurrentIndex(index, true);
                if (onModeRequested)
                {
                    onModeRequested(index);
                }
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    QRectF thumbGeometryForPosition(double position) const
    {
        constexpr int margin = 3;
        const double segmentWidth = std::max(1.0, (width() - margin * 2) / 2.0);
        return QRectF(margin + segmentWidth * position,
                      margin,
                      segmentWidth,
                      std::max(1, height() - margin * 2));
    }

    void moveThumb(bool animate)
    {
        if (!animate)
        {
            animation_->stop();
            thumb_position_ = static_cast<double>(current_index_);
            update();
            return;
        }

        animation_->stop();
        animation_->setStartValue(thumb_position_);
        animation_->setEndValue(static_cast<double>(current_index_));
        animation_->start();
    }

    QVariantAnimation *animation_ = nullptr;
    QString labels_[2];
    QColor background_color_;
    QColor border_color_;
    QColor thumb_color_;
    QColor active_text_color_;
    QColor inactive_text_color_;
    int current_index_ = 0;
    double thumb_position_ = 0.0;
};

SkyDeviceConfigDialog::SkyDeviceConfigDialog(GroundTelemetryService *service, QWidget *parent)
    : QDialog(parent)
    , service_(service)
{
    setWindowFlag(Qt::Window, true);
    setupUi();
    VaporView::installCustomTitleBar(this);
    mode_switch_ = new ConfigModeSwitch();
    mode_switch_->onModeRequested = [this](int index) {
        const ConfigMode requestedMode = index == 0 ? ConfigMode::Visual : ConfigMode::Raw;
        const int requestSerial = ++mode_switch_request_serial_;
        QTimer::singleShot(kModeSwitchDeferredWorkDelayMs, this, [this, requestedMode, requestSerial]() {
            if (requestSerial != mode_switch_request_serial_)
            {
                return;
            }
            const bool applied = setConfigMode(requestedMode);
            if (!applied)
            {
                updateModeSwitch();
            }
        });
    };
    VaporView::addWidgetToCustomTitleBar(this, mode_switch_);
    if (service_)
    {
        connect(service_, &GroundTelemetryService::skyConfigReceived, this, &SkyDeviceConfigDialog::onSkyConfigReceived);
        connect(service_, &GroundTelemetryService::skyConfigApplyResultReceived, this, &SkyDeviceConfigDialog::onApplyResultReceived);
    }
    setConfig(SkyConfig::defaults());
    setEnglish(false);
    refreshSerialPortOptions();
    applyDynamicMetrics();
}

void SkyDeviceConfigDialog::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
}

void SkyDeviceConfigDialog::setFontScale(int percent)
{
    if (percent < 70 || percent > 150)
    {
        return;
    }
    font_scale_percent_ = percent;
    applyDynamicMetrics();
}

void SkyDeviceConfigDialog::onReadClicked()
{
    refreshSerialPortOptions();
    if (service_)
    {
        service_->requestSkyConfig();
    }
}

void SkyDeviceConfigDialog::onApplyClicked()
{
    if (!service_)
    {
        return;
    }

    SkyConfig config;
    QString error;
    if (config_mode_ == ConfigMode::Raw)
    {
        if (!configFromRawText(config, &error))
        {
            setRawStatus(error, true);
            QMessageBox::warning(this, is_english_ ? "Invalid Config" : "配置无效", error);
            return;
        }
        setConfig(config);
        setConfigMode(ConfigMode::Raw);
    }
    else
    {
        config = currentConfigFromUi();
    }

    if (!config.validate(&error))
    {
        setRawStatus(error, true);
        QMessageBox::warning(this, is_english_ ? "Invalid Config" : "配置无效", error);
        return;
    }
    current_config_ = config;
    syncRawTextFromVisual();
    setRawStatus(is_english_ ? QStringLiteral("Config sent to sky.") : QStringLiteral("配置已发送到天空端。"));
    service_->setSkyConfig(config.toJson());
}

void SkyDeviceConfigDialog::onSaveClicked()
{
    if (service_)
    {
        service_->saveSkyConfig();
    }
}

void SkyDeviceConfigDialog::onSkyConfigReceived(const QJsonObject& object)
{
    SkyConfig config;
    QString error;
    if (!SkyConfig::fromJson(object, config, &error))
    {
        if (raw_config_text_)
        {
            raw_config_text_->setPlainText(QJsonDocument(object).toJson(QJsonDocument::Indented));
        }
        if (mode_stack_ && raw_page_)
        {
            config_mode_ = ConfigMode::Raw;
            mode_stack_->setCurrentWidget(raw_page_);
            updateModeSwitch();
        }
        setRawStatus(QStringLiteral("Invalid config from sky: %1").arg(error), true);
        return;
    }
    setConfig(config);
    syncRawTextFromVisual();
    setRawStatus(is_english_ ? QStringLiteral("Config read from sky.") : QStringLiteral("已读取天空端配置。"));
}

void SkyDeviceConfigDialog::onApplyResultReceived(const QJsonObject& result)
{
    const bool success = result.value(QStringLiteral("success")).toBool(false);
    const QString error = result.value(QStringLiteral("error")).toString();
    QString message;
    if (success)
    {
        message = is_english_ ? QStringLiteral("Sky accepted the config.") : QStringLiteral("天空端已应用配置。");
    }
    else
    {
        message = error.isEmpty()
            ? (is_english_ ? QStringLiteral("Sky failed to apply the config.") : QStringLiteral("天空端应用配置失败。"))
            : (is_english_ ? QStringLiteral("Sky failed to apply the config: %1").arg(error)
                           : QStringLiteral("天空端应用配置失败：%1").arg(error));
    }
    setRawStatus(message, !success);
    if (raw_status_label_)
    {
        raw_status_label_->setToolTip(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)));
    }
}

void SkyDeviceConfigDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::PaletteChange)
    {
        applyThemeStyle();
    }
}

void SkyDeviceConfigDialog::setupUi()
{
    setObjectName(QStringLiteral("skyDeviceConfigDialog"));
    applyThemeStyle();
    setFont(qApp->font());
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 10);
    root->setSpacing(8);

    mode_stack_ = new QStackedWidget(this);
    visual_page_ = new QWidget(mode_stack_);
    raw_page_ = new QWidget(mode_stack_);
    raw_page_->setObjectName(QStringLiteral("skyConfigRawPage"));

    auto *visualLayout = new QVBoxLayout(visual_page_);
    visualLayout->setContentsMargins(0, 0, 0, 0);
    visualLayout->setSpacing(0);

    auto *scroll = new QScrollArea(visual_page_);
    scroll->setObjectName(QStringLiteral("skyConfigScrollArea"));
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("skyConfigContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);

    auto *deviceGrid = new QGridLayout();
    deviceGrid->setContentsMargins(0, 0, 0, 0);
    deviceGrid->setHorizontalSpacing(8);
    deviceGrid->setVerticalSpacing(8);
    for (int column = 0; column < 4; ++column)
    {
        deviceGrid->setColumnStretch(column, 1);
    }
    for (int row = 0; row < 3; ++row)
    {
        deviceGrid->setRowStretch(row, 0);
    }
    contentLayout->addLayout(deviceGrid);
    contentLayout->addStretch(1);

    auto addSerialGroup = [this, deviceGrid](const QString& title, QGroupBox*& group, SerialRow& row, int rowIndex, int columnIndex) {
        group = new QGroupBox(title, this);
        row = createSerialRow(title);
        auto *layout = createCardFormLayout(group, title, createEnableTitleAction(group, row.enabled_label, row.enabled));
        createSerialFields(layout, row);
        deviceGrid->addWidget(group, rowIndex, columnIndex);
    };

    addSerialGroup(QStringLiteral("EPSILON"), epsilon_group_, epsilon_, 0, 0);
    addSerialGroup(QStringLiteral("PTB210"), ptb_group_, ptb_, 0, 1);
    addSerialGroup(QStringLiteral("HMP3"), hmp_group_, hmp_, 0, 2);
    addSerialGroup(QStringLiteral("TFA1500-L"), lidar_group_, lidar_, 0, 3);
    addSerialGroup(QStringLiteral("RD105"), temperature_controller_group_, temperature_controller_, 1, 0);
    temperature_controller_slave_address_ = new QSpinBox(this);
    temperature_controller_slave_address_->setRange(1, 247);
    polishConfigField(temperature_controller_slave_address_);
    if (auto *layout = qobject_cast<QFormLayout *>(temperature_controller_group_->findChild<QWidget *>(QStringLiteral("skyConfigGroupBody"))->layout()))
    {
        temperature_controller_slave_address_label_ = addLabeledRow(layout, QStringLiteral("站号地址"), temperature_controller_slave_address_);
    }

    wave_group_ = new QGroupBox(QStringLiteral("Wave TCP"), this);
    wave_enabled_ = new QPushButton(this);
    wave_enabled_->setObjectName(QStringLiteral("skyEnableToggle"));
    configureEnableToggleButton(wave_enabled_);
    connect(wave_enabled_, &QPushButton::toggled, this, [this](bool) {
        updateEnableButton(wave_enabled_);
    });
    auto *waveLayout = createCardFormLayout(
        wave_group_,
        QStringLiteral("Wave TCP"),
        createEnableTitleAction(wave_group_, wave_enabled_label_, wave_enabled_));
    wave_host_ = new QLineEdit(this);
    wave_port_ = new QSpinBox(this);
    wave_port_->setRange(1, 65535);
    wave_downsample_ = new QSpinBox(this);
    wave_downsample_->setRange(1, 1000);
    for (QWidget *field : {static_cast<QWidget*>(wave_host_), static_cast<QWidget*>(wave_port_), static_cast<QWidget*>(wave_downsample_)})
    {
        polishConfigField(field);
    }
    wave_host_label_ = addLabeledRow(waveLayout, QStringLiteral("主机"), wave_host_);
    wave_port_label_ = addLabeledRow(waveLayout, QStringLiteral("端口"), wave_port_);
    wave_downsample_label_ = addLabeledRow(waveLayout, QStringLiteral("降采样倍率"), wave_downsample_);
    deviceGrid->addWidget(wave_group_, 1, 1);

    telemetry_group_ = new QGroupBox(QStringLiteral("数传配置"), this);
    auto *telemetryBody = createCardBody(telemetry_group_, QStringLiteral("数传配置"));
    auto makeRate = [this]() {
        auto *spin = new QDoubleSpinBox(this);
        spin->setRange(0.1, 200.0);
        spin->setDecimals(1);
        polishConfigField(spin);
        return spin;
    };
    telemetry_basic_rate_ = makeRate();
    telemetry_feature_rate_ = makeRate();
    telemetry_waveform_rate_ = makeRate();
    telemetry_heartbeat_rate_ = makeRate();
    telemetry_status_rate_ = makeRate();
    auto *telemetryGrid = new QGridLayout(telemetryBody);
    telemetryGrid->setContentsMargins(kCardFormHorizontalMargin, 12, kCardFormHorizontalMargin, 12);
    telemetryGrid->setHorizontalSpacing(12);
    telemetryGrid->setVerticalSpacing(kCardFormSpacing);
    telemetryGrid->setColumnStretch(1, 1);
    telemetryGrid->setColumnStretch(3, 1);
    telemetry_basic_label_ = addLabeledGridCell(telemetryGrid, 0, 0, QStringLiteral("基础遥测 Hz"), telemetry_basic_rate_);
    telemetry_feature_label_ = addLabeledGridCell(telemetryGrid, 0, 1, QStringLiteral("特征值 Hz"), telemetry_feature_rate_);
    telemetry_waveform_label_ = addLabeledGridCell(telemetryGrid, 1, 0, QStringLiteral("波形 Hz"), telemetry_waveform_rate_);
    telemetry_heartbeat_label_ = addLabeledGridCell(telemetryGrid, 1, 1, QStringLiteral("心跳 Hz"), telemetry_heartbeat_rate_);
    telemetry_status_label_ = addLabeledGridCell(telemetryGrid, 2, 0, QStringLiteral("状态 Hz"), telemetry_status_rate_);
    deviceGrid->addWidget(telemetry_group_, 1, 2, 1, 2);

    scroll->setWidget(content);
    visualLayout->addWidget(scroll);

    auto *rawLayout = new QVBoxLayout(raw_page_);
    rawLayout->setContentsMargins(0, 0, 0, 0);
    rawLayout->setSpacing(8);
    raw_status_label_ = new QLabel(raw_page_);
    raw_status_label_->setObjectName(QStringLiteral("skyConfigRawStatus"));
    raw_status_label_->setWordWrap(true);
    raw_config_text_ = new QPlainTextEdit(raw_page_);
    raw_config_text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    raw_config_text_->setMinimumHeight(360);
    rawLayout->addWidget(raw_status_label_);
    rawLayout->addWidget(raw_config_text_, 1);

    mode_stack_->addWidget(visual_page_);
    mode_stack_->addWidget(raw_page_);
    mode_stack_->setCurrentWidget(visual_page_);
    root->addWidget(mode_stack_, 1);

    auto *buttonBar = new QWidget(this);
    buttonBar->setObjectName(QStringLiteral("skyConfigButtonBar"));
    buttonBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    read_button_ = new QPushButton(buttonBar);
    apply_button_ = new QPushButton(buttonBar);
    save_button_ = new QPushButton(buttonBar);
    close_button_ = new QPushButton(buttonBar);
    connect(read_button_, &QPushButton::clicked, this, &SkyDeviceConfigDialog::onReadClicked);
    connect(apply_button_, &QPushButton::clicked, this, &SkyDeviceConfigDialog::onApplyClicked);
    connect(save_button_, &QPushButton::clicked, this, &SkyDeviceConfigDialog::onSaveClicked);
    connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(read_button_);
    buttonLayout->addWidget(apply_button_);
    buttonLayout->addWidget(save_button_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(close_button_);
    root->addWidget(buttonBar);

    applyThemeStyle();
    updateModeSwitch();
}

SkyDeviceConfigDialog::SerialRow SkyDeviceConfigDialog::createSerialRow(const QString&)
{
    SerialRow row;
    row.enabled = new QPushButton(this);
    row.enabled->setObjectName(QStringLiteral("skyEnableToggle"));
    configureEnableToggleButton(row.enabled);
    connect(row.enabled, &QPushButton::toggled, this, [this, button = row.enabled](bool) {
        updateEnableButton(button);
    });
    return row;
}

void SkyDeviceConfigDialog::createSerialFields(QFormLayout *layout, SerialRow& row)
{
    row.port = new QComboBox(this);
    row.port->setEditable(true);
    row.port->setInsertPolicy(QComboBox::NoInsert);
    row.baud = new QSpinBox(this);
    row.baud->setRange(1200, 4000000);
    row.frequency = new QDoubleSpinBox(this);
    row.frequency->setRange(0.1, 1000.0);
    row.frequency->setDecimals(1);
    polishConfigField(row.port);
    polishConfigField(row.baud);
    polishConfigField(row.frequency);
    row.port_label = addLabeledRow(layout, QStringLiteral("串口"), row.port);
    row.baud_label = addLabeledRow(layout, QStringLiteral("波特率"), row.baud);
    row.frequency_label = addLabeledRow(layout, QStringLiteral("频率 Hz"), row.frequency);
}

void SkyDeviceConfigDialog::setConfig(const SkyConfig& config)
{
    current_config_ = config;
    setSerialRow(epsilon_, config.epsilon);
    setSerialRow(ptb_, config.ptb);
    setSerialRow(hmp_, config.hmp);
    setSerialRow(lidar_, config.lidar);
    temperature_controller_.enabled->setChecked(config.temperature_controller.enabled);
    updateEnableButton(temperature_controller_.enabled);
    applyComboText(temperature_controller_.port, config.temperature_controller.port);
    temperature_controller_.baud->setValue(config.temperature_controller.baud_rate);
    temperature_controller_.frequency->setValue(config.temperature_controller.frequency_hz);
    temperature_controller_slave_address_->setValue(config.temperature_controller.slave_address);
    wave_enabled_->setChecked(config.wave_tcp.enabled);
    updateEnableButton(wave_enabled_);
    wave_host_->setText(config.wave_tcp.host);
    wave_port_->setValue(config.wave_tcp.port);
    wave_downsample_->setValue(config.wave_tcp.downsample_ratio);
    telemetry_basic_rate_->setValue(config.telemetry.basic_rate_hz);
    telemetry_feature_rate_->setValue(config.telemetry.feature_rate_hz);
    telemetry_waveform_rate_->setValue(config.telemetry.waveform_rate_hz);
    telemetry_heartbeat_rate_->setValue(config.telemetry.heartbeat_rate_hz);
    telemetry_status_rate_->setValue(config.telemetry.status_rate_hz);
    updateConfigPreview();
}

SkyConfig SkyDeviceConfigDialog::currentConfigFromUi() const
{
    SkyConfig config = current_config_;
    config.epsilon = serialConfigFromRow(epsilon_);
    config.ptb = serialConfigFromRow(ptb_);
    config.hmp = serialConfigFromRow(hmp_);
    config.lidar = serialConfigFromRow(lidar_);
    const SerialDeviceConfig temperatureSerial = serialConfigFromRow(temperature_controller_);
    config.temperature_controller.enabled = temperatureSerial.enabled;
    config.temperature_controller.port = temperatureSerial.port;
    config.temperature_controller.baud_rate = temperatureSerial.baud_rate;
    config.temperature_controller.frequency_hz = temperatureSerial.frequency_hz;
    config.temperature_controller.slave_address = temperature_controller_slave_address_->value();
    config.wave_tcp.enabled = wave_enabled_->isChecked();
    config.wave_tcp.host = wave_host_->text().trimmed();
    config.wave_tcp.port = wave_port_->value();
    config.wave_tcp.downsample_ratio = wave_downsample_->value();
    config.telemetry.basic_rate_hz = telemetry_basic_rate_->value();
    config.telemetry.feature_rate_hz = telemetry_feature_rate_->value();
    config.telemetry.waveform_rate_hz = telemetry_waveform_rate_->value();
    config.telemetry.heartbeat_rate_hz = telemetry_heartbeat_rate_->value();
    config.telemetry.status_rate_hz = telemetry_status_rate_->value();
    return config;
}

void SkyDeviceConfigDialog::setSerialRow(const SerialRow& row, const SerialDeviceConfig& config)
{
    row.enabled->setChecked(config.enabled);
    updateEnableButton(row.enabled);
    applyComboText(row.port, config.port);
    row.baud->setValue(config.baud_rate);
    row.frequency->setValue(config.frequency_hz);
}

SerialDeviceConfig SkyDeviceConfigDialog::serialConfigFromRow(const SerialRow& row) const
{
    SerialDeviceConfig config;
    config.enabled = row.enabled->isChecked();
    config.port = row.port->currentText().trimmed();
    config.baud_rate = row.baud->value();
    config.frequency_hz = row.frequency->value();
    return config;
}

void SkyDeviceConfigDialog::updateTexts()
{
    setWindowTitle(is_english_ ? "Sky Device Config" : "天空端设备配置");
    auto updateSerialLabels = [this](const SerialRow& row) {
        if (row.enabled_label) row.enabled_label->setText(is_english_ ? QStringLiteral("Enabled") : QStringLiteral("启用"));
        if (row.port_label) row.port_label->setText(is_english_ ? QStringLiteral("Port") : QStringLiteral("串口"));
        if (row.baud_label) row.baud_label->setText(is_english_ ? QStringLiteral("Baud") : QStringLiteral("波特率"));
        if (row.frequency_label) row.frequency_label->setText(is_english_ ? QStringLiteral("Frequency Hz") : QStringLiteral("频率 Hz"));
    };
    setCardTitle(epsilon_group_, QStringLiteral("EPSILON"));
    setCardTitle(ptb_group_, QStringLiteral("PTB210"));
    setCardTitle(hmp_group_, QStringLiteral("HMP3"));
    setCardTitle(lidar_group_, QStringLiteral("TFA1500-L"));
    setCardTitle(temperature_controller_group_, QStringLiteral("RD105"));
    setCardTitle(wave_group_, QStringLiteral("Wave TCP"));
    setCardTitle(telemetry_group_, is_english_ ? QStringLiteral("Telemetry") : QStringLiteral("数传配置"));
    updateSerialLabels(epsilon_);
    updateSerialLabels(ptb_);
    updateSerialLabels(hmp_);
    updateSerialLabels(lidar_);
    updateSerialLabels(temperature_controller_);
    if (temperature_controller_slave_address_label_) temperature_controller_slave_address_label_->setText(is_english_ ? QStringLiteral("Slave Address") : QStringLiteral("站号地址"));
    if (wave_enabled_label_) wave_enabled_label_->setText(is_english_ ? QStringLiteral("Enabled") : QStringLiteral("启用"));
    if (wave_host_label_) wave_host_label_->setText(is_english_ ? QStringLiteral("Host") : QStringLiteral("主机"));
    if (wave_port_label_) wave_port_label_->setText(is_english_ ? QStringLiteral("Port") : QStringLiteral("端口"));
    if (wave_downsample_label_) wave_downsample_label_->setText(is_english_ ? QStringLiteral("Downsample") : QStringLiteral("降采样倍率"));
    if (telemetry_basic_label_) telemetry_basic_label_->setText(is_english_ ? QStringLiteral("Basic Hz") : QStringLiteral("基础遥测 Hz"));
    if (telemetry_feature_label_) telemetry_feature_label_->setText(is_english_ ? QStringLiteral("Feature Hz") : QStringLiteral("特征值 Hz"));
    if (telemetry_waveform_label_) telemetry_waveform_label_->setText(is_english_ ? QStringLiteral("Waveform Hz") : QStringLiteral("波形 Hz"));
    if (telemetry_heartbeat_label_) telemetry_heartbeat_label_->setText(is_english_ ? QStringLiteral("Heartbeat Hz") : QStringLiteral("心跳 Hz"));
    if (telemetry_status_label_) telemetry_status_label_->setText(is_english_ ? QStringLiteral("Status Hz") : QStringLiteral("状态 Hz"));
    updateEnableButton(epsilon_.enabled);
    updateEnableButton(ptb_.enabled);
    updateEnableButton(hmp_.enabled);
    updateEnableButton(lidar_.enabled);
    updateEnableButton(temperature_controller_.enabled);
    updateEnableButton(wave_enabled_);
    read_button_->setText(is_english_ ? "Read From Sky" : "读取天空端配置");
    apply_button_->setText(is_english_ ? "Apply Config" : "应用配置");
    save_button_->setText(is_english_ ? "Save To Sky" : "保存到天空端");
    close_button_->setText(is_english_ ? "Close" : "关闭");
    if (mode_switch_)
    {
        mode_switch_->setLabels(is_english_ ? QStringLiteral("Visual Config") : QStringLiteral("可视化配置"),
                                is_english_ ? QStringLiteral("Raw Config") : QStringLiteral("原始配置"));
    }
    if (raw_status_label_ && raw_status_label_->text().isEmpty())
    {
        setRawStatus(is_english_
                         ? QStringLiteral("Edit the same JSON used by the sky config file.")
                         : QStringLiteral("可直接编辑天空端配置文件使用的 JSON。"));
    }
}

void SkyDeviceConfigDialog::refreshSerialPortOptions()
{
    QStringList ports;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts())
    {
        ports.push_back(info.portName());
    }
    ports.removeDuplicates();
    ports.sort(Qt::CaseInsensitive);

    auto refreshCombo = [&ports](QComboBox *combo) {
        if (!combo)
        {
            return;
        }
        const QString current = combo->currentText();
        combo->blockSignals(true);
        combo->clear();
        combo->addItems(ports);
        combo->setCurrentText(current);
        combo->blockSignals(false);
    };

    refreshCombo(epsilon_.port);
    refreshCombo(ptb_.port);
    refreshCombo(hmp_.port);
    refreshCombo(lidar_.port);
    refreshCombo(temperature_controller_.port);
}

void SkyDeviceConfigDialog::updateEnableButton(QPushButton *button)
{
    if (!button)
    {
        return;
    }
    button->setText(QString());
    button->setIcon(enableToggleIcon(button->isChecked()));
    button->setIconSize(QSize(kEnableToggleIconSize, kEnableToggleIconSize));
    button->setToolTip(button->isChecked()
                           ? (is_english_ ? QStringLiteral("Enabled") : QStringLiteral("已启用"))
                           : (is_english_ ? QStringLiteral("Disabled") : QStringLiteral("已禁用")));
}

void SkyDeviceConfigDialog::updateConfigPreview()
{
    if (!raw_config_text_)
    {
        return;
    }
    if (config_mode_ == ConfigMode::Visual || raw_config_text_->toPlainText().trimmed().isEmpty())
    {
        syncRawTextFromVisual();
    }
}

bool SkyDeviceConfigDialog::setConfigMode(ConfigMode mode)
{
    if (mode == config_mode_)
    {
        if (mode_stack_)
        {
            mode_stack_->setCurrentWidget(mode == ConfigMode::Raw ? raw_page_ : visual_page_);
        }
        updateModeSwitch();
        return true;
    }

    if (mode == ConfigMode::Raw)
    {
        syncRawTextFromVisual();
        config_mode_ = ConfigMode::Raw;
        if (mode_stack_ && raw_page_)
        {
            mode_stack_->setCurrentWidget(raw_page_);
        }
        setRawStatus(is_english_
                         ? QStringLiteral("Edit the same JSON used by the sky config file.")
                         : QStringLiteral("可直接编辑天空端配置文件使用的 JSON。"));
        updateModeSwitch();
        return true;
    }

    if (config_mode_ == ConfigMode::Raw)
    {
        SkyConfig config;
        QString error;
        if (!configFromRawText(config, &error))
        {
            setRawStatus(error, true);
            updateModeSwitch();
            return false;
        }
        setConfig(config);
    }

    config_mode_ = ConfigMode::Visual;
    if (mode_stack_ && visual_page_)
    {
        mode_stack_->setCurrentWidget(visual_page_);
    }
    setRawStatus(is_english_
                     ? QStringLiteral("Raw config is synchronized with the visual form.")
                     : QStringLiteral("原始配置已与可视化表单同步。"));
    updateModeSwitch();
    return true;
}

void SkyDeviceConfigDialog::updateModeSwitch()
{
    if (mode_switch_)
    {
        mode_switch_->setCurrentIndex(config_mode_ == ConfigMode::Visual ? 0 : 1);
    }
}

void SkyDeviceConfigDialog::syncRawTextFromVisual()
{
    if (!raw_config_text_)
    {
        return;
    }
    const QSignalBlocker blocker(raw_config_text_);
    raw_config_text_->setPlainText(QJsonDocument(currentConfigFromUi().toJson()).toJson(QJsonDocument::Indented));
}

bool SkyDeviceConfigDialog::configFromRawText(SkyConfig& config, QString *errorMessage) const
{
    if (!raw_config_text_)
    {
        if (errorMessage) *errorMessage = QStringLiteral("Raw config editor is unavailable");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw_config_text_->toPlainText().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (errorMessage)
        {
            *errorMessage = is_english_
                ? QStringLiteral("JSON parse error at offset %1: %2").arg(parseError.offset).arg(parseError.errorString())
                : QStringLiteral("JSON 解析错误，位置 %1：%2").arg(parseError.offset).arg(parseError.errorString());
        }
        return false;
    }
    if (!document.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = is_english_
                ? QStringLiteral("Sky config JSON root must be an object.")
                : QStringLiteral("天空端配置 JSON 根节点必须是对象。");
        }
        return false;
    }
    return SkyConfig::fromJson(document.object(), config, errorMessage);
}

void SkyDeviceConfigDialog::setRawStatus(const QString& message, bool error)
{
    if (!raw_status_label_)
    {
        return;
    }
    raw_status_label_->setText(message);
    raw_status_label_->setProperty("status", error ? QStringLiteral("error") : QStringLiteral("normal"));
    raw_status_label_->style()->unpolish(raw_status_label_);
    raw_status_label_->style()->polish(raw_status_label_);
    raw_status_label_->setToolTip(message);
}

void SkyDeviceConfigDialog::applyDynamicMetrics()
{
    setFont(qApp->font());
    const QList<QWidget*> fields = {
        epsilon_.port, epsilon_.baud, epsilon_.frequency,
        ptb_.port, ptb_.baud, ptb_.frequency,
        hmp_.port, hmp_.baud, hmp_.frequency,
        lidar_.port, lidar_.baud, lidar_.frequency,
        temperature_controller_.port, temperature_controller_.baud, temperature_controller_.frequency, temperature_controller_slave_address_,
        wave_host_, wave_port_, wave_downsample_,
        telemetry_basic_rate_, telemetry_feature_rate_, telemetry_waveform_rate_,
        telemetry_heartbeat_rate_, telemetry_status_rate_
    };
    for (QWidget *field : fields)
    {
        polishConfigField(field);
    }
    applyWindowSizing();
}

void SkyDeviceConfigDialog::applyWindowSizing()
{
    const QSize screenLimit = screenFractionSize(this);
    const QSize targetMinimumSize = QSize(kDialogMinimumWidth, kDialogMinimumHeight).boundedTo(screenLimit);
    setMinimumSize(targetMinimumSize);

    if (isMaximized() || isFullScreen())
    {
        return;
    }

    if (layout())
    {
        layout()->invalidate();
    }
    const QSize hint = sizeHint();
    const QSize preferredSize(kDialogPreferredWidth,
                              std::max(targetMinimumSize.height(), hint.height()));
    const QSize targetSize = defaultWindowSizeWithinScreenFraction(this, preferredSize, 0.5, targetMinimumSize);
    if (targetSize != size())
    {
        resize(targetSize);
    }
}

void SkyDeviceConfigDialog::applyThemeStyle()
{
    if (applying_theme_style_)
    {
        return;
    }

    const QString nextStyleSheet = skyDeviceConfigStyleSheet(isDarkApplicationPalette());
    if (styleSheet() == nextStyleSheet)
    {
        return;
    }

    applying_theme_style_ = true;
    setStyleSheet(nextStyleSheet);
    applying_theme_style_ = false;
    if (mode_switch_)
    {
        mode_switch_->refreshTheme();
    }
}

}  // namespace VaporView
