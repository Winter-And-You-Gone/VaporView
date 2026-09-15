#include "ground/widgets/JsonConfigEditor.h"

#include "shared/theme/AppTheme.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>

#include <algorithm>
#include <cstring>

namespace VaporView::Ground::Widgets
{

namespace
{

constexpr int kStringStateBit = 1;

int encodeBlockState(int depth, bool inString)
{
    return (std::max(0, depth) << 1) | (inString ? kStringStateBit : 0);
}

int blockDepth(int state)
{
    return state >= 0 ? state >> 1 : 0;
}

bool blockInString(int state)
{
    return state >= 0 && (state & kStringStateBit) != 0;
}

bool isJsonNumberCharacter(QChar ch)
{
    return ch.isDigit() || ch == QLatin1Char('-') || ch == QLatin1Char('+') ||
           ch == QLatin1Char('.') || ch == QLatin1Char('e') || ch == QLatin1Char('E');
}

bool isJsonLiteralStart(const QString& text, int position, const char *literal)
{
    const QString value = QString::fromLatin1(literal);
    if (text.mid(position, value.size()).compare(value, Qt::CaseSensitive) != 0)
    {
        return false;
    }
    const int end = position + value.size();
    return end >= text.size() ||
           (!text.at(end).isLetterOrNumber() && text.at(end) != QLatin1Char('_'));
}

class JsonSyntaxHighlighter final : public QSyntaxHighlighter
{
public:
    explicit JsonSyntaxHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
    }

protected:
    void highlightBlock(const QString& text) override
    {
        const bool dark = qApp && VaporView::isDarkThemePalette(qApp->palette());
        QTextCharFormat keyFormat;
        keyFormat.setForeground(VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark));
        keyFormat.setFontWeight(QFont::DemiBold);

        QTextCharFormat stringFormat;
        stringFormat.setForeground(VaporView::appThemeColor(VaporView::AppThemeColor::Success, dark));

        QTextCharFormat numberFormat;
        numberFormat.setForeground(VaporView::appThemeColor(VaporView::AppThemeColor::Warning, dark));

        QTextCharFormat literalFormat;
        literalFormat.setForeground(VaporView::appThemeColor(VaporView::AppThemeColor::Link, dark));
        literalFormat.setFontWeight(QFont::DemiBold);

        const QVector<QColor> bracketColors = {
            VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark),
            VaporView::appThemeColor(VaporView::AppThemeColor::Success, dark),
            VaporView::appThemeColor(VaporView::AppThemeColor::Warning, dark),
            VaporView::appThemeColor(VaporView::AppThemeColor::Link, dark),
            VaporView::appThemeColor(VaporView::AppThemeColor::Danger, dark),
        };

        int state = previousBlockState();
        int depth = blockDepth(state);
        bool inString = blockInString(state);
        int position = 0;
        while (position < text.size())
        {
            const QChar ch = text.at(position);
            if (inString)
            {
                const int stringStart = position;
                bool closed = false;
                while (position < text.size())
                {
                    if (text.at(position) == QLatin1Char('\\'))
                    {
                        position += static_cast<int>(std::min<qsizetype>(2, text.size() - position));
                        continue;
                    }
                    if (text.at(position) == QLatin1Char('"'))
                    {
                        ++position;
                        closed = true;
                        break;
                    }
                    ++position;
                }
                const int length = position - stringStart;
                int next = position;
                while (next < text.size() && text.at(next).isSpace())
                {
                    ++next;
                }
                setFormat(stringStart,
                          length,
                          closed && next < text.size() && text.at(next) == QLatin1Char(':')
                              ? keyFormat
                              : stringFormat);
                inString = !closed;
                continue;
            }

            if (ch == QLatin1Char('"'))
            {
                const int stringStart = position++;
                bool closed = false;
                while (position < text.size())
                {
                    if (text.at(position) == QLatin1Char('\\'))
                    {
                        position += static_cast<int>(std::min<qsizetype>(2, text.size() - position));
                        continue;
                    }
                    if (text.at(position) == QLatin1Char('"'))
                    {
                        ++position;
                        closed = true;
                        break;
                    }
                    ++position;
                }
                const int length = position - stringStart;
                int next = position;
                while (next < text.size() && text.at(next).isSpace())
                {
                    ++next;
                }
                setFormat(stringStart,
                          length,
                          closed && next < text.size() && text.at(next) == QLatin1Char(':')
                              ? keyFormat
                              : stringFormat);
                inString = !closed;
                continue;
            }
            if (ch == QLatin1Char('{') || ch == QLatin1Char('['))
            {
                QTextCharFormat bracketFormat;
                bracketFormat.setForeground(bracketColors.at(depth % bracketColors.size()));
                bracketFormat.setFontWeight(QFont::DemiBold);
                setFormat(position, 1, bracketFormat);
                ++depth;
                ++position;
                continue;
            }
            if (ch == QLatin1Char('}') || ch == QLatin1Char(']'))
            {
                const bool matched = depth > 0;
                const int bracketDepth = matched ? depth - 1 : 0;
                QTextCharFormat bracketFormat;
                bracketFormat.setForeground(matched
                    ? bracketColors.at(bracketDepth % bracketColors.size())
                    : VaporView::appThemeColor(VaporView::AppThemeColor::Danger, dark));
                bracketFormat.setFontWeight(QFont::DemiBold);
                setFormat(position, 1, bracketFormat);
                if (matched)
                {
                    --depth;
                }
                ++position;
                continue;
            }
            if (ch == QLatin1Char('-') || ch.isDigit())
            {
                const int numberStart = position;
                while (position < text.size() && isJsonNumberCharacter(text.at(position)))
                {
                    ++position;
                }
                setFormat(numberStart, position - numberStart, numberFormat);
                continue;
            }
            if (ch.isLetter())
            {
                int literalLength = 0;
                for (const char *literal : {"true", "false", "null"})
                {
                    if (isJsonLiteralStart(text, position, literal))
                    {
                        literalLength = static_cast<int>(std::strlen(literal));
                        break;
                    }
                }
                if (literalLength > 0)
                {
                    setFormat(position, literalLength, literalFormat);
                    position += literalLength;
                    continue;
                }
            }
            ++position;
        }

        setCurrentBlockState(encodeBlockState(depth, inString));
    }
};

} // namespace

JsonConfigEditor::JsonConfigEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    syntax_highlighter_ = new JsonSyntaxHighlighter(document());

    validation_badge_ = new QLabel(this);
    validation_badge_->setObjectName(QStringLiteral("deviceRemoteSkyJsonFormatBadge"));
    validation_badge_->setAttribute(Qt::WA_TransparentForMouseEvents);
    validation_badge_->setAlignment(Qt::AlignCenter);
    validation_badge_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    validation_badge_->hide();
    updateValidationBadgeStyle();
}

void JsonConfigEditor::setValidationStatus(bool valid,
                                            const QString& statusText,
                                            const QString& detail)
{
    validation_known_ = true;
    validation_valid_ = valid;
    setProperty("jsonFormatState", valid ? QStringLiteral("valid") : QStringLiteral("invalid"));
    setProperty("jsonFormatError", detail);
    validation_badge_->setText(statusText);
    validation_badge_->setToolTip(detail.isEmpty() ? statusText : detail);
    validation_badge_->setProperty("jsonFormatState", valid ? QStringLiteral("valid")
                                                              : QStringLiteral("invalid"));
    validation_badge_->show();
    updateValidationBadgeStyle();
    updateValidationBadgeGeometry();
    update();
}

void JsonConfigEditor::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange)
    {
        updateValidationBadgeStyle();
        if (syntax_highlighter_)
        {
            syntax_highlighter_->rehighlight();
        }
        update();
    }
}

void JsonConfigEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateValidationBadgeGeometry();
}

void JsonConfigEditor::updateValidationBadgeGeometry()
{
    if (!validation_badge_ || !validation_badge_->isVisible())
    {
        return;
    }
    const QSize hint = validation_badge_->sizeHint();
    const int rightInset = verticalScrollBar() && verticalScrollBar()->isVisible()
        ? verticalScrollBar()->width() + 8
        : 10;
    validation_badge_->setGeometry(std::max(0, width() - rightInset - hint.width()),
                                   8,
                                   hint.width(),
                                   hint.height());
}

void JsonConfigEditor::updateValidationBadgeStyle()
{
    if (!validation_badge_)
    {
        return;
    }
    const bool dark = VaporView::isDarkThemePalette(palette());
    const QColor foreground = VaporView::appThemeColor(
        validation_valid_ ? VaporView::AppThemeColor::Success : VaporView::AppThemeColor::Danger,
        dark);
    const QColor background = VaporView::appThemeColor(
        validation_valid_ ? VaporView::AppThemeColor::SuccessBackground
                          : VaporView::AppThemeColor::DangerBackground,
        dark);
    QFont badgeFont = font();
    badgeFont.setPointSizeF(std::max<qreal>(8.0, badgeFont.pointSizeF() - 2.0));
    badgeFont.setWeight(QFont::DemiBold);
    validation_badge_->setFont(badgeFont);
    validation_badge_->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background-color: %2; border: 1px solid %1; border-radius: 5px; padding: 1px 6px; }")
        .arg(foreground.name(), background.name()));
    validation_badge_->adjustSize();
}

} // namespace VaporView::Ground::Widgets
