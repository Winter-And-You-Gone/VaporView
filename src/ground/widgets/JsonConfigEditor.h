#pragma once

#include <QPlainTextEdit>

class QEvent;
class QPaintEvent;
class QResizeEvent;
class QSyntaxHighlighter;
class QLabel;

namespace VaporView::Ground::Widgets
{

class JsonConfigEditor final : public QPlainTextEdit
{
public:
    explicit JsonConfigEditor(QWidget *parent = nullptr);

    void setValidationStatus(bool valid,
                             const QString& statusText,
                             const QString& detail = QString());

    bool hasValidationStatus() const { return validation_known_; }
    bool isValidationSuccessful() const { return validation_valid_; }

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    class LineNumberArea;

    int lineNumberAreaWidth() const;
    void updateLineNumberAreaWidth(int blockCount);
    void updateLineNumberArea(const QRect& rect, int dy);
    void paintLineNumberArea(QPaintEvent *event);
    void updateValidationBadgeGeometry();
    void updateValidationBadgeStyle();

    LineNumberArea *line_number_area_ = nullptr;
    QSyntaxHighlighter *syntax_highlighter_ = nullptr;
    QLabel *validation_badge_ = nullptr;
    bool validation_known_ = false;
    bool validation_valid_ = false;
};

} // namespace VaporView::Ground::Widgets
