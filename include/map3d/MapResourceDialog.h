#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace VaporView::Map3D {

class MapResourceManager;

class MapResourceDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit MapResourceDialog(MapResourceManager* manager, QWidget* parent = nullptr);

signals:
    void resourcesChanged();

private slots:
    void refreshManifest();
    void rescanLocalResources();
    void downloadSelected();
    void removeSelected();
    void refreshRows();
    void showOperationResult(const QString& packageId, bool success, const QString& message);
    void updateProgress(const QString& packageId, qint64 received, qint64 total);

private:
    QString selectedPackageId() const;
    void setBusy(bool busy);

    MapResourceManager* manager_ = nullptr;
    QLineEdit* manifest_url_edit_ = nullptr;
    QTableWidget* package_table_ = nullptr;
    QPushButton* refresh_button_ = nullptr;
    QPushButton* rescan_button_ = nullptr;
    QPushButton* download_button_ = nullptr;
    QPushButton* remove_button_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QLabel* status_label_ = nullptr;
};

} // namespace VaporView::Map3D
