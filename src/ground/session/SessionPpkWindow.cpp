#include "ground/session/SessionPpkWindow.h"
#include "ground/session/SessionPpkWidget.h"
#include "ground/widgets/CustomTitleBar.h"
#include "ground/widgets/WindowSizing.h"
#include <QScrollArea>

namespace VaporView::Ground::SessionUi
{
SessionPpkWindow::SessionPpkWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowFlag(Qt::Window, true);
    setObjectName(QStringLiteral("sessionPpkWindow"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("sessionPpkScrollArea"));
    scroll->viewport()->setObjectName(QStringLiteral("sessionPpkViewport"));
    scroll->setWidgetResizable(true);
    panel_ = new SessionPpkWidget(scroll);
    scroll->setWidget(panel_);
    setCentralWidget(scroll);
    connect(panel_, &SessionPpkWidget::busyChanged, this, &SessionPpkWindow::busyChanged);
    VaporView::installCustomTitleBar(this);
    setMinimumSize(640, 560);
    resize(820, 780);
    setEnglish(false);
    VaporView::centerWindowOnScreen(this, parent);
}

void SessionPpkWindow::setEnglish(bool english)
{
    setWindowTitle(QString::fromUtf8(english ? "PPK Processing" : "PPK 后处理"));
    panel_->setEnglish(english);
}

void SessionPpkWindow::setSessionDirectory(const QString &session)
{
    panel_->setSessionDirectory(session);
}

QString SessionPpkWindow::sessionDirectory() const
{
    return panel_->sessionDirectory();
}

bool SessionPpkWindow::busy() const
{
    return panel_->busy();
}
} // namespace VaporView::Ground::SessionUi
