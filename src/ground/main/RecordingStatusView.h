#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;

namespace VaporView::Ground::Main
{

class RecordingStatusView final : public QWidget
{
public:
    explicit RecordingStatusView(QWidget *parent = nullptr);

    void setStatusText(const QString& plainText);
    QString statusText() const;

private:
    struct RowWidgets
    {
        bool fullWidth = false;
        bool valueSpansUnit = false;
        QLabel *fullLabel = nullptr;
        QLabel *fieldLabel = nullptr;
        QLabel *valueLabel = nullptr;
        QLabel *unitLabel = nullptr;
    };

    QGridLayout *grid_layout_;
    QString status_text_;
    QString status_structure_key_;
    QVector<RowWidgets> row_widgets_;
    int field_column_width_ = 0;
    int value_column_width_ = 0;
    int unit_column_width_ = 0;
};

}  // namespace VaporView::Ground::Main
