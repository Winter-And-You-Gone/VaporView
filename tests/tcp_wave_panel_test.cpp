#include "ground/wave/TcpWavePanel.h"

#include <QApplication>
#include <QByteArray>
#include <QSettings>
#include <QTemporaryDir>
#include <QVector>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void testInvalidTcpStreamDoesNotGrowBacklog()
{
    TcpWavePanel panel;
    panel.setEnglish(true);

    const QByteArray invalidChunk(8192, static_cast<char>(0x7f));
    for (int i = 0; i < 1024; ++i)
    {
        panel.testFeedSocketBytes(invalidChunk);
    }

    require(panel.testBufferedByteCount() <= 3, "invalid TCP stream backlog should stay bounded");
}

void testWavePlotXAxisLabelsDefaultToChinese()
{
    TcpWavePanel panel;

    const QVector<float> samples(512, 0.25f);
    panel.injectRemoteRawSignalFrame(1, samples);
    panel.injectRemoteSecondHarmonicFrame(2, samples);
    for (int frame = 0; frame < 11; ++frame)
    {
        VaporView::WaveformFeature feature;
        feature.host_time_us = static_cast<quint64>(frame + 1) * 1000ULL;
        feature.peak = 0.5f + static_cast<float>(frame) * 0.01f;
        feature.rms = 0.1f;
        feature.quality_flags = 0;
        panel.injectRemoteWaveformFeature(feature);
    }
    panel.testFlushLiveDisplay();

    require(panel.testRawXAxisLabel() == QStringLiteral("512 点"),
            "raw waveform x-axis defaults samples to Chinese");
    require(panel.testHarmonicXAxisLabel() == QStringLiteral("512 点"),
            "harmonic waveform x-axis defaults samples to Chinese");
    require(panel.testPeakXAxisLabel() == QStringLiteral("11 帧"),
            "peak trend x-axis defaults frames to Chinese");
    require(panel.testWavePlotBottomMarginExtra() >= 8 &&
                panel.testPeakPlotBottomMarginExtra() >= 8,
            "waveform plots reserve extra bottom margin for full x-axis labels");

    panel.setEnglish(true);
    require(panel.testRawXAxisLabel() == QStringLiteral("512 samples"),
            "raw waveform x-axis still supports English samples");
    require(panel.testPeakXAxisLabel() == QStringLiteral("11 frames"),
            "peak trend x-axis still supports English frames");
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewTcpWavePanelTest"));
    app.setApplicationName(QStringLiteral("tcp_wave_panel_test"));

    testInvalidTcpStreamDoesNotGrowBacklog();
    testWavePlotXAxisLabelsDefaultToChinese();
    std::cout << "tcp_wave_panel_test passed\n";
    return 0;
}
