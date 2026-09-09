#include "ground/main/RecordingStatusView.h"

#include "ground/main/GroundMainWindowSupport.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QSizePolicy>

#include <algorithm>
#include <utility>

namespace
{

constexpr int kRecordingStatusColumnSpacing = 2;

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

void setLabelTextIfChanged(QLabel *label, const QString& text)
{
    if (label && label->text() != text)
    {
        label->setText(text);
    }
}

void setRecordingStatusLabelWidth(QLabel *label, int width)
{
    if (!label)
    {
        return;
    }
    label->setMinimumWidth(width);
    label->setMaximumWidth(width);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void clearRowLayout(QHBoxLayout *layout)
{
    if (!layout)
    {
        return;
    }
    while (QLayoutItem *item = layout->takeAt(0))
    {
        delete item;
    }
}

QString recordingStatusStructureKey(const QList<RecordingStatusLine>& lines)
{
    QStringList tokens;
    tokens.reserve(lines.size() * 3);
    for (const RecordingStatusLine& line : lines)
    {
        if (line.fullWidth)
        {
            tokens << QStringLiteral("full")
                   << (line.section ? QStringLiteral("section") : QStringLiteral("plain"));
        }
        else
        {
            // Units are presentation data.  Keep them out of the structure key so
            // local/remote updates can reuse the same row widgets.
            tokens << QStringLiteral("field") << line.label;
        }
    }
    return tokens.join(QChar(0x1f));
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
    grid_layout_->setHorizontalSpacing(kRecordingStatusColumnSpacing);
    grid_layout_->setVerticalSpacing(1);
    grid_layout_->setColumnStretch(0, 0);
    grid_layout_->setColumnStretch(1, 0);
    grid_layout_->setColumnStretch(2, 0);
}

void RecordingStatusView::setStatusText(const QString& plainText)
{
    const QStringList textLines = plainText.split(QLatin1Char('\n'));
    QList<RecordingStatusLine> lines;
    lines.reserve(textLines.size());
    for (int row = 0; row < textLines.size(); ++row)
    {
        lines.append(parseRecordingStatusLine(textLines.at(row), row));
    }
    const QString structureKey = recordingStatusStructureKey(lines);

    if (status_text_ == plainText)
    {
        return;
    }

    status_text_ = plainText;

    auto applyColumnWidths = [this]() {
        int fieldWidth = 0;
        int valueWidth = 0;
        int unitWidth = 0;
        int unitlessRowWidth = 0;
        int fullLabelWidth = 0;
        for (const RowWidgets& row : std::as_const(row_widgets_))
        {
            if (row.fullLabel)
            {
                fullLabelWidth = std::max(
                    fullLabelWidth,
                    row.fullLabel->fontMetrics().horizontalAdvance(row.fullLabel->text()));
            }
            if (!row.fieldLabel || !row.valueLabel)
            {
                continue;
            }

            const int fieldTextWidth =
                row.fieldLabel->fontMetrics().horizontalAdvance(row.fieldLabel->text());
            const int valueTextWidth =
                row.valueLabel->fontMetrics().horizontalAdvance(row.valueLabel->text());
            if (row.valueSpansUnit)
            {
                // A unit-less row is laid out as one compact field/value pair.  It
                // must not widen the shared numeric columns used by rows with units.
                unitlessRowWidth = std::max(
                    unitlessRowWidth,
                    fieldTextWidth + kRecordingStatusColumnSpacing + valueTextWidth);
            }
            else if (row.unitLabel)
            {
                fieldWidth = std::max(fieldWidth, fieldTextWidth);
                valueWidth = std::max(valueWidth, valueTextWidth);
                unitWidth = std::max(
                    unitWidth,
                    row.unitLabel->fontMetrics().horizontalAdvance(row.unitLabel->text()));
            }
        }

        field_column_width_ = std::max(field_column_width_, fieldWidth);
        value_column_width_ = std::max(value_column_width_, valueWidth);
        unit_column_width_ = std::max(unit_column_width_, unitWidth);
        fieldWidth = field_column_width_;
        valueWidth = value_column_width_;
        unitWidth = unit_column_width_;
        const int unitRowWidth = fieldWidth + valueWidth + unitWidth +
                                 2 * kRecordingStatusColumnSpacing;
        const int contentMinimumWidth =
            std::max({unitRowWidth, unitlessRowWidth, fullLabelWidth});
        setMinimumWidth(std::max(minimumWidth(), contentMinimumWidth));
        grid_layout_->setColumnMinimumWidth(0, 0);
        grid_layout_->setColumnMinimumWidth(1, 0);
        grid_layout_->setColumnMinimumWidth(2, 0);
        for (const RowWidgets& row : std::as_const(row_widgets_))
        {
            if (!row.rowWidget || !row.rowLayout || !row.fieldLabel || !row.valueLabel)
            {
                continue;
            }

            clearRowLayout(row.rowLayout);
            row.rowWidget->setMinimumWidth(contentMinimumWidth);
            row.rowWidget->setMaximumWidth(QWIDGETSIZE_MAX);
            row.rowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            row.rowLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (row.valueSpansUnit)
            {
                const int fieldTextWidth = std::max(
                    1,
                    row.fieldLabel->fontMetrics().horizontalAdvance(row.fieldLabel->text()));
                const int valueTextWidth = std::max(
                    1,
                    row.valueLabel->fontMetrics().horizontalAdvance(row.valueLabel->text()));
                setRecordingStatusLabelWidth(row.fieldLabel, fieldTextWidth);
                setRecordingStatusLabelWidth(row.valueLabel, valueTextWidth);
                if (row.unitLabel)
                {
                    row.unitLabel->setVisible(false);
                }
                row.rowLayout->addWidget(row.fieldLabel, 0, Qt::AlignVCenter);
                row.rowLayout->addSpacing(kRecordingStatusColumnSpacing);
                row.rowLayout->addWidget(row.valueLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            }
            else if (row.unitLabel)
            {
                setRecordingStatusLabelWidth(row.fieldLabel, fieldWidth);
                setRecordingStatusLabelWidth(row.valueLabel, valueWidth);
                setRecordingStatusLabelWidth(row.unitLabel, unitWidth);
                row.unitLabel->setVisible(true);
                row.rowLayout->addWidget(row.fieldLabel, 0, Qt::AlignVCenter);
                row.rowLayout->addSpacing(kRecordingStatusColumnSpacing);
                row.rowLayout->addWidget(row.valueLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
                row.rowLayout->addSpacing(kRecordingStatusColumnSpacing);
                row.rowLayout->addWidget(row.unitLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            }
        }
    };

    bool compatibleRows = status_structure_key_ == structureKey && row_widgets_.size() == lines.size();
    if (compatibleRows)
    {
        for (int row = 0; row < lines.size(); ++row)
        {
            if (!lines.at(row).fullWidth &&
                (!row_widgets_.at(row).rowWidget || !row_widgets_.at(row).rowLayout ||
                 !row_widgets_.at(row).fieldLabel || !row_widgets_.at(row).valueLabel ||
                 !row_widgets_.at(row).unitLabel))
            {
                compatibleRows = false;
                break;
            }
        }
    }
    if (compatibleRows)
    {
        setUpdatesEnabled(false);
        for (int row = 0; row < lines.size(); ++row)
        {
            const RecordingStatusLine& line = lines.at(row);
            RowWidgets& widgets = row_widgets_[row];
            if (line.fullWidth)
            {
                setLabelTextIfChanged(widgets.fullLabel,
                                      line.label.isEmpty() ? QStringLiteral(" ") : line.label);
            }
            else
            {
                setLabelTextIfChanged(widgets.fieldLabel, line.label);
                setLabelTextIfChanged(widgets.valueLabel, line.value);
                setLabelTextIfChanged(widgets.unitLabel, line.unit);
                widgets.valueSpansUnit = line.unit.isEmpty();
            }
        }
        applyColumnWidths();
        grid_layout_->invalidate();
        grid_layout_->activate();
        for (const RowWidgets& row : std::as_const(row_widgets_))
        {
            if (row.valueLabel)
                row.valueLabel->setVisible(true);
            if (row.unitLabel)
                row.unitLabel->setVisible(!row.valueSpansUnit);
        }
        setUpdatesEnabled(true);
        update();
        return;
    }

    status_structure_key_ = structureKey;
    row_widgets_.clear();

    const int previousRowCount = grid_layout_->rowCount();
    for (int row = 0; row < previousRowCount; ++row)
    {
        grid_layout_->setRowMinimumHeight(row, 0);
        grid_layout_->setRowStretch(row, 0);
    }
    clearLayout(grid_layout_);

    int outputRow = 0;

    for (int row = 0; row < lines.size(); ++row)
    {
        const RecordingStatusLine line = lines.at(row);
        RowWidgets widgets;
        widgets.fullWidth = line.fullWidth;
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
            widgets.fullLabel = label;
            row_widgets_.append(widgets);
            ++outputRow;
            continue;
        }

        auto *nameLabel = createRecordingStatusLabel(
            nullptr,
            QStringLiteral("recordingStatusFieldLabel"),
            line.label,
            Qt::AlignRight);
        widgets.fieldLabel = nameLabel;

        auto *valueLabel = createRecordingStatusLabel(
            nullptr,
            QStringLiteral("recordingStatusValueLabel"),
            line.value,
            Qt::AlignRight);
        valueLabel->setFont(VaporView::Ground::MainSupport::numericFontFrom(valueLabel->font()));
        valueLabel->ensurePolished();
        valueLabel->setMinimumHeight(valueLabel->fontMetrics().height() + 2);
        widgets.valueLabel = valueLabel;

        widgets.valueSpansUnit = line.unit.isEmpty();
        auto *unitLabel = createRecordingStatusLabel(
            nullptr,
            QStringLiteral("recordingStatusUnitLabel"),
            line.unit,
            Qt::AlignRight);
        unitLabel->ensurePolished();
        widgets.unitLabel = unitLabel;

        auto *rowWidget = new QWidget(this);
        rowWidget->setObjectName(QStringLiteral("recordingStatusRow"));
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);
        nameLabel->setParent(rowWidget);
        valueLabel->setParent(rowWidget);
        unitLabel->setParent(rowWidget);
        widgets.rowWidget = rowWidget;
        widgets.rowLayout = rowLayout;
        grid_layout_->addWidget(rowWidget, outputRow, 0, 1, 3);
        unitLabel->setVisible(false);

        grid_layout_->setRowMinimumHeight(outputRow, valueLabel->minimumHeight());
        row_widgets_.append(widgets);
        ++outputRow;
    }

    applyColumnWidths();
}

QString RecordingStatusView::statusText() const
{
    return status_text_;
}

}  // namespace VaporView::Ground::Main
