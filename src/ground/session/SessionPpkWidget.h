#pragma once

#include "ppk/PpkProcessor.h"
#include <QWidget>
#include <memory>

class QLabel;
class QPushButton;
class QComboBox;
class QProgressBar;
class QDoubleSpinBox;
class QCheckBox;
class QSpinBox;
class QGroupBox;
template <typename T> class QFutureWatcher;

namespace VaporView::Ground::SessionUi
{
QString ppkStatusText(const Ppk::PpkStatus &status, bool hasSession, bool busy, bool english);

class SessionPpkWidget final : public QWidget
{
    Q_OBJECT
  public:
    explicit SessionPpkWidget(QWidget *parent = nullptr);
    ~SessionPpkWidget() override;
    void setEnglish(bool english);
    void setSessionDirectory(const QString &session);
    QString sessionDirectory() const
    {
        return session_;
    }
    bool busy() const
    {
        return watcher_ != nullptr;
    }
  signals:
    void busyChanged(bool busy);

  private:
    QString textFor(const char *en, const char *zh) const;
    void refresh();
    void run();
    void importFile(bool navigation);
    void startWork(const std::function<Ppk::PpkProcessResult()> &work, bool processing);
    QString session_;
    QString operation_error_;
    bool english_ = false;
    bool processing_ = false;
    QLabel *status_ = nullptr;
    QLabel *session_label_ = nullptr;
    QLabel *rover_status_ = nullptr;
    QLabel *base_status_ = nullptr;
    QLabel *navigation_status_ = nullptr;
    QGroupBox *inputs_group_ = nullptr;
    QGroupBox *settings_group_ = nullptr;
    QGroupBox *processing_group_ = nullptr;
    QGroupBox *results_group_ = nullptr;
    QGroupBox *navigation_group_ = nullptr;
    QLabel *quality_ = nullptr;
    QLabel *source_label_ = nullptr;
    QLabel *arm_label_ = nullptr;
    QLabel *mask_label_ = nullptr;
    QPushButton *base_ = nullptr;
    QPushButton *nav_ = nullptr;
    QPushButton *run_ = nullptr;
    QPushButton *clear_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QComboBox *source_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QDoubleSpinBox *mask_ = nullptr;
    QSpinBox *receiver_ = nullptr;
    QSpinBox *frequencies_ = nullptr;
    QLabel *receiver_label_ = nullptr;
    QLabel *frequencies_label_ = nullptr;
    std::array<QDoubleSpinBox *, 3> arm_{};
    std::array<QCheckBox *, 5> systems_{};
    QFutureWatcher<Ppk::PpkProcessResult> *watcher_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancel_flag_;
};
} // namespace VaporView::Ground::SessionUi
