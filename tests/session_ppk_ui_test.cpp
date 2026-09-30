#include "ground/session/SessionPpkWidget.h"
#include "ppk/AttitudeStore.h"
#include "EpsilonRawSatellite.h"
#include "ppk/SessionNavigationSource.h"
#include "test_ui_helpers.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTimer>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "rtklib.h"
#undef lock
#undef unlock

namespace
{
void require(bool ok, const char *reason)
{
    if (!ok)
        throw std::runtime_error(reason);
}
} // namespace
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
    try
    {
        using namespace VaporView::Ppk;
        QTemporaryDir session;
        VaporView::Ground::SessionUi::SessionPpkWidget panel;
        panel.setSessionDirectory(session.path());
        panel.resize(1180, 260);
        panel.show();
        auto *run = panel.findChild<QPushButton *>("sessionPpkRunButton");
        auto *cancel = panel.findChild<QPushButton *>("sessionPpkCancelButton");
        auto *source = panel.findChild<QComboBox *>("sessionPpkSourceCombo");
        require(run && cancel && source && !run->isEnabled(), "old Session disables PPK run");
        auto *model = qobject_cast<QStandardItemModel *>(source->model());
        require(model && !model->item(1)->isEnabled(), "old Session only enables Original");
        panel.setEnglish(true);
        require(run->text() == "Run PPK", "PPK uses Session English translations");
        panel.setEnglish(false);
        require(run->text() == QString::fromUtf8("运行 PPK"), "PPK uses Session Chinese translations");
        const QString fixtures = QStringLiteral(VAPORVIEW_SOURCE_DIR "/tests/fixtures/ppk/");
        QDir().mkpath(session.filePath("ppk/rover"));
        require(QFile::copy(fixtures + "rover.obs", session.filePath("ppk/rover/rover.obs")),
                "archive real rover fixture");
        require(PpkProcessor::importBase(session.path(), fixtures + "base.obs"), "archive base");
        require(PpkProcessor::importNavigation(session.path(), fixtures + "navigation.nav"), "archive ephemerides");
        auto config = PpkProcessor::loadConfig(session.path());
        config.roverObs = "ppk/rover/rover.obs";
        config.frequencies = 2;
        config.constellations = SYS_GPS;
        config.elevationMaskDeg = 10;
        require(PpkProcessor::saveConfig(session.path(), config), "save reproducible settings");
        obs_t obs{};
        auto nav = std::make_unique<nav_t>();
        sta_t station{};
        const auto name = QFile::encodeName(QDir::toNativeSeparators(fixtures + "rover.obs"));
        require(readrnxt(name.constData(), 1, {}, {}, 0, "", &obs, nav.get(), &station) > 0 && obs.n,
                "read fixture dates");
        const auto first = gpst2utc(obs.data[0].time), last = gpst2utc(obs.data[obs.n - 1].time);
        AttitudeStore attitudes;
        for (qint64 micros = (qint64(first.time) - 1) * 1000000; micros <= (qint64(last.time) + 1) * 1000000;
             micros += 200000)
        {
            std::vector<uint8_t> payload(102, 0);
            const uint32_t seconds = uint32_t(micros / 1000000), fraction = uint32_t(micros % 1000000);
            std::memcpy(payload.data() + 6, &seconds, 4);
            std::memcpy(payload.data() + 10, &fraction, 4);
            const auto frame = encodeFdilinkFrame(0x50, payload, 0);
            require(attitudes.appendSystemState(session.path(), micros + 10000000,
                                                QByteArray(reinterpret_cast<const char *>(frame.data()), frame.size())),
                    "record independent UTC attitude");
        }
        attitudes.close();
        freeobs(&obs);
        freenav(nav.get(), 0xFF);
        panel.setSessionDirectory(session.path());
        require(run->isEnabled(), "complete inputs enable run");
        run->click();
        require(panel.busy(), "UI dispatches processing asynchronously");
        cancel->click();
        require(VaporViewTest::processEventsUntil(5000, [&] { return !panel.busy(); }), "UI cancellation completes");
        int heartbeat = 0;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&] { ++heartbeat; });
        timer.start(1);
        run->click();
        require(panel.busy() && !source->isEnabled(), "source switching disabled during processing");
        require(VaporViewTest::processEventsUntil(15000, [&] { return !panel.busy(); }),
                "real PPK worker completes from UI");
        timer.stop();
        require(PpkProcessor::status(session.path()).completed && heartbeat > 0,
                "UI stays responsive during real RTKLIB processing");
        require(model->item(1)->isEnabled(), "completed PPK enables track selection");
        source->setCurrentIndex(1);
        require(sessionNavigationSource(session.path()) == NavigationSource::Ppk, "UI activates PPK");
        require(panel.findChild<QLabel *>("sessionPpkQuality")->text().contains("FIX"), "UI exposes FIX FLOAT quality");
        source->setCurrentIndex(0);
        require(sessionNavigationSource(session.path()) == NavigationSource::Original, "UI restores Original");
        panel.close();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
