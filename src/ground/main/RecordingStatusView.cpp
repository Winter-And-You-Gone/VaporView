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

void setLabelTextIfChanged(QLabel *label, const QString& text)
{
    if (label && label->text() != text)
    {
        label->setText(text);
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
            tokens << QStringLiteral("field") << line.label;
            // A value without a unit uses the unit column as extra room.  The
            // presence of a unit therefore changes the row layout, not just text.
            tokens << (line.unit.isEmpty() ? QStringLiteral("span") : QStringLiteral("unit"));
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
    grid_layout_->setHorizontalSpacing(4);
    grid_layout_->setVerticalSpacing(1);
    grid_layout_->setColumnStretch(0, 1);
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
        for (const RowWidgets& row : std::as_const(row_widgets_))
        {
            if (row.fieldLabel)
            {
                fieldWidth = std::max(fieldWidth,
                                      row.fieldLabel->fontMetrics().horizontalAdvance(row.fieldLabel->text()));
            }
            if (row.valueLabel)
            {
                if (!row.valueSpansUnit && row.unitLabel)
                {
                    valueWidth = std::max(valueWidth,
                                          row.valueLabel->fontMetrics().horizontalAdvance(row.valueLabel->text()));
                    unitWidth = std::max(unitWidth,
                                         row.unitLabel->fontMetrics().horizontalAdvance(row.unitLabel->text()));
                }
            }
        }

        field_column_width_ = std::max(field_column_width_, fieldWidth + 2);
        // Keep enough room for the common unit-less state text (for example,
        // "仅远程有效") when it spans the value and unit columns.
        value_column_width_ = std::max(value_column_width_, std::max(40, valueWidth + 4));
        unit_column_width_ = std::max(unit_column_width_, std::max(20, unitWidth + 6));
        fieldWidth = field_column_width_;
        valueWidth = value_column_width_;
        unitWidth = unit_column_width_;
        grid_layout_->setColumnMinimumWidth(0, fieldWidth);
        grid_layout_->setColumnMinimumWidth(1, valueWidth);
        grid_layout_->setColumnMinimumWidth(2, unitWidth);
        for (const RowWidgets& row : std::as_const(row_widgets_))
        {
            if (row.fieldLabel)
            {
                row.fieldLabel->setMinimumWidth(fieldWidth);
            }
            if (row.valueLabel)
            {
                if (row.valueSpansUnit)
                {
                    row.valueLabel->setMinimumWidth(0);
                    row.valueLabel->setMaximumWidth(QWIDGETSIZE_MAX);
                    row.valueLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
                }
                else if (row.unitLabel)
                {
                    row.valueLabel->setMinimumWidth(valueWidth);
                    row.valueLabel->setMaximumWidth(valueWidth);
                    row.valueLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                    row.unitLabel->setMinimumWidth(unitWidth);
                    row.unitLabel->setMaximumWidth(unitWidth);
                    row.unitLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                    row.unitLabel->setVisible(true);
                }
            }
        }
    };

    bool compatibleRows = status_structure_key_ == structureKey && row_widgets_.size() == lines.size();
    if (compatibleRows)
    {
        for (int row = 0; row < lines.size(); ++row)
        {
            if (!lines.at(row).fullWidth &&
                (!row_widgets_.at(row).fieldLabel || !row_widgets_.at(row).valueLabel ||
                 lines.at(row).unit.isEmpty() != row_widgets_.at(row).valueSpansUnit ||
                 (!lines.at(row).unit.isEmpty() && !row_widgets_.at(row).unitLabel)))
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
            const RowWidgets& widgets = row_widgets_.at(row);
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
                row.unitLabel->setVisible(true);
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
            this,
            QStringLiteral("recordingStatusFieldLabel"),
            line.label,
            Qt::AlignLeft);
        widgets.fieldLabel = nameLabel;
        grid_layout_->addWidget(nameLabel, outputRow, 0);

        auto *valueLabel = createRecordingStatusLabel(
            this,
            QStringLiteral("recordingStatusValueLabel"),
            line.value,
            Qt::AlignRight);
        valueLabel->setFont(VaporView::Ground::MainSupport::numericFontFrom(valueLabel->font()));
        valueLabel->ensurePolished();
        valueLabel->setMinimumHeight(valueLabel->fontMetrics().height() + 2);
        widgets.valueLabel = valueLabel;

        widgets.valueSpansUnit = line.unit.isEmpty();
        if (widgets.valueSpansUnit)
        {
            grid_layout_->addWidget(valueLabel, outputRow, 1, 1, 2,
                                    Qt::AlignRight | Qt::AlignVCenter);
        }
        else
        {
            auto *unitLabel = createRecordingStatusLabel(
                this,
                QStringLiteral("recordingStatusUnitLabel"),
                line.unit,
                Qt::AlignRight);
            unitLabel->ensurePolished();
            widgets.unitLabel = unitLabel;
            grid_layout_->addWidget(valueLabel, outputRow, 1, Qt::AlignRight | Qt::AlignVCenter);
            grid_layout_->addWidget(unitLabel, outputRow, 2, Qt::AlignRight | Qt::AlignVCenter);
        }

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
