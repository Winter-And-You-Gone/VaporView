#include "map3d/Map3DWindow.h"

#include "geo/SessionTrackReader.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QAction>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>

namespace VaporView::Map3D {

Map3DWindow::Map3DWindow(QWidget* parent)
    : QMainWindow(parent)
    , view_(new OsgEarthViewWidget(this))
    , status_label_(new QLabel(this))
{
    setObjectName(QStringLiteral("map3DWindow"));
    setWindowTitle(QStringLiteral("VaporView 3D Map"));
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(1100, 760);
    view_->setObjectName(QStringLiteral("map3DView"));
    setCentralWidget(view_);
    status_label_->setObjectName(QStringLiteral("map3DStatusLabel"));

    QToolBar* toolbar = addToolBar(QStringLiteral("3D Map"));
    QAction* openSessionAction = toolbar->addAction(QStringLiteral("打开 Session"));
    connect(openSessionAction, &QAction::triggered, this, &Map3DWindow::openSessionDirectory);

    QAction* clearAction = toolbar->addAction(QStringLiteral("清空轨迹"));
    connect(clearAction, &QAction::triggered, this, &Map3DWindow::clearTrack);

    follow_action_ = toolbar->addAction(QStringLiteral("跟随飞机"));
    follow_action_->setCheckable(true);
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    follow_action_->setChecked(settings.value(QStringLiteral("followAircraft"), true).toBool());
    view_->setFollowAircraft(follow_action_->isChecked());
    connect(follow_action_, &QAction::toggled, this, [this](bool enabled) {
        if (view_)
        {
            view_->setFollowAircraft(enabled);
        }
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        settings.setValue(QStringLiteral("followAircraft"), enabled);
    });

    QAction* loadEarthAction = toolbar->addAction(QStringLiteral("加载 Earth 文件"));
    connect(loadEarthAction, &QAction::triggered, this, &Map3DWindow::openEarthFile);

    statusBar()->addPermanentWidget(status_label_, 1);
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("未加载 Earth 文件，当前显示本地 NED 网格。加载 .earth 后显示地图底图。"), 8000);
}

Map3DWindow::~Map3DWindow() = default;

void Map3DWindow::appendSample(const VaporView::Geo::NavSample& sample)
{
    view_->appendSample(sample);
    updateStatus(&sample);
}

void Map3DWindow::appendSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    view_->appendSamples(samples);
    updateStatus(samples.empty() ? nullptr : &samples.back());
}

void Map3DWindow::clearTrack()
{
    view_->clearTrack();
    updateStatus(nullptr);
}

void Map3DWindow::loadSessionDirectory(const QString& sessionDir)
{
    const VaporView::Geo::SessionTrackReadResult result = VaporView::Geo::readSessionTrack(sessionDir);
    if (!result.ok)
    {
        QMessageBox::warning(this,
                             QStringLiteral("Session Track"),
                             QStringLiteral("无法读取轨迹: %1").arg(result.error));
        return;
    }

    clearTrack();
    appendSamples(result.samples);
    statusBar()->showMessage(QStringLiteral("Loaded %1 samples from %2")
                                 .arg(result.samples.size())
                                 .arg(result.sourceCsvPath),
                             5000);
}

void Map3DWindow::openSessionDirectory()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    const QString initial = settings.value(QStringLiteral("lastSessionDir")).toString();
    const QString dir = QFileDialog::getExistingDirectory(this,
                                                          QStringLiteral("选择 Session 目录"),
                                                          initial);
    if (dir.isEmpty())
    {
        return;
    }
    settings.setValue(QStringLiteral("lastSessionDir"), dir);
    loadSessionDirectory(dir);
}

void Map3DWindow::openEarthFile()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    const QString initial = settings.value(QStringLiteral("lastEarthFile")).toString();
    const QString file = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("加载 Earth 文件"),
                                                      initial,
                                                      QStringLiteral("osgEarth (*.earth);;All Files (*)"));
    if (file.isEmpty())
    {
        return;
    }
    if (!view_->loadEarthFile(file))
    {
        QMessageBox::warning(this,
                             QStringLiteral("osgEarth"),
                             QStringLiteral("无法加载 Earth 文件: %1").arg(file));
        return;
    }
    settings.setValue(QStringLiteral("lastEarthFile"), file);
    updateStatus(nullptr);
    statusBar()->showMessage(QStringLiteral("Loaded earth file: %1").arg(file), 5000);
}

void Map3DWindow::updateStatus(const VaporView::Geo::NavSample* latest)
{
    QString text = QStringLiteral("Points: %1").arg(view_ ? view_->sampleCount() : 0);
    if (view_)
    {
        const QSize framebufferSize = view_->framebufferSize();
        text += QStringLiteral(" | Map %1 | View %2x%3")
                    .arg(view_->hasEarthMap() ? QStringLiteral("Earth") : QStringLiteral("Local grid"))
                    .arg(framebufferSize.width())
                    .arg(framebufferSize.height());
    }
    if (latest && latest->hasLlh())
    {
        text += QStringLiteral(" | Lat %1 Lon %2 H %3 m | Fix %4")
                    .arg(latest->latDeg, 0, 'f', 7)
                    .arg(latest->lonDeg, 0, 'f', 7)
                    .arg(latest->heightM, 0, 'f', 2)
                    .arg(static_cast<int>(latest->fixQuality));
    }
    status_label_->setText(text);
}

} // namespace VaporView::Map3D
