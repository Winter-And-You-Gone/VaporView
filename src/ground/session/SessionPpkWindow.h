#pragma once

#include <QMainWindow>

namespace VaporView::Ground::SessionUi
{
class SessionPpkWidget;

class SessionPpkWindow final : public QMainWindow
{
    Q_OBJECT
  public:
    explicit SessionPpkWindow(QWidget *parent = nullptr);
    void setEnglish(bool english);
    void setSessionDirectory(const QString &session);
    QString sessionDirectory() const;
    bool busy() const;

  signals:
    void busyChanged(bool busy);

  private:
    SessionPpkWidget *panel_;
};
} // namespace VaporView::Ground::SessionUi
