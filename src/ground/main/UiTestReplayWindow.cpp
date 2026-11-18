#include "ground/main/UiTestReplayWindow.h"
#include "ground/session/SessionCsv.h"
#include "ground/session/SessionLoader.h"
#include "ground/session/SessionWaveformRepository.h"
#include "shared/session/SessionPathResolver.h"

#include "shared/session/HarmonicWaveform.h"
#include "shared/session/FpgaSessionArchive.h"
#include "FpgaTelemetry.h"
#include <QTemporaryDir>
#include <QtConcurrentRun>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>

using namespace VaporView::Ground;
using namespace VaporView::Session;
namespace Raw = VaporView::SessionRawDat;

struct UiTestReplayWindow::Index
{
    struct Entry {
        quint64 timestamp;
        qint64 offset;
        quint32 length;
        Raw::RawRecordHeader header{};
    };
    struct Track {
        Stream type;
        QString path;
        QStringList columns;
        QVector<Entry> entries;
        int current = -1;
        QString description;
        std::shared_ptr<QFile> file;
    };
    struct Event { quint64 timestamp; int track, row; };
    std::shared_ptr<QTemporaryDir> archiveCache;
    QVector<Track> tracks;
    QVector<Event> events;
    SessionWaveformCatalog legacy;
    QString name, error;

    void add(Track track)
    {
        if (track.entries.isEmpty()) return;
        std::stable_sort(track.entries.begin(), track.entries.end(),
            [](const Entry& a, const Entry& b) { return a.timestamp < b.timestamp; });
        const int stream = int(tracks.size());
        for (int i = 0; i < track.entries.size(); ++i)
            events.append({track.entries[i].timestamp, stream, i});
        tracks.append(std::move(track));
    }

    void csv(Stream type, const QString& path, const std::atomic_bool& cancelled)
    {
        QFile file(path);
        if (!file.exists()) return;
        if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return; }
        Track track{type, path, SessionCsv::parseCsvLine(QString::fromUtf8(file.readLine()).trimmed())};
        const int time = SessionCsv::findHeaderIndex(track.columns,
            {"record_timestamp_us", "host_time_us", "timestamp_us", "epsilon_host_timestamp_us", "rtk_timestamp_us"});
        while (!file.atEnd() && !cancelled) {
            const qint64 offset = file.pos();
            QByteArray line = file.readLine();
            while (line.count('"') % 2 && !file.atEnd() && !cancelled) line += file.readLine();
            if (line.trimmed().isEmpty()) continue;
            bool ok = false;
            const auto timestamp = SessionCsv::parseCsvLine(QString::fromUtf8(line).trimmed()).value(time).toULongLong(&ok);
            if (!ok) { error = QStringLiteral("%1 @ %2: invalid timestamp").arg(path).arg(offset); return; }
            track.entries.append({timestamp, offset, quint32(line.size())});
        }
        add(std::move(track));
    }

    void raw(const QString& path, quint16 source, const std::atomic_bool& cancelled)
    {
        QFile file(path);
        if (!file.exists()) return;
        if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return; }
        Raw::RawScanOptions options;
        options.expectedSourceId = source;
        options.isCancelled = [&] { return cancelled.load(); };
        const auto scan = Raw::scan(file, options);
        if (!scan.success()) { error = path + ": " + scan.error; return; }
        // Navigation packet types and DLIA channels update independently.
        QMap<int, Track> channels;
        for (const auto& record : scan.records) {
            int channel = source == Raw::kSourceNavigation ? record.header.recordType : 0;
            if (source == Raw::kSourceWaveform && record.header.recordType == Raw::kRecordTypeHarmonicPair) {
                HarmonicLayout layout;
                if (!file.seek(record.payloadOffset) || !parseHarmonicLayout(file.read(kHarmonicHeaderSize),
                        record.header.payloadSize, layout)) { error = path + ": invalid harmonic header"; return; }
                channel = layout.channel;
            }
            if (cancelled) return;
            auto& track = channels[channel];
            if (track.entries.isEmpty()) {
                track.type = Stream::Raw;
                track.path = path;
                if (channel)
                    track.columns = {QStringLiteral("%1 · 0x%2").arg(QFileInfo(path).fileName()).arg(channel, 2, 16, QLatin1Char('0'))};
            }
            track.entries.append({record.header.hostTimestampUs, qint64(record.payloadOffset),
                                  record.header.payloadSize, record.header});
        }
        for (auto& track : channels) add(std::move(track));
    }

    void indexFpga(const QString& directory, const std::atomic_bool& cancelled)
    {
        const QString path = FpgaSessionArchive::rawPath(directory);
        if (!QFileInfo::exists(path) || cancelled) return;
        archiveCache = std::make_shared<QTemporaryDir>();
        QFile cache(archiveCache->filePath("frames"));
        if (!archiveCache->isValid() || !cache.open(QIODevice::WriteOnly)) {
            error = QStringLiteral("Cannot create FPGA replay cache");
            return;
        }
        QMap<int, Track> tracksBySource;
        VaporView::FpgaWave::Assembler assembler;
        const bool ok = FpgaSessionArchive::replay(directory, [&](quint64 host, const auto& frame) {
            const auto& h = frame.header;
            if (h.frameType != VaporView::FpgaVlp1::FrameType::Data) return true;
            const QByteArray payload(reinterpret_cast<const char*>(frame.payload.data()), frame.payload.size());
            QByteArray bytes;
            Stream type;
            if (h.message == 0x1000 || h.message == 0x1001) {
                auto wave = assembler.accept({h.source, h.message, h.flags, h.cycleId, h.timestamp, payload});
                assembler.takeExpired();
                if (!wave) return true;
                type = Stream::FpgaWave;
                bytes = VaporView::FpgaRemote::encodeWaveformPreview(*wave);
            } else {
                type = Stream::FpgaSensor;
                const auto reading = VaporView::FpgaSensor::FpgaSensorDecoder::decode(payload, h.source, h.message, h.flags, h.timestamp);
                bytes = VaporView::FpgaRemote::encodeSensor(reading, host);
            }
            if (bytes.isEmpty()) return true;
            auto& track = tracksBySource[h.source];
            track.type = type;
            track.path = cache.fileName();
            track.columns = {QStringLiteral("FPGA 0x%1").arg(h.source, 2, 16, QLatin1Char('0'))};
            track.entries.append({host, cache.pos(), quint32(bytes.size())});
            return cache.write(bytes) == bytes.size();
        }, &error, [&](quint64, const QJsonObject&) { assembler.clear(); }, [&] { return cancelled.load(); });
        if (!ok || !cache.flush()) {
            if (error.isEmpty()) error = QStringLiteral("Failed to index FPGA archive");
            return;
        }
        for (auto& track : tracksBySource) add(std::move(track));
    }

    void load(const QString& path, const std::atomic_bool& cancelled)
    {
        const auto metadata = SessionLoader::loadMetadata(path);
        if (!metadata.success) { error = metadata.error; return; }
        const auto& m = metadata.metadata;
        name = m.sessionName;
        const auto context = loadSessionPathContext(m.sessionDirectory);
        const auto resolved = [&](SessionFileKind kind) { return resolveSessionPath(context, kind).absolutePath; };
        const auto& layout = standardSessionPackageLayout();
        csv(Stream::Sensors, m.sensorSummaryCsvFilename, cancelled);
        csv(Stream::Laser, resolved(SessionFileKind::LaserTemperatureControllerCsv), cancelled);
        csv(Stream::System, resolved(SessionFileKind::SystemTemperatureControllerCsv), cancelled);
        csv(Stream::Feature, resolved(SessionFileKind::WaveformFeaturesCsv), cancelled);
        csv(Stream::Peak, m.waveformPeaksCsvFilename, cancelled);
        const QDir dir(m.sessionDirectory);
        csv(Stream::NavigationStatus, dir.filePath(layout.navigationStatusCsvPath), cancelled);
        csv(Stream::Log, dir.filePath(layout.eventLogPath), cancelled);
        for (const auto& def : standardRawFileDefinitions()) {
            if (cancelled || !error.isEmpty()) return;
            raw(resolved(def.kind), def.sourceId, cancelled);
        }
        raw(dir.filePath("raw/fpga_vlp1.dat"), Raw::kSourceFpga, cancelled);
        indexFpga(m.sessionDirectory, cancelled);
        if (!archiveCache && !QFileInfo::exists(m.waveformRawFilename) && !cancelled) {
            auto result = SessionWaveformRepository::loadCatalog(m);
            if (!result.success) { error = result.error; return; }
            legacy = std::move(result.catalog);
            Track track{Stream::LegacyWave, m.waveformDirectory};
            for (int i = 0; i < legacy.frameCount && !cancelled; ++i) {
                const auto frame = SessionWaveformRepository::readFrame(legacy, i);
                if (!frame.success) { error = frame.error; return; }
                track.entries.append({frame.timestampUs, i, 0});
            }
            add(std::move(track));
        }
        std::stable_sort(events.begin(), events.end(),
            [](const Event& a, const Event& b) { return a.timestamp < b.timestamp; });
    }
};

UiTestReplayWindow::UiTestReplayWindow(QWidget *parent, bool english, const QString& directory)
    : QWidget(parent, Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
      english_(english), directory_(directory)
{
    setObjectName("uiTestReplayWindow");
    setWindowTitle(label("界面测试 · 数据源", "UI test data source"));
    auto *layout = new QVBoxLayout(this);
    auto *buttons = new QHBoxLayout;
    simulate_ = new QPushButton(label("模拟数据", "Simulated data"), this);
    simulate_->setObjectName("replaySimulate");
    choose_ = new QPushButton(label("打开 Session…", "Open Session…"), this);
    buttons->addWidget(simulate_);
    buttons->addWidget(choose_);
    layout->addLayout(buttons);
    source_ = new QLabel(this);
    source_->setObjectName("replaySource");
    source_->setTextFormat(Qt::PlainText);
    source_->setWordWrap(true);
    layout->addWidget(source_);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 1000000);
    layout->addWidget(slider_);
    auto *transport = new QHBoxLayout;
    play_ = new QPushButton(this);
    play_->setObjectName("replayPlay");
    time_ = new QLabel(this);
    transport->addWidget(play_);
    transport->addStretch();
    transport->addWidget(time_);
    layout->addLayout(transport);
    streams_ = new QComboBox(this);
    detail_ = new QLabel(this);
    detail_->setTextFormat(Qt::PlainText);
    detail_->setWordWrap(true);
    layout->addWidget(streams_);
    layout->addWidget(detail_);
    connect(streams_, &QComboBox::currentIndexChanged, this, [this] { updatePosition(); });
    connect(simulate_, &QPushButton::clicked, this, &UiTestReplayWindow::selectSimulation);
    connect(choose_, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, label("选择 Session", "Select Session"),
                                                       directory_, "Session (session.json)");
        if (!file.isEmpty()) loadSession(file);
    });
    connect(play_, &QPushButton::clicked, this, [this] {
        if (!timer_.isActive() && position_ == index_->events.last().timestamp) seek(index_->events.first().timestamp);
        setPlaying(!timer_.isActive());
    });
    connect(slider_, &QSlider::sliderPressed, this, [this] {
        resumeAfterDrag_ = timer_.isActive();
        setPlaying(false);
    });
    connect(slider_, &QSlider::sliderReleased, this, [this] { setPlaying(resumeAfterDrag_); });
    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        if (!index_) return;
        const auto first = index_->events.first().timestamp;
        const auto span = index_->events.last().timestamp - first;
        seek(first + quint64(double(span) * value / slider_->maximum()));
    });
    timer_.setInterval(20);
    connect(&timer_, &QTimer::timeout, this, &UiTestReplayWindow::tick);
    selectSimulation();
    setMinimumWidth(320);
}

QString UiTestReplayWindow::label(const char *zh, const char *en) const { return QString::fromUtf8(english_ ? en : zh); }
UiTestReplayWindow::~UiTestReplayWindow() { if (cancel_) cancel_->store(true); }
void UiTestReplayWindow::closeEvent(QCloseEvent *event)
{
    setPlaying(false);
    if (cancel_) cancel_->store(true);
    event->accept();
    // Mode teardown destroys this window after the native close event returns.
    if (exitMode) QTimer::singleShot(0, this, exitMode);
}

void UiTestReplayWindow::selectSimulation()
{
    if (cancel_) cancel_->store(true);
    setPlaying(false);
    index_.reset();
    streams_->clear();
    streams_->hide();
    detail_->hide();
    slider_->setValue(0);
    slider_->setEnabled(false);
    play_->setEnabled(false);
    simulate_->setEnabled(false);
    time_->clear();
    source_->setToolTip({});
    source_->setText(label("当前使用模拟数据", "Using simulated data"));
    if (resetSource) resetSource(false);
    adjustSize();
}

void UiTestReplayWindow::loadSession(const QString& path)
{
    if (cancel_) cancel_->store(true);
    auto token = cancel_ = std::make_shared<std::atomic_bool>(false);
    simulate_->setEnabled(true);
    source_->setText(label("正在读取 ", "Loading ") + QFileInfo(path).dir().dirName());
    auto *watch = new QFutureWatcher<std::shared_ptr<Index>>(this);
    connect(watch, &QFutureWatcherBase::finished, this, [this, watch, token, path] {
        watch->deleteLater();
        if (token->load()) return;
        auto loaded = watch->result();
        if (!loaded->error.isEmpty() || loaded->events.isEmpty()) {
            source_->setText((index_ ? index_->name : label("模拟数据", "Simulated data")) + "\n" +
                (loaded->error.isEmpty() ? label("没有可回放记录", "No playable records") : loaded->error));
            simulate_->setEnabled(bool(index_));
            return;
        }
        setPlaying(false);
        index_ = std::move(loaded);
        directory_ = QFileInfo(path).absolutePath();
        source_->setText(index_->name);
        source_->setToolTip(path);
        {
            const QSignalBlocker block(streams_);
            streams_->clear();
            for (const auto& track : index_->tracks) {
                const bool fpga = track.type == Stream::FpgaWave || track.type == Stream::FpgaSensor;
                streams_->addItem((fpga || track.type == Stream::Raw) && !track.columns.isEmpty()
                    ? track.columns.first() : QFileInfo(track.path).fileName());
            }
        }
        streams_->show();
        detail_->show();
        slider_->setEnabled(true);
        play_->setEnabled(true);
        seek(index_->events.first().timestamp);
        setPlaying(true);
    });
    watch->setFuture(QtConcurrent::run([token, path] {
        auto result = std::make_shared<Index>();
        result->load(path, *token);
        return result;
    }));
}

void UiTestReplayWindow::seek(quint64 timestamp)
{
    resetSource(true);
    position_ = anchor_ = timestamp;
    clock_.restart();
    const auto& events = index_->events;
    if (timestamp == events.first().timestamp) {
        for (auto& track : index_->tracks) {
            track.current = -1;
            track.description.clear();
        }
        next_ = 0;
        // Even coincident samples of the same stream must play once from the beginning.
        tick();
        return;
    }
    next_ = std::upper_bound(events.begin(), events.end(), timestamp,
        [](quint64 t, const auto& e) { return t < e.timestamp; }) - events.begin();
    QVector<Index::Event> latest;
    for (int i = 0; i < index_->tracks.size(); ++i) {
        auto& track = index_->tracks[i];
        track.current = -1;
        track.description.clear();
        const auto end = std::upper_bound(track.entries.begin(), track.entries.end(), timestamp,
            [](quint64 t, const auto& e) { return t < e.timestamp; });
        if (end != track.entries.begin()) latest.append({(end - 1)->timestamp, i, int(end - track.entries.begin() - 1)});
    }
    std::stable_sort(latest.begin(), latest.end(), [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
    for (const auto& event : latest)
        if (!deliver(event.track, event.row)) return;
    updatePosition();
}

bool UiTestReplayWindow::deliver(int stream, int row)
{
    auto& track = index_->tracks[stream];
    const auto& e = track.entries[row];
    Sample sample{track.type, e.timestamp, {}, e.header};
    QString description;
    if (track.type == Stream::LegacyWave) {
        auto frame = SessionWaveformRepository::readFrame(index_->legacy, e.offset);
        if (!frame.success) { source_->setText(frame.error); setPlaying(false); return false; }
        sample.legacyWave = std::move(frame.samples);
    } else {
        if (!track.file) track.file = std::make_shared<QFile>(track.path);
        auto& file = *track.file;
        const auto length = e.length;
        if ((!file.isOpen() && !file.open(QIODevice::ReadOnly)) || !file.seek(e.offset) || (sample.bytes = file.read(length)).size() != length) {
            source_->setText(label("读取失败 · ", "Read failed · ") + track.path);
            setPlaying(false);
            return false;
        }
        if (track.type == Stream::Raw || track.type == Stream::FpgaWave || track.type == Stream::FpgaSensor) {
            description = QStringLiteral("%1 B · %2").arg(e.length).arg(QString::fromLatin1(sample.bytes.left(24).toHex(' ')));
        } else {
            auto fields = SessionCsv::parseCsvLine(QString::fromUtf8(sample.bytes).trimmed());
            QStringList lines;
            for (int i = 0; i < track.columns.size(); ++i) {
                const auto value = fields.value(i);
                sample.fields.insert(track.columns[i], value);
                if (!value.isEmpty()) lines << track.columns[i] + " = " + value;
            }
            description = lines.join('\n');
        }
    }
    track.current = row;
    track.description = std::move(description);
    present(sample);
    return true;
}

void UiTestReplayWindow::tick()
{
    const auto& events = index_->events;
    position_ = anchor_ + std::min(quint64(clock_.elapsed()) * 1000, events.last().timestamp - anchor_);
    // Yield between batches so file changes and slider input stay responsive.
    QElapsedTimer batch;
    batch.start();
    for (int n = 0; n < 128 && batch.elapsed() < 12 && next_ < events.size() && events[next_].timestamp <= position_; ++n) {
        const auto e = events[next_];
        if (!deliver(e.track, e.row)) return;
        ++next_;
    }
    if (next_ == events.size()) setPlaying(false);
    else if (next_ > 0 && events[next_].timestamp < position_) position_ = events[next_ - 1].timestamp;
    updatePosition();
}

void UiTestReplayWindow::setPlaying(bool playing)
{
    if (playing && index_ && next_ < index_->events.size()) {
        anchor_ = position_;
        clock_.restart();
        timer_.start();
    } else timer_.stop();
    play_->setText(timer_.isActive() ? label("暂停", "Pause") : label("播放", "Play"));
}

void UiTestReplayWindow::updatePosition()
{
    if (!index_) return;
    if (refresh) refresh();
    const auto first = index_->events.first().timestamp;
    const auto span = index_->events.last().timestamp - first;
    const auto elapsed = position_ - first;
    const QSignalBlocker block(slider_);
    slider_->setValue(span ? int(double(elapsed) / span * slider_->maximum()) : slider_->maximum());
    time_->setText(QStringLiteral("%1 / %2 s").arg(elapsed / 1e6, 0, 'f', 2).arg(span / 1e6, 0, 'f', 2));
    if (streams_->currentIndex() < 0) return;
    const auto& track = index_->tracks.at(streams_->currentIndex());
    detail_->setText(QStringLiteral("%1 / %2").arg(track.current + 1).arg(track.entries.size()));
    detail_->setToolTip(track.description.toHtmlEscaped().replace('\n', "<br>"));
}
