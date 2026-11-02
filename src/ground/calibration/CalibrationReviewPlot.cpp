#include "CalibrationReviewPlot.h"
#include "shared/theme/AppTheme.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace VaporView::Ground::Calibration
{
CalibrationReviewPlot::CalibrationReviewPlot(QWidget *parent):QWidget(parent) {setMinimumHeight(230);setMouseTracking(true);}
void CalibrationReviewPlot::setStage(const ConcentrationStage& s,int w) {
    stage_=s;wavelength_=w;selected_.clear();bounds_=range();excluded_.clear();
    for(const auto& op:s.operations){if(op.excluded)excluded_.insert(op.recordId);else excluded_.remove(op.recordId);}update();
}
void CalibrationReviewPlot::setSelection(const QSet<QString>& ids) {selected_=ids;update();}
QRectF CalibrationReviewPlot::plotRect() const {return QRectF(rect()).adjusted(64,20,-20,-42);}
QPair<double,double> CalibrationReviewPlot::range() const
{
    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
    for(const auto& r:stage_.samples.value(wavelength_))if(std::isfinite(r.feature.peak)){lo=std::min(lo,r.feature.peak);hi=std::max(hi,r.feature.peak);}
    if(!std::isfinite(lo))return {0,1};if(lo==hi){lo-=0.5;hi+=0.5;}const double pad=(hi-lo)*0.08;return {lo-pad,hi+pad};
}
QPointF CalibrationReviewPlot::position(int i) const
{
    const auto& rows=stage_.samples[wavelength_];const auto bounds=bounds_;const auto box=plotRect();
    const auto first=rows.first().feature.hostTimeUs,last=rows.last().feature.hostTimeUs;
    const double x=last>first?double(rows[i].feature.hostTimeUs-first)/double(last-first):double(i)/std::max(1,int(rows.size())-1);
    const auto v=rows[i].feature.peak;
    return {box.left()+x*box.width(),std::isfinite(v)?box.bottom()-(v-bounds.first)/(bounds.second-bounds.first)*box.height():box.bottom()};
}
void CalibrationReviewPlot::paintEvent(QPaintEvent *)
{
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),palette().base());
    const auto box=plotRect();p.setPen(palette().mid().color());p.drawRect(box);
    p.setPen(palette().text().color());p.drawText(QRectF(0,0,width(),20),Qt::AlignCenter,QStringLiteral("%1 nm · 归一化峰值 R · 单击查看，拖动框选").arg(wavelength_));
    p.drawText(QRectF(0,height()-30,width(),25),Qt::AlignCenter,QStringLiteral("主机 UTC 时间 / 阶段周期"));
    const auto bounds=bounds_;p.drawText(2,35,QString::number(bounds.second,'g',4));p.drawText(2,int(box.bottom()),QString::number(bounds.first,'g',4));
    const auto& rows=stage_.samples[wavelength_];const int stride=std::max(1,int(rows.size())/8000);
    // Drawing may be thinned. Selection and statistics always inspect every raw peak.
    for(int i=0;i<rows.size();++i) {
        const auto& r=rows[i];const bool selected=selected_.contains(r.feature.id);
        if(i%stride && !selected)continue;
        const bool invalid=!r.feature.invalidReason().isEmpty(),excluded=excluded_.contains(r.feature.id);
        p.setPen(selected?QPen(palette().text().color(),2):Qt::NoPen);
        p.setBrush(invalid?QColor("#c15b5b"):excluded?QColor("#9b9da3"):QColor("#267fb4"));
        p.drawEllipse(position(i),selected?5:3,selected?5:3);
    }
    p.setPen(palette().text().color());p.drawText(int(box.left()+6),int(box.top()+18),QStringLiteral("蓝：有效   灰：人工排除   红：质量无效（恢复不能绕过）"));
    if(dragging_){p.setPen(QPen(QColor("#267fb4"),1,Qt::DashLine));p.setBrush(QColor(38,127,180,35));p.drawRect(QRect(start_,end_).normalized());}
}
void CalibrationReviewPlot::mousePressEvent(QMouseEvent *e){if(e->button()!=Qt::LeftButton)return;start_=end_=e->position().toPoint();dragging_=true;}
void CalibrationReviewPlot::mouseMoveEvent(QMouseEvent *e){if(dragging_){end_=e->position().toPoint();update();}}
void CalibrationReviewPlot::mouseReleaseEvent(QMouseEvent *e)
{
    if(!dragging_)return;end_=e->position().toPoint();dragging_=false;
    if(!(e->modifiers()&Qt::ControlModifier))selected_.clear();
    const auto& rows=stage_.samples[wavelength_];const auto selection=QRect(start_,end_).normalized();
    if((start_-end_).manhattanLength()<6) {
        int nearest=-1;double distance=144;
        for(int i=0;i<rows.size();++i){const auto d=position(i)-end_;const double sq=d.x()*d.x()+d.y()*d.y();if(sq<distance){distance=sq;nearest=i;}}
        if(nearest>=0)selected_.insert(rows[nearest].feature.id);
    }else for(int i=0;i<rows.size();++i)if(selection.contains(position(i).toPoint()))selected_.insert(rows[i].feature.id);
    update();if(selectionChanged)selectionChanged();
}
void CalibrationFitPlot::paintEvent(QPaintEvent *)
{
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),palette().base());
    const auto box=QRectF(rect()).adjusted(65,30,-20,-40);p.setPen(palette().mid().color());p.drawRect(box);
    p.setPen(palette().text().color());p.drawText(QRectF(0,0,width(),25),Qt::AlignCenter,QStringLiteral("%1 nm · 阶段均值 ± 标准差 / 已知浓度（等权）").arg(wavelength));
    if(!project.fits.contains(wavelength))return;const auto f=project.fits[wavelength].parameters;
    const double rx=f.rMax-f.rMin,cy=f.cMax-f.cMin;if(rx<=0 || cy<=0)return;
    const auto map=[&](double r,double c){return QPointF(box.left()+box.width()*(r-f.rMin)/rx,box.bottom()-box.height()*(c-f.cMin)/cy);};
    p.setPen(QPen(QColor("#d28c37"),2));p.drawLine(map(f.rMin,std::fma(f.a,f.rMin,f.b)),map(f.rMax,std::fma(f.a,f.rMax,f.b)));
    for(const auto& s:project.stages)if(s.status=="complete") {
        const auto st=s.statistics(wavelength);if(!st.used)continue;
        p.setPen(QPen(QColor("#267fb4"),1));p.drawLine(map(st.mean-st.standardDeviation,s.concentration),map(st.mean+st.standardDeviation,s.concentration));
        p.setBrush(QColor("#267fb4"));p.drawEllipse(map(st.mean,s.concentration),4,4);
    }
    p.setPen(palette().text().color());p.drawText(2,45,QString::number(f.cMax,'g',5));p.drawText(2,int(box.bottom()),QString::number(f.cMin,'g',5));
    p.drawText(QRectF(0,height()-28,width(),25),Qt::AlignCenter,QStringLiteral("R：%1 ～ %2；浓度单位 %3").arg(f.rMin,0,'g',6).arg(f.rMax,0,'g',6).arg(project.unit));
}
}
