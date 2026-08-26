#include "ground/main/RecordingStatusView.h"

#include "ground/main/GroundMainWindowSupport.h"

#include <QGridLayout>
#include <QLabel>
#include <QList>
#include <QSizePolicy>

#include <algorithm>
#include <utility>

namespace
{

struct RecordingStatusLine
{
    bool fullWidth = false;
    bool section = false;
    QString label;
    QString value;
    QString unit;
};

bool isRecordingStatusUnit(const QString& text)
{
    return text == QStringLiteral("行") ||
           text == QStringLiteral("帧") ||
           text == QStringLiteral("条") ||
           text == QStringLiteral("rows") ||
           text == QStringLiteral("frames") ||
           text == QStringLiteral("features") ||
           text == QStringLiteral("records");
}

RecordingStatusLine parseRecordingStatusLine(const QString& line, int row)
{
    RecordingStatusLine result;
    const QString trimmed = line.trimmed();
    result.label = trimmed;
    if (trimmed.isEmpty())
    {
        result.fullWidth = true;
        return result;
    }

    const bool fullWidthLine = row == 0 ||
                               trimmed.endsWith(QChar(0xFF1A)) ||
                               trimmed.endsWith(QLatin1Char(':'));
    if (fullWidthLine)
    {
        result.fullWidth = true;
        result.section = row != 0;
        return result;
    }

    int separator = line.indexOf(QChar(0xFF1A));
    int separatorWidth = 1;
    if (separator < 0)
    {
        separator = line.indexOf(QStringLiteral(": "));
        separatorWidth = 2;
    }
    if (separator < 0)
    {
        separator = line.indexOf(QLatin1Char(':'));
        separatorWidth = 1;
    }
    if (separator < 0)
    {
        result.fullWidth = true;
        return result;
    }

    result.label = line.left(separator + 1).trimmed();
    result.value = line.mid(separator + separatorWidth).trimmed();

    const int unitSeparator = result.value.lastIndexOf(QLatin1Char(' '));
    if (unitSeparator > 0)
    {
        const QString candidate = result.value.mid(unitSeparator + 1);
        if (isRecordingStatusUnit(candidate))
        {
            result.unit = candidate;
            result.value = result.value.left(unitSeparator).trimmed();
        }
    }
    return result;
}

QLabel *createRecordingStatusLabel(QWidget *parent,
                                   const QString& objectName,
                                   const QString& text,
                                   Qt::Alignment alignment)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(objectName);
    label->setAlignment(alignment | Qt::AlignVCenter);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(false);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    label->ensurePolished();
    label->setMinimumHeight(label->fontMetrics().height() + 2);
    return label;
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

}  // namespace

namespace VaporView::Ground::Main
{

RecordingStatusView::RecordingStatusView(QWidget *parent)
    : QWidget(parent)
    , grid_layout_(new QGridLayout(this))
{
    setObjectName(QStringLiteral("recordingStatusView"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(false);
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

    grid_layout_->setContentsMargins(0, 0, 0, 0);
    grid_layout_->setHorizontalSpacing(4);
    grid_layout_->setVerticalSpacing(1);
    grid_layout_->setColumnStretch(0, 1);
    grid_layout_->setColumnStretch(1, 0);
    grid_layout_->setColumnStretch(2, 0);
}

void RecordingStatusView::setStatusText(const QString& plainText)
{
    if (status_text_ == plainText)
    {
        setToolTip(plainText);
        return;
    }

    status_text_ = plainText;
    setToolTip(plainText);

    const int previousRowCount = grid_layout_->rowCount();
    for (int row = 0; row < previousRowCount; ++row)
    {
        grid_layout_->setRowMinimumHeight(row, 0);
        grid_layout_->setRowStretch(row, 0);
    }
    clearLayout(grid_layout_);

    QList<QLabel *> valueLabelsWithUnits;
    QList<QLabel *> unitLabels;
    QList<QLabel *> fieldLabels;
    int fieldWidth = 0;
    int valueWidth = 0;
    int unitWidth = 0;
    int outputRow = 0;

    const QStringList lines = plainText.split(QLatin1Char('\n'));
    for (int row = 0; row < lines.size(); ++row)
    {
        const RecordingStatusLine line = parseRecordingStatusLine(lines.at(row), row);
        if (line.fullWidth)
        {
            auto *label = createRecordingStatusLabel(
                this,
                row == 0 ? QStringLiteral("recordingStatusStateLabel")
                         : QStringLiteral("recordingStatusSectionLabel"),
                line.label.isEmpty() ? QStringLiteral(" ") : line.label,
                Qt::AlignLeft);
            label->setProperty("recordingStatusSection", line.section);
            grid_layout_->addWidget(label, outputRow, 0, 1, 3);
            grid_layout_->setRowMinimumHeight(outputRow, label->minimumHeight() + (line.section ? 2 : 0));
            ++outputRow;
            continue;
        }

        auto *nameLabel = createRecordingStatusLabel(
            this,
            QStringLiteral("recordingStatusFieldLabel"),
            line.label,
            Qt::AlignLeft);
        fieldWidth = std::max(fieldWidth, nameLabel->fontMetrics().horizontalAdvance(line.label));
        fieldLabels.append(nameLabel);
        grid_layout_->addWidget(nameLabel, outputRow, 0);

        auto *valueLabel = createRecordingStatusLabel(
            this,
            QStringLiteral("recordingStatusValueLabel"),
            line.value,
            Qt::AlignRight);
        valueLabel->setFont(VaporView::Ground::MainSupport::numericFontFrom(valueLabel->font()));
        valueLabel->ensurePolished();
        valueLabel->setMinimumHeight(valueLabel->fontMetrics().height() + 2);

        if (line.unit.isEmpty())
        {
            valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            grid_layout_->addWidget(valueLabel, outputRow, 1, 1, 2);
        }
        else
        {
            auto *unitLabel = createRecordingStatusLabel(
                this,
                QStringLiteral("recordingStatusUnitLabel"),
                line.unit,
                Qt::AlignRight);
            unitLabel->ensurePolished();
            valueWidth = std::max(valueWidth, valueLabel->fontMetrics().horizontalAdvance(line.value));
            unitWidth = std::max(unitWidth, unitLabel->fontMetrics().horizontalAdvance(line.unit));
            valueLabelsWithUnits.append(valueLabel);
            unitLabels.append(unitLabel);
            grid_layout_->addWidget(valueLabel, outputRow, 1, Qt::AlignRight | Qt::AlignVCenter);
            grid_layout_->addWidget(unitLabel, outputRow, 2, Qt::AlignRight | Qt::AlignVCenter);
        }

        grid_layout_->setRowMinimumHeight(outputRow, valueLabel->minimumHeight());
        ++outputRow;
    }

    fieldWidth += 2;
    valueWidth += 4;
    unitWidth += 6;
    grid_layout_->setColumnMinimumWidth(0, fieldWidth);
    grid_layout_->setColumnMinimumWidth(1, valueWidth);
    grid_layout_->setColumnMinimumWidth(2, unitWidth);
    for (QLabel *label : std::as_const(fieldLabels))
    {
        label->setMinimumWidth(fieldWidth);
    }
    for (QLabel *label : std::as_const(valueLabelsWithUnits))
    {
        label->setMinimumWidth(valueWidth);
        label->setMaximumWidth(valueWidth);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    for (QLabel *label : std::as_const(unitLabels))
    {
        label->setMinimumWidth(unitWidth);
        label->setMaximumWidth(unitWidth);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
}

QString RecordingStatusView::statusText() const
{
    return status_text_;
}

}  // namespace VaporView::Ground::Main
