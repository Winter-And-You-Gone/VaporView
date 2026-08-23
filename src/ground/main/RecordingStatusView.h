#pragma once

#include <QString>
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
    QGridLayout *grid_layout_;
    QString status_text_;
};

}  // namespace VaporView::Ground::Main
