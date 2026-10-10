#pragma once

#include "FpgaControlConfig.h"
#include "FpgaSensorDecoder.h"
#include "FpgaWaveformAssembler.h"
#include <QMap>
#include <QElapsedTimer>
#include <QSet>
#include <QWidget>
#include <functional>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;

class FpgaControlPage final : public QWidget
{
    Q_OBJECT
public:
    explicit FpgaControlPage(QWidget *parent = nullptr);
    FpgaControlConfig configuration() const;
    void setConfiguration(const FpgaControlConfig &config);
    void setConnectionState(bool ready, bool busy, const QString &detail = {});
    void setTransportConnected(bool connected);
    void setHardwareValues(const QMap<quint32, quint32> &values);
    void updateSensor(const VaporView::FpgaSensor::Reading &reading);
    void updateWaveform(const VaporView::FpgaWave::CompletedStream &stream);
    void appendDiagnostic(const QString &text);
    void setLanguage(bool english);
    void setTheme(bool dark, int fontScalePercent = 100);
    void setRecordingState(bool active, const QString &detail = {});
signals:
    void connectRequested(const QString &locator, const QString &backend);
    void disconnectRequested();
    void applyRequested(const FpgaControlConfig &config);
    void configurationChanged(const FpgaControlConfig &config);
    void refreshRequested();
    void acquisitionRequested(bool enable);
    void waveformRequested(int channel, bool enable);
    void dacRequested(int channel, bool enable);
    void sensorEnableRequested(quint16 source, bool enable);
    void rawRequested(bool enable);
    void setTemperatureRequested(double celsius);
    void replayRequested(const QString &path);
    void exportRequested(const QString &path);
    void recordingRequested(bool enable);
    void pressureSourceChanged(quint16 source);
private:
    struct Field {
        QDoubleSpinBox *input = nullptr;
        std::function<double(const FpgaControlConfig&)> read;
        std::function<void(FpgaControlConfig&, double)> write;
    };
    QString text(const char *zh, const char *en) const;
    void updateActions();
    void lockOperation();
    void refreshMeasurements();
    void label(QWidget *widget, const QString &zh, const QString &en);
    FpgaControlConfig config_;
    QVector<Field> fields_;
    QVector<QWidget*> onlineControls_;
    QMap<quint32, QLabel*> hardwareLabels_;
    QMap<quint16, QLabel*> sensorLabels_;
    QMap<quint16, QWidget*> plots_;
    QMap<quint16, QLabel*> waveLabels_;
    QMap<quint16, QComboBox*> waveComponents_;
    QMap<quint16, VaporView::FpgaWave::CompletedStream> latestWaves_;
    QMap<quint16, VaporView::FpgaSensor::Reading> latestReadings_;
    QSet<quint16> historicalReadings_, historicalWaves_;
    QElapsedTimer measurementClock_;
    QMap<quint16, qint64> readingTimes_, waveTimes_;
    bool rerendering_ = false;
    QLineEdit *locator_ = nullptr;
    QAbstractButton *connect_ = nullptr, *disconnect_ = nullptr;
    QAbstractButton *refresh_ = nullptr;
    QAbstractButton *record_ = nullptr;
    QLabel *status_ = nullptr, *recordStatus_ = nullptr;
    QPlainTextEdit *diagnostics_ = nullptr;
    QComboBox *pressure_ = nullptr;
    QComboBox *backend_ = nullptr;
    QCheckBox *recordSensors_ = nullptr, *recordDlia_ = nullptr, *recordRaw_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QString connectionDetail_, recordingDetail_;
    bool connected_ = false, ready_ = false, busy_ = false, english_ = false, recording_ = false;
};
