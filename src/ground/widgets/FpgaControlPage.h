#pragma once

#include "FpgaControlConfig.h"
#include "FpgaSensorDecoder.h"
#include "FpgaWaveformAssembler.h"
#include "SensorTransportPath.h"
#include <QMap>
#include <QElapsedTimer>
#include <QSet>
#include <QWidget>
#include <QJsonObject>
#include <functional>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
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
    void setUsbDiscoveryHost(bool remote, bool available = true);
    void setUsbDiscoveryResult(const QJsonObject &result);
    quint64 usbDiscoveryGeneration() const { return usbDiscoveryGeneration_; }
    void setHardwareValues(const QMap<quint32, quint32> &values);
    void updateSensor(const VaporView::FpgaSensor::Reading &reading);
    void updateWaveform(const VaporView::FpgaWave::CompletedStream &stream);
    void appendDiagnostic(const QString &text);
    void setLanguage(bool english);
    void setTheme(bool dark, int fontScalePercent = 100);
    void setRecordingState(bool active, const QString &detail = {});
    void setSensorTransportPath(VaporView::SensorTransportPath path);
    void simulateOperation(const QJsonObject &request);
    void setUiTestState(bool enabled, bool dataStalled = false,
                        bool partialFailure = false, qint64 elapsedMs = 0,
                        VaporView::SensorTransportPath path = VaporView::SensorTransportPath::DirectDevices);
signals:
    void discoverDevicesRequested();
    void connectRequested(const QString &locator, const QString &backend);
    void disconnectRequested();
    void applyRequested(const FpgaControlConfig &config);
    void applyWmsRequested(const FpgaControlConfig &config);
    void applyDliaRequested(const FpgaControlConfig &config);
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
    void showEvent(QShowEvent *event) override;
    void requestUsbDiscovery();
    void rebuildUsbDevices();
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
    struct Simulation {
        bool connected=true, acquiring=true, recording=true, stalled=false, partialFailure=false;
        bool missingCycle=false, dliaLatched=false;
        qint64 elapsedMs=0, rawUntilMs=0;
        quint32 cycle=0;
        double temperature=24.0;
        QSet<quint16> disabledSensors;
        std::array<bool,2> wms{{true,true}}, dac{{true,true}};
        FpgaControlConfig saved, applied;
        QJsonObject savedUsb;
        bool savedUsbRemote=false, savedUsbAvailable=true;
        VaporView::FpgaWave::Assembler assembler{4};
    } simulation_;
    FpgaControlConfig config_;
    QVector<Field> fields_;
    QVector<QWidget*> onlineControls_;
    QMap<quint32, QLabel*> hardwareLabels_;
    QMap<quint16, QLabel*> sensorLabels_;
    QMap<quint16, QWidget*> plots_, secondPlots_;
    QMap<quint16, QLabel*> firstPlotTitles_, secondPlotTitles_;
    QMap<quint16, QLabel*> waveLabels_;
    QMap<quint16, QComboBox*> waveComponents_;
    QMap<quint16, VaporView::FpgaWave::CompletedStream> latestWaves_, lastCompleteRaw_;
    QMap<quint16, bool> rawEnabled_;
    QMap<quint16, VaporView::FpgaSensor::Reading> latestReadings_;
    QSet<quint16> historicalReadings_, historicalWaves_;
    QElapsedTimer measurementClock_;
    QMap<quint16, qint64> readingTimes_, waveTimes_;
    bool rerendering_ = false;
    QComboBox *locator_ = nullptr;
    QAbstractButton *discover_ = nullptr;
    QLabel *discoveryStatus_ = nullptr;
    QJsonObject usbDiscovery_;
    quint64 usbDiscoveryGeneration_ = 0;
    bool usbRemote_ = false, usbHostAvailable_ = true, usbDiscovering_ = false;
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
    VaporView::SensorTransportPath sensorTransportPath_ = VaporView::SensorTransportPath::DirectDevices;
    bool connected_ = false, ready_ = false, busy_ = false, english_ = false, recording_ = false;
};
