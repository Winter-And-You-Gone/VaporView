#pragma once

#include "TelemetryTypes.h"
#include "ground/widgets/VisualTextLabel.h"
#include "data_types.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace VaporView::Ground::Widgets
{
constexpr int kEpsilonSideTitleWidth = 24;
constexpr int kEpsilonSideTitleHorizontalPadding = 8;
constexpr int kEpsilonTitleColumnWidth = 90;
constexpr int kEpsilonMotionTitleColumnWidth = 180;
constexpr int kEpsilonLeftValueColumnWidth = 130;
constexpr int kEpsilonPositionValueColumnWidth = 112;
constexpr int kEpsilonMotionValueColumnWidth = 145;
constexpr int kEpsilonFieldBaseSpacing = 2;
constexpr int kEpsilonMotionFieldSpacing = 8;
constexpr int kEpsilonFieldMinimumHeight = 20;
constexpr int kEpsilonThreeColumnContentReserve = 96;

inline QFont numericFontFrom(const QFont& base)
{
    QFont font(base);
    font.setFamilies({
        QStringLiteral("Consolas"),
        QStringLiteral("Monaco"),
        QStringLiteral("Courier New")
    });
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}
class EpsilonPanel : public QWidget
{
public:
    explicit EpsilonPanel(QLabel *rateLabel = nullptr, QWidget *parent = nullptr)
        : QWidget(parent)
        , rate_label_(rateLabel)
        , cards_layout_(nullptr)
        , current_card_columns_(0)
        , is_english_(false)
        , compact_layout_(false)
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
        setObjectName(QStringLiteral("epsilonPanel"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
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
        updateCardGridLayout(true);
        QTimer::singleShot(0, this, [this]() {
            updateCardGridLayout(true);
        });
    }

    void setCompactLayout(bool compact)
    {
        const bool changed = compact_layout_ != compact;
        compact_layout_ = compact;
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        updateCardGridLayout(changed);
        QTimer::singleShot(0, this, [this]() {
            updateCardGridLayout(true);
        });
        if (layout())
        {
            layout()->invalidate();
            layout()->activate();
        }
        updateGeometry();
    }

    void updateData(const VaporView::EpsilonData& epsilon_data)
    {
        bool layoutWidthDirty = false;
        auto setValue = [this, &layoutWidthDirty](const QString& key, const QString& value) {
            if (QLabel *label = value_labels_.value(key, nullptr))
            {
                const QString display_text = value.isEmpty() ? QStringLiteral("--") : value;
                label->setText(display_text);
                label->setToolTip(display_text);
                label->ensurePolished();
                if (label->fontMetrics().horizontalAdvance(display_text) + 2 > label->minimumWidth())
                {
                    layoutWidthDirty = true;
                }
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
        auto valueTriple = [](double a, double b, double c, int decimals) {
            if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
            {
                return QString();
            }
            return QStringLiteral("%1/%2/%3")
                .arg(a, 0, 'f', decimals)
                .arg(b, 0, 'f', decimals)
                .arg(c, 0, 'f', decimals);
        };
        auto attitudeSourcesText = [this](int sourceCount) {
            if (sourceCount <= 0)
            {
                return QString();
            }
            return is_english_
                ? QStringLiteral("%1 source(s)").arg(sourceCount)
                : QStringLiteral("%1 路").arg(sourceCount);
        };
        auto attitudeDeltaText = [this](const VaporView::EpsilonData& sample) {
            if (sample.attitude_source_count < 2 || !std::isfinite(sample.attitude_delta_max_deg))
            {
                return QString();
            }

            const auto formatDelta = [](double value) {
                return QStringLiteral("%1°").arg(value, 0, 'f', 3);
            };
            QStringList parts;
            if (std::isfinite(sample.attitude_delta_ahrs_euler_deg))
            {
                parts << QStringLiteral("41-63 %1").arg(formatDelta(sample.attitude_delta_ahrs_euler_deg));
            }
            if (std::isfinite(sample.attitude_delta_ahrs_quat_deg))
            {
                parts << QStringLiteral("41-64 %1").arg(formatDelta(sample.attitude_delta_ahrs_quat_deg));
            }
            if (std::isfinite(sample.attitude_delta_euler_quat_deg))
            {
                parts << QStringLiteral("63-64 %1").arg(formatDelta(sample.attitude_delta_euler_quat_deg));
            }
            const QString maxText = formatDelta(sample.attitude_delta_max_deg);
            return is_english_
                ? QStringLiteral("max %1 (%2)").arg(maxText, parts.join(QStringLiteral(", ")))
                : QStringLiteral("最大 %1（%2）").arg(maxText, parts.join(QStringLiteral("， ")));
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
            ? valueTriple(epsilon_data.vel_n_mps, epsilon_data.vel_e_mps, epsilon_data.vel_d_mps, 3)
            : QString());
        setValue(QStringLiteral("imu_acc"),
                 valueTriple(epsilon_data.imu_acc_x_mps2, epsilon_data.imu_acc_y_mps2, epsilon_data.imu_acc_z_mps2, 3));
        setValue(QStringLiteral("imu_gyr"),
                 valueTriple(epsilon_data.imu_gyr_x_radps, epsilon_data.imu_gyr_y_radps, epsilon_data.imu_gyr_z_radps, 4));
        setValue(QStringLiteral("rpy"),
                 valueTriple(epsilon_data.roll_deg, epsilon_data.pitch_deg, epsilon_data.yaw_deg, 2));
        setValue(QStringLiteral("attitude_sources"), attitudeSourcesText(epsilon_data.attitude_source_count));
        setValue(QStringLiteral("attitude_delta"), attitudeDeltaText(epsilon_data));
        setValue(QStringLiteral("acc"),
                 gnss_fix_valid && std::isfinite(epsilon_data.hacc_m) && std::isfinite(epsilon_data.vacc_m)
                     ? QStringLiteral("%1m/%2m")
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
        if (layoutWidthDirty)
        {
            updateCardGridLayout(true);
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateCardGridLayout(true);
    }

private:
    QString formatRateValue(double hz) const
    {
        constexpr int kRateFieldChars = 9; // "-999.9 Hz" keeps signs and separators stable.
        QString text;
        if (!(hz > 0.0) || !std::isfinite(hz))
        {
            text = QStringLiteral("-- Hz");
        }
        else
        {
            text = QStringLiteral("%1 Hz").arg(hz, 0, 'f', 1);
        }
        return text.rightJustified(std::max(kRateFieldChars, static_cast<int>(text.size())), QLatin1Char(' '));
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
        rate_label_->setText(totalText);
        rate_label_->setToolTip(totalText);
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

    static int sectionTitleWidthForLabel(const QLabel *label)
    {
        if (!label)
        {
            return kEpsilonSideTitleWidth;
        }

        const QFontMetrics metrics(label->font());
        int textWidth = 0;
        const QStringList lines = label->text().split(QChar('\n'));
        for (const QString& line : lines)
        {
            textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
        }
        return std::max(kEpsilonSideTitleWidth,
                        textWidth + kEpsilonSideTitleHorizontalPadding);
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
        label->setFixedWidth(kEpsilonSideTitleWidth);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        section_labels_.insert(key, label);
        section_zh_.insert(key, zhTitle);
        section_en_.insert(key, enTitle);
    }

    int availableCardWidth() const
    {
        int availableWidth = contentsRect().width();
        if (const QWidget *parent = parentWidget())
        {
            const int parentWidth = parent->contentsRect().width() - 4;
            if (parentWidth > 0)
            {
                availableWidth = availableWidth > 0
                    ? std::min(availableWidth, parentWidth)
                    : parentWidth;
            }
            if (availableWidth <= 0)
            {
                if (const QWidget *grandParent = parent->parentWidget())
                {
                    const int grandParentWidth = grandParent->contentsRect().width() - 8;
                    if (grandParentWidth > 0)
                    {
                        availableWidth = grandParentWidth;
                    }
                }
            }
        }
        return availableWidth;
    }

    int desiredCardColumns() const
    {
        if (section_cards_.isEmpty())
        {
            return 1;
        }

        const int availableWidth = availableCardWidth();
        if (availableWidth <= 0)
        {
            return compact_layout_ ? 1 : std::min(2, static_cast<int>(section_cards_.size()));
        }

        const QVector<int> widths = standardCardWidths();
        const int gap = std::max(0, cards_layout_ ? cards_layout_->horizontalSpacing() : 0);
        const int cardCount = widths.size();
        int allWidth = 0;
        for (int width : widths)
        {
            allWidth += width;
        }
        allWidth += gap * std::max(0, cardCount - 1);
        if (cardCount >= 3 && availableWidth >= allWidth + kEpsilonThreeColumnContentReserve)
        {
            return 3;
        }

        if (cardCount >= 2)
        {
            const int firstRowWidth = widths.at(0) + widths.at(1) + gap;
            const int wrappedRowWidth = cardCount >= 3 ? widths.at(2) : 0;
            if (availableWidth >= std::max(firstRowWidth, wrappedRowWidth))
            {
                return 2;
            }
        }

        return 1;
    }

    QVector<int> standardCardWidths() const
    {
        QVector<int> widths;
        widths.reserve(section_card_standard_widths_.size());
        for (int width : section_card_standard_widths_)
        {
            widths.push_back(width);
        }
        return widths;
    }

    int fieldSpacingForSection(const QString& key) const
    {
        return key == QStringLiteral("motion")
            ? kEpsilonMotionFieldSpacing
            : kEpsilonFieldBaseSpacing;
    }

    int sectionTitleWidthForCard(int cardIndex) const
    {
        const QString key = cardIndex >= 0 && cardIndex < section_card_keys_.size()
            ? section_card_keys_.at(cardIndex)
            : QString();
        return sectionTitleWidthForLabel(section_labels_.value(key, nullptr));
    }

    int sectionChromeWidthForCard(int cardIndex) const
    {
        int chromeWidth = sectionTitleWidthForCard(cardIndex);
        if (cardIndex >= 0 && cardIndex < section_cards_.size())
        {
            if (const QLayout *outerLayout = section_cards_.at(cardIndex)->layout())
            {
                const QMargins margins = outerLayout->contentsMargins();
                chromeWidth += margins.left() + margins.right() + outerLayout->spacing();
            }
        }
        if (cardIndex >= 0 && cardIndex < section_card_grids_.size())
        {
            if (const QGridLayout *grid = section_card_grids_.at(cardIndex))
            {
                const QMargins margins = grid->contentsMargins();
                chromeWidth += margins.left() + margins.right();
            }
        }
        return chromeWidth;
    }

    int fieldSpacingForCard(int cardIndex) const
    {
        const QString key = cardIndex >= 0 && cardIndex < section_card_keys_.size()
            ? section_card_keys_.at(cardIndex)
            : QString();
        return fieldSpacingForSection(key);
    }

    QStringList valueWidthSamplesForSection(const QString& key) const
    {
        if (key == QStringLiteral("status"))
        {
            return is_english_
                ? QStringList{QStringLiteral("2026-07-01T23:59:59.999Z"),
                              QStringLiteral("1782934672910000 us"),
                              QStringLiteral("raw 9999 / dropped 999"),
                              QStringLiteral("0X0060 initialized / position fusion active"),
                              QStringLiteral("Yes")}
                : QStringList{QStringLiteral("2026-07-01T23:59:59.999Z"),
                              QStringLiteral("1782934672910000 us"),
                              QStringLiteral("原始 9999 / 丢帧 999"),
                              QStringLiteral("0X0060 已初始化 / 定位融合中"),
                              QStringLiteral("是")};
        }
        if (key == QStringLiteral("position"))
        {
            return QStringList{QStringLiteral("RTK_FIXED"),
                               QStringLiteral("99"),
                               QStringLiteral("-179.99999999"),
                               QStringLiteral("-9999.999"),
                               QStringLiteral("0.999m/0.999m")};
        }
        if (key == QStringLiteral("motion"))
        {
            return is_english_
                ? QStringList{QStringLiteral("-12.345/12.345/-12.345"),
                              QStringLiteral("-12.345/-12.345/12.345"),
                              QStringLiteral("-0.1234/-0.1234/0.1234"),
                              QStringLiteral("-179.99/-89.99/359.99"),
                              QStringLiteral("max 999.999° (41-63 999.999°, 41-64 999.999°, 63-64 999.999°)")}
                : QStringList{QStringLiteral("-12.345/12.345/-12.345"),
                              QStringLiteral("-12.345/-12.345/12.345"),
                              QStringLiteral("-0.1234/-0.1234/0.1234"),
                              QStringLiteral("-179.99/-89.99/359.99"),
                              QStringLiteral("最大 999.999°（41-63 999.999°，41-64 999.999°，63-64 999.999°）")};
        }
        return {};
    }

    bool syncSectionTitleWidths()
    {
        bool changed = false;
        for (int i = 0; i < section_card_keys_.size(); ++i)
        {
            QLabel *label = section_labels_.value(section_card_keys_.at(i), nullptr);
            const int titleWidth = sectionTitleWidthForLabel(label);
            if (label &&
                (label->width() != titleWidth ||
                 label->minimumWidth() != titleWidth ||
                 label->maximumWidth() != titleWidth))
            {
                label->setFixedWidth(titleWidth);
                changed = true;
            }
            if (i < section_card_chrome_widths_.size())
            {
                const int chromeWidth = sectionChromeWidthForCard(i);
                if (section_card_chrome_widths_.at(i) != chromeWidth)
                {
                    section_card_chrome_widths_[i] = chromeWidth;
                    changed = true;
                }
            }
        }
        return changed;
    }

    bool syncSectionColumnWidths()
    {
        const int count = std::min(section_card_grids_.size(), section_card_title_labels_.size());
        bool changed = false;
        for (int i = 0; i < count; ++i)
        {
            int titleWidth = section_card_title_widths_.value(i, 0);
            for (QLabel *label : section_card_title_labels_.at(i))
            {
                if (!label)
                {
                    continue;
                }
                titleWidth = std::max(titleWidth,
                                      QFontMetrics(label->font()).horizontalAdvance(label->text()));
            }
            int valueWidth = 0;
            if (i < section_card_value_labels_.size())
            {
                for (QLabel *label : section_card_value_labels_.at(i))
                {
                    if (!label)
                    {
                        continue;
                    }
                    label->ensurePolished();
                    valueWidth = std::max(valueWidth, label->fontMetrics().horizontalAdvance(label->text()));
                }
            }
            const QString sectionKey = i < section_card_keys_.size() ? section_card_keys_.at(i) : QString();
            for (const QString& sample : valueWidthSamplesForSection(sectionKey))
            {
                if (i < section_card_value_labels_.size() && !section_card_value_labels_.at(i).isEmpty())
                {
                    QLabel *sampleLabel = section_card_value_labels_.at(i).constFirst();
                    if (sampleLabel)
                    {
                        sampleLabel->ensurePolished();
                        valueWidth = std::max(valueWidth, sampleLabel->fontMetrics().horizontalAdvance(sample));
                    }
                }
            }
            valueWidth += 2;
            QGridLayout *grid = section_card_grids_.at(i);
            if (!grid)
            {
                continue;
            }
            if (grid->columnMinimumWidth(0) != titleWidth)
            {
                grid->setColumnMinimumWidth(0, titleWidth);
                changed = true;
            }
            if (grid->columnMinimumWidth(1) != valueWidth)
            {
                grid->setColumnMinimumWidth(1, valueWidth);
                changed = true;
            }
            for (QLabel *label : section_card_title_labels_.at(i))
            {
                if (label)
                {
                    if (label->width() != titleWidth ||
                        label->minimumWidth() != titleWidth ||
                        label->maximumWidth() != titleWidth)
                    {
                        label->setFixedWidth(titleWidth);
                        changed = true;
                    }
                }
            }
            if (i < section_card_value_labels_.size())
            {
                for (QLabel *label : section_card_value_labels_.at(i))
                {
                    if (!label)
                    {
                        continue;
                    }
                    if (label->minimumWidth() != valueWidth ||
                        label->maximumWidth() != QWIDGETSIZE_MAX)
                    {
                        label->setMinimumWidth(valueWidth);
                        label->setMaximumWidth(QWIDGETSIZE_MAX);
                        changed = true;
                    }
                }
            }
            if (i < section_card_standard_widths_.size() &&
                i < section_card_chrome_widths_.size() &&
                i < section_card_value_widths_.size())
            {
                const int standardWidth = section_card_chrome_widths_.at(i) +
                                          titleWidth +
                                          fieldSpacingForCard(i) +
                                          valueWidth;
                if (section_card_standard_widths_.at(i) != standardWidth)
                {
                    section_card_standard_widths_[i] = standardWidth;
                    changed = true;
                }
                if (i < section_cards_.size() &&
                    section_cards_.at(i) &&
                    section_cards_.at(i)->minimumWidth() != standardWidth)
                {
                    section_cards_.at(i)->setMinimumWidth(standardWidth);
                    changed = true;
                }
                if (section_card_value_widths_.at(i) != valueWidth)
                {
                    section_card_value_widths_[i] = valueWidth;
                    changed = true;
                }
            }
        }
        return changed;
    }

    void setCardsExpandable(bool expandable)
    {
        for (QFrame *card : section_cards_)
        {
            card->setSizePolicy(expandable ? QSizePolicy::Expanding : QSizePolicy::Maximum,
                                expandable ? QSizePolicy::Expanding : QSizePolicy::Preferred);
        }
    }

    void syncCardMinimumHeights()
    {
        int maximumMinimumHeight = 0;
        for (QFrame *card : section_cards_)
        {
            if (QLayout *cardLayout = card->layout())
            {
                maximumMinimumHeight = std::max(maximumMinimumHeight, cardLayout->minimumSize().height());
            }
        }
        for (QFrame *card : section_cards_)
        {
            if (maximumMinimumHeight > 0)
            {
                card->setMinimumHeight(maximumMinimumHeight);
            }
        }
    }

    void updateFieldSpacingForCard(int cardIndex, int targetCardWidth)
    {
        if (cardIndex < 0 ||
            cardIndex >= section_card_grids_.size() ||
            cardIndex >= section_cards_.size() ||
            cardIndex >= section_card_standard_widths_.size())
        {
            return;
        }

        Q_UNUSED(targetCardWidth);
        section_card_grids_.at(cardIndex)->setHorizontalSpacing(fieldSpacingForCard(cardIndex));
    }

    void updateFieldSpacings(int columns, const QVector<int>& widths)
    {
        if (widths.isEmpty())
        {
            return;
        }

        const int availableWidth = std::max(0, availableCardWidth());
        const int gap = std::max(0, cards_layout_ ? cards_layout_->horizontalSpacing() : 0);
        if (columns >= 3)
        {
            for (int i = 0; i < widths.size(); ++i)
            {
                updateFieldSpacingForCard(i, widths.at(i));
            }
            return;
        }

        if (columns == 2 && widths.size() >= 3)
        {
            const int firstRowAvailableWidth = std::max(0, availableWidth - gap);
            const int firstRowStandardWidth = std::max(1, widths.at(0) + widths.at(1));
            const int firstWidth = std::max(widths.at(0), firstRowAvailableWidth * widths.at(0) / firstRowStandardWidth);
            const int secondWidth = std::max(widths.at(1), firstRowAvailableWidth - firstWidth);
            updateFieldSpacingForCard(0, firstWidth);
            updateFieldSpacingForCard(1, secondWidth);
            updateFieldSpacingForCard(2, availableWidth);
            return;
        }

        for (int i = 0; i < widths.size(); ++i)
        {
            updateFieldSpacingForCard(i, availableWidth);
        }
    }

    void updateCardGridLayout(bool force = false)
    {
        if (!cards_layout_ || section_cards_.isEmpty())
        {
            return;
        }

        cards_layout_->setHorizontalSpacing(compact_layout_ ? 4 : 2);
        cards_layout_->setVerticalSpacing(compact_layout_ ? 4 : 2);
        const bool titleWidthsChanged = syncSectionTitleWidths();
        const bool columnWidthsChanged = syncSectionColumnWidths();
        const bool widthsChanged = titleWidthsChanged || columnWidthsChanged;
        syncCardMinimumHeights();
        const QVector<int> widths = standardCardWidths();

        const int columns = desiredCardColumns();
        updateFieldSpacings(columns, widths);
        if (!force && current_card_columns_ == columns && !widthsChanged)
        {
            return;
        }

        for (QFrame *card : section_cards_)
        {
            cards_layout_->removeWidget(card);
        }
        for (int i = 0; i < 5; ++i)
        {
            cards_layout_->setColumnStretch(i, 0);
            cards_layout_->setRowStretch(i, 0);
            cards_layout_->setColumnMinimumWidth(i, 0);
        }

        if (columns >= 3)
        {
            setCardsExpandable(true);
            for (int i = 0; i < section_cards_.size(); ++i)
            {
                if (i < widths.size())
                {
                    cards_layout_->setColumnMinimumWidth(i, widths.at(i));
                    cards_layout_->setColumnStretch(i, std::max(1, widths.at(i)));
                }
                cards_layout_->addWidget(section_cards_.at(i), 0, i);
            }
        }
        else if (columns == 2 && section_cards_.size() >= 3)
        {
            setCardsExpandable(true);
            // Keep the short top cards content-sized; the motion card still spans both columns.
            section_cards_.at(0)->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
            section_cards_.at(1)->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
            cards_layout_->setColumnMinimumWidth(0, widths.at(0));
            cards_layout_->setColumnMinimumWidth(1, widths.at(1));
            cards_layout_->setColumnStretch(0, 0);
            cards_layout_->setColumnStretch(1, 0);
            cards_layout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            cards_layout_->addWidget(section_cards_.at(0), 0, 0, Qt::AlignLeft | Qt::AlignTop);
            cards_layout_->addWidget(section_cards_.at(1), 0, 1, Qt::AlignLeft | Qt::AlignTop);
            cards_layout_->addWidget(section_cards_.at(2), 1, 0, 1, 2);
        }
        else
        {
            setCardsExpandable(true);
            cards_layout_->setColumnStretch(0, 1);
            for (int i = 0; i < section_cards_.size(); ++i)
            {
                cards_layout_->addWidget(section_cards_.at(i), i, 0);
            }
        }

        current_card_columns_ = columns;
        cards_layout_->invalidate();
        cards_layout_->activate();
        updateGeometry();
    }

    QGridLayout *addSectionCard(const QString& key,
                                const QString& zhTitle,
                                const QString& enTitle,
                                int titleColumnWidth,
                                int valueColumnWidth)
    {
        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("epsilonSectionCard"));
        card->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        section_cards_.push_back(card);

        auto *outerLayout = new QHBoxLayout(card);
        outerLayout->setContentsMargins(2, 2, 2, 2);
        outerLayout->setSpacing(2);

        auto *sectionLabel = new QLabel(card);
        registerSectionLabel(sectionLabel, key, zhTitle, enTitle);
        outerLayout->addWidget(sectionLabel);

        auto *cardLayout = new QGridLayout();
        cardLayout->setContentsMargins(2, 2, 2, 2);
        cardLayout->setHorizontalSpacing(fieldSpacingForSection(key));
        cardLayout->setVerticalSpacing(2);
        cardLayout->setColumnMinimumWidth(0, titleColumnWidth);
        cardLayout->setColumnMinimumWidth(1, valueColumnWidth);
        cardLayout->setColumnStretch(0, 0);
        cardLayout->setColumnStretch(1, 1);
        outerLayout->addLayout(cardLayout, 1);
        section_card_grids_.push_back(cardLayout);
        section_card_keys_.push_back(key);
        section_card_title_widths_.push_back(titleColumnWidth);
        section_card_title_labels_.push_back({});
        section_card_value_labels_.push_back({});
        const int chromeWidth = kEpsilonSideTitleWidth + outerLayout->contentsMargins().left() +
                                outerLayout->contentsMargins().right() + outerLayout->spacing() +
                                cardLayout->contentsMargins().left() + cardLayout->contentsMargins().right();
        section_card_chrome_widths_.push_back(chromeWidth);
        section_card_value_widths_.push_back(valueColumnWidth);
        section_card_standard_widths_.push_back(chromeWidth + titleColumnWidth + fieldSpacingForSection(key) + valueColumnWidth);
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
        title->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        if (const int titleColumnWidth = layout->columnMinimumWidth(column * 2); titleColumnWidth > 0)
        {
            title->setFixedWidth(titleColumnWidth);
        }
        title->setMinimumHeight(kEpsilonFieldMinimumHeight);
        QLabel *value = new QLabel(QStringLiteral("--"), this);
        value->setObjectName(QStringLiteral("valueLabel"));
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
        value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        value->setMinimumHeight(kEpsilonFieldMinimumHeight);
        value->setMinimumWidth(valueColumnWidth);
        layout->setRowMinimumHeight(row, kEpsilonFieldMinimumHeight);
        layout->addWidget(title, row, column * 2);
        layout->addWidget(value, row, column * 2 + 1);
        title_labels_.insert(key, title);
        value_labels_.insert(key, value);
        title_zh_.insert(key, zhTitle);
        title_en_.insert(key, enTitle);
        const int cardIndex = section_card_grids_.indexOf(layout);
        if (cardIndex >= 0 && cardIndex < section_card_title_labels_.size())
        {
            section_card_title_labels_[cardIndex].push_back(title);
        }
        if (cardIndex >= 0 && cardIndex < section_card_value_labels_.size())
        {
            section_card_value_labels_[cardIndex].push_back(value);
        }
    }

    void setupUi()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(2);

        if (!rate_label_)
        {
            rate_label_ = new VaporView::VisualTextLabel(this);
            rate_label_->setObjectName(QStringLiteral("rateLabel"));
            rate_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            layout->addWidget(rate_label_);
        }
        rate_label_->setFont(numericFontFrom(rate_label_->font()));
        rate_label_->setMinimumWidth(0);
        rate_label_->setMaximumWidth(QWIDGETSIZE_MAX);
        rate_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        cards_layout_ = new QGridLayout();
        cards_layout_->setContentsMargins(0, 0, 0, 0);
        cards_layout_->setHorizontalSpacing(2);
        cards_layout_->setVerticalSpacing(2);

        QGridLayout *statusGrid = addSectionCard(QStringLiteral("status"),
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

        QGridLayout *positionGrid = addSectionCard(QStringLiteral("position"),
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
        addField(positionGrid, row++, 0, QStringLiteral("acc"), QStringLiteral("hAcc/vAcc:"), QStringLiteral("hAcc/vAcc:"), kEpsilonPositionValueColumnWidth);

        QGridLayout *motionGrid = addSectionCard(QStringLiteral("motion"),
                                                 QStringLiteral("姿态与运动"),
                                                 QStringLiteral("Attitude / Motion"),
                                                 kEpsilonMotionTitleColumnWidth,
                                                 kEpsilonMotionValueColumnWidth);
        row = 0;
        addField(motionGrid, row++, 0, QStringLiteral("ned_vel"), QStringLiteral("NED速度[m/s][N/E/D]:"), QStringLiteral("NED Velocity [m/s][N/E/D]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("imu_acc"), QStringLiteral("IMU加速度[m/s²][X/Y/Z]:"), QStringLiteral("IMU Accel [m/s²][X/Y/Z]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("imu_gyr"), QStringLiteral("IMU角速度[rad/s][X/Y/Z]:"), QStringLiteral("IMU Gyro [rad/s][X/Y/Z]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("rpy"), QStringLiteral("姿态角[deg][Roll/Pitch/Yaw]:"), QStringLiteral("Attitude [deg][Roll/Pitch/Yaw]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("attitude_sources"), QStringLiteral("姿态来源[0x41/0x63/0x64]:"), QStringLiteral("Attitude Sources [0x41/0x63/0x64]:"), kEpsilonMotionValueColumnWidth);
        addField(motionGrid, row++, 0, QStringLiteral("attitude_delta"), QStringLiteral("姿态一致性[最大差值]:"), QStringLiteral("Attitude Consistency [max delta]:"), kEpsilonMotionValueColumnWidth);

        updateCardGridLayout(true);
        layout->addLayout(cards_layout_, 0);
        layout->addStretch(1);
    }

    QLabel *rate_label_;
    QGridLayout *cards_layout_;
    QVector<QFrame*> section_cards_;
    QVector<QGridLayout*> section_card_grids_;
    QVector<QString> section_card_keys_;
    QVector<int> section_card_title_widths_;
    QVector<int> section_card_standard_widths_;
    QVector<int> section_card_chrome_widths_;
    QVector<int> section_card_value_widths_;
    QVector<QVector<QLabel*>> section_card_title_labels_;
    QVector<QVector<QLabel*>> section_card_value_labels_;
    int current_card_columns_;
    QHash<QString, QLabel*> section_labels_;
    QHash<QString, QLabel*> title_labels_;
    QHash<QString, QLabel*> value_labels_;
    QHash<QString, QString> section_zh_;
    QHash<QString, QString> section_en_;
    QHash<QString, QString> title_zh_;
    QHash<QString, QString> title_en_;
    bool is_english_;
    bool compact_layout_;
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

} // namespace VaporView::Ground::Widgets
