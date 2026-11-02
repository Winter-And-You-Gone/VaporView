#pragma once
#include "CalibrationProject.h"
#include <QWidget>
#include <functional>

namespace VaporView::Ground::Calibration
{
class CalibrationReviewPlot final : public QWidget
{
public:
    explicit CalibrationReviewPlot(QWidget *parent = nullptr);
    void setStage(const ConcentrationStage&, int wavelength);
    void setSelection(const QSet<QString>&);
    QSet<QString> selection() const { return selected_; }
    std::function<void()> selectionChanged;
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    ConcentrationStage stage_;
    int wavelength_ = 816;
    QSet<QString> selected_;
    QPoint start_, end_;
    bool dragging_ = false;
    QPair<double,double> bounds_{0,1};
    QSet<QString> excluded_;
    QRectF plotRect() const;
    QPointF position(int index) const;
    QPair<double,double> range() const;
};
class CalibrationFitPlot final : public QWidget
{
public:
    CalibrationProject project;
    int wavelength = 816;
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override;
};
}
