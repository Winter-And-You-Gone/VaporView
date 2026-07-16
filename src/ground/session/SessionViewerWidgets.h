#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

namespace VaporView::Ground::SessionUi
{

struct SessionTableTheme
{
    QColor background;
    QColor text;
    QColor grid;
    QColor headerBackground;
    QColor headerText;
    QColor selectedBackground;
    QColor selectedText;
    QColor highlightedBackground;
    QColor highlightedText;
    QColor secondaryHighlightedBackground;
    QColor secondaryHighlightedText;
};

QFont numericFontFrom(const QFont& base);
SessionTableTheme sessionTableThemeFor(const QWidget *widget);

class SessionCsvTableModel : public QAbstractTableModel
{
public:
    virtual void setRows(const QStringList& headers, QVector<QStringList>&& rows) = 0;
    virtual void setHeaders(const QStringList& headers) = 0;
    virtual void clear() = 0;
    virtual void setTheme(const SessionTableTheme& theme) = 0;
    virtual void setHighlightedRows(const QVector<int>& rows,
                                    int primaryRow,
                                    const QHash<int, QString>& deltas) = 0;

protected:
    using QAbstractTableModel::QAbstractTableModel;
};

class SessionWavePlotWidget : public QWidget
{
public:
    virtual void setSamples(const QVector<float>& samples, int firstSampleIndex = 0) = 0;

protected:
    using QWidget::QWidget;
};

class SessionPeakPlotWidget : public QWidget
{
public:
    enum class PlotMode
    {
        Scatter,
        Polyline
    };

    virtual void setEnglish(bool english) = 0;
    virtual void setPeakValues(const QVector<float>& values) = 0;
    virtual void setCurrentFrame(int frameIndex) = 0;
    virtual void setPlotMode(PlotMode mode) = 0;
    virtual void setViewRange(int startIndex, int count) = 0;
    virtual void setViewChangedCallback(std::function<void(int, int, int)> callback) = 0;

protected:
    using QWidget::QWidget;
};

class SingleSeriesTrendPlotWidget : public QWidget
{
public:
    enum class PlotMode
    {
        Scatter,
        Polyline
    };

    virtual void setValues(const QVector<double>& values) = 0;
    virtual void setCurrentIndex(int index) = 0;
    virtual void setPlotMode(PlotMode mode) = 0;
    virtual void setViewRange(int startIndex, int count) = 0;

protected:
    using QWidget::QWidget;
};

SessionCsvTableModel *createSessionCsvTableModel(QObject *parent = nullptr);
SessionWavePlotWidget *createSessionWavePlotWidget(QWidget *parent = nullptr);
SessionPeakPlotWidget *createSessionPeakPlotWidget(QWidget *parent = nullptr);
SingleSeriesTrendPlotWidget *createSingleSeriesTrendPlotWidget(
    const QColor& color,
    const QString& emptyText,
    const QString& unit = QString(),
    QWidget *parent = nullptr);

} // namespace VaporView::Ground::SessionUi
