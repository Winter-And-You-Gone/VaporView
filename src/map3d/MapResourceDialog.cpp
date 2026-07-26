#include "map3d/MapResourceDialog.h"

#include "map3d/MapResourceManager.h"

#include <QDialogButtonBox>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace VaporView::Map3D {
namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes < 0)
    {
        return QStringLiteral("未知");
    }
    if (bytes >= 1024LL * 1024LL * 1024LL)
    {
        return QStringLiteral("%1 GiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytes >= 1024LL * 1024LL)
    {
        return QStringLiteral("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QStringLiteral("%1 KiB").arg(std::max<qint64>(1, bytes / 1024));
}

} // namespace

MapResourceDialog::MapResourceDialog(MapResourceManager* manager, QWidget* parent)
    : QDialog(parent)
    , manager_(manager)
{
    setWindowTitle(QStringLiteral("3D 地图资源"));
    resize(820, 520);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    manifest_url_edit_ = new QLineEdit(this);
    manifest_url_edit_->setPlaceholderText(QStringLiteral("https://example.com/vaporview-map-manifest.json"));
    form->addRow(QStringLiteral("HTTP 清单"), manifest_url_edit_);
    root->addLayout(form);

    auto* hint = new QLabel(QStringLiteral("地图资源不会随安装包发布。下载完成后将按清单校验并放入本地 resources/maps。"), this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    package_table_ = new QTableWidget(this);
    package_table_->setColumnCount(4);
    package_table_->setHorizontalHeaderLabels({QStringLiteral("资源"), QStringLiteral("版本"), QStringLiteral("大小"), QStringLiteral("状态")});
    package_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    package_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    package_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    package_table_->horizontalHeader()->setStretchLastSection(true);
    package_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    root->addWidget(package_table_, 1);

    auto* actions = new QHBoxLayout;
    refresh_button_ = new QPushButton(QStringLiteral("读取清单"), this);
    rescan_button_ = new QPushButton(QStringLiteral("重新扫描"), this);
    download_button_ = new QPushButton(QStringLiteral("下载选中"), this);
    remove_button_ = new QPushButton(QStringLiteral("删除选中"), this);
    actions->addWidget(refresh_button_);
    actions->addWidget(rescan_button_);
    actions->addWidget(download_button_);
    actions->addWidget(remove_button_);
    actions->addStretch(1);
    root->addLayout(actions);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 1000);
    progress_bar_->setValue(0);
    progress_bar_->setTextVisible(true);
    root->addWidget(progress_bar_);

    status_label_ = new QLabel(this);
    status_label_->setWordWrap(true);
    root->addWidget(status_label_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    root->addWidget(buttons);

    connect(refresh_button_, &QPushButton::clicked, this, &MapResourceDialog::refreshManifest);
    connect(rescan_button_, &QPushButton::clicked, this, &MapResourceDialog::rescanLocalResources);
    connect(download_button_, &QPushButton::clicked, this, &MapResourceDialog::downloadSelected);
    connect(remove_button_, &QPushButton::clicked, this, &MapResourceDialog::removeSelected);
    connect(package_table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const bool enabled = !selectedPackageId().isEmpty();
        download_button_->setEnabled(enabled);
        remove_button_->setEnabled(enabled);
    });

    if (manager_)
    {
        manifest_url_edit_->setText(manager_->manifestUrl());
        connect(manager_, &MapResourceManager::manifestUpdated, this, &MapResourceDialog::refreshRows);
        connect(manager_, &MapResourceManager::operationFinished, this, &MapResourceDialog::showOperationResult);
        connect(manager_, &MapResourceManager::operationProgress, this, &MapResourceDialog::updateProgress);
        connect(manager_, &MapResourceManager::resourcesChanged, this, [this]() {
            refreshRows();
            emit resourcesChanged();
        });
    }
    refreshRows();
}

QString MapResourceDialog::selectedPackageId() const
{
    const QList<QTableWidgetItem*> selected = package_table_->selectedItems();
    if (selected.isEmpty())
    {
        return {};
    }
    return selected.first()->data(Qt::UserRole).toString();
}

void MapResourceDialog::refreshManifest()
{
    if (!manager_)
    {
        return;
    }
    manager_->setManifestUrl(manifest_url_edit_->text());
    setBusy(true);
    manager_->refreshManifest();
}

void MapResourceDialog::downloadSelected()
{
    if (!manager_)
    {
        return;
    }
    const QString id = selectedPackageId();
    if (id.isEmpty())
    {
        return;
    }
    setBusy(true);
    manager_->downloadPackage(id);
}

void MapResourceDialog::rescanLocalResources()
{
    refreshRows();
    if (manager_)
    {
        status_label_->setText(QStringLiteral("已重新扫描本地地图资源：%1").arg(manager_->downloadRoot()));
    }
}

void MapResourceDialog::removeSelected()
{
    if (!manager_)
    {
        return;
    }
    const QString id = selectedPackageId();
    if (id.isEmpty())
    {
        return;
    }
    const auto it = std::find_if(manager_->packages().cbegin(), manager_->packages().cend(), [&id](const MapResourcePackage& package) {
        return package.id == id;
    });
    if (it == manager_->packages().cend())
    {
        return;
    }
    if (QMessageBox::question(this,
                              QStringLiteral("删除地图资源"),
                              QStringLiteral("确定删除“%1”的本地文件吗？").arg(it->displayName)) != QMessageBox::Yes)
    {
        return;
    }
    setBusy(true);
    manager_->removePackage(id);
}

void MapResourceDialog::refreshRows()
{
    package_table_->setRowCount(0);
    if (!manager_)
    {
        return;
    }
    for (const MapResourcePackage& package : manager_->packages())
    {
        const int row = package_table_->rowCount();
        package_table_->insertRow(row);
        auto* name = new QTableWidgetItem(package.displayName);
        name->setData(Qt::UserRole, package.id);
        package_table_->setItem(row, 0, name);
        package_table_->setItem(row, 1, new QTableWidgetItem(package.version));
        package_table_->setItem(row, 2, new QTableWidgetItem(formatBytes(package.sizeBytes)));
        QString reason;
        const bool installed = manager_->packageInstalled(package, &reason);
        package_table_->setItem(row, 3, new QTableWidgetItem(installed ? QStringLiteral("已安装") : QStringLiteral("未安装")));
    }
    download_button_->setEnabled(!selectedPackageId().isEmpty());
    remove_button_->setEnabled(!selectedPackageId().isEmpty());
    status_label_->setText(QStringLiteral("资源目录：%1").arg(manager_->downloadRoot()));
}

void MapResourceDialog::showOperationResult(const QString& packageId, bool success, const QString& message)
{
    Q_UNUSED(packageId);
    setBusy(false);
    status_label_->setText(message);
    if (!success)
    {
        QMessageBox::warning(this, QStringLiteral("地图资源操作失败"), message);
    }
    refreshRows();
}

void MapResourceDialog::updateProgress(const QString& packageId, qint64 received, qint64 total)
{
    Q_UNUSED(packageId);
    if (total <= 0)
    {
        progress_bar_->setRange(0, 0);
        return;
    }
    progress_bar_->setRange(0, 1000);
    progress_bar_->setValue(static_cast<int>(std::clamp((received * 1000) / total, qint64(0), qint64(1000))));
}

void MapResourceDialog::setBusy(bool busy)
{
    refresh_button_->setEnabled(!busy);
    rescan_button_->setEnabled(!busy);
    download_button_->setEnabled(!busy && !selectedPackageId().isEmpty());
    remove_button_->setEnabled(!busy && !selectedPackageId().isEmpty());
    if (!busy)
    {
        progress_bar_->setRange(0, 1000);
        progress_bar_->setValue(0);
    }
}

} // namespace VaporView::Map3D
