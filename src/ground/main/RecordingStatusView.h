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
        QLabel *fullLabel = nullptr;
        QLabel *fieldLabel = nullptr;
        QLabel *valueLabel = nullptr;
        QLabel *unitLabel = nullptr;
    };

    QGridLayout *grid_layout_;
    QString status_text_;
    QString status_structure_key_;
    QVector<RowWidgets> row_widgets_;
};

}  // namespace VaporView::Ground::Main
