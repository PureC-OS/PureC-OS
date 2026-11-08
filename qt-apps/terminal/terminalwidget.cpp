#include "terminalwidget.h"
#include "platform.h"
#include "theme.h"

#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtGui/QFontMetrics>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>
#include <algorithm>
#include <unistd.h>

namespace purec {

static const QColor kAnsi[16] = {
    QColor(0x45, 0x47, 0x5A), QColor(0xF3, 0x8B, 0xA8), QColor(0xA6, 0xE3, 0xA1), QColor(0xF9, 0xE2, 0xAF),
    QColor(0x89, 0xB4, 0xFA), QColor(0xF5, 0xC2, 0xE7), QColor(0x94, 0xE2, 0xD5), QColor(0xBA, 0xC2, 0xDE),
    QColor(0x58, 0x5B, 0x70), QColor(0xF3, 0x8B, 0xA8), QColor(0xA6, 0xE3, 0xA1), QColor(0xF9, 0xE2, 0xAF),
    QColor(0x89, 0xB4, 0xFA), QColor(0xF5, 0xC2, 0xE7), QColor(0x94, 0xE2, 0xD5), QColor(0xA6, 0xAD, 0xC8),
};

TerminalWidget::TerminalWidget(QWidget *parent)
    : QWidget(parent), m_screen(80, 24)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_font = QFont(fontFamily(true));
    m_font.setPixelSize(16);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    m_boldFont = m_font;
    m_boldFont.setBold(true);
    updateMetrics();
    m_cwd = QDir::currentPath();
    connect(&m_shell, &ShellSession::output, this, [this](const QByteArray &data) {
        m_screen.feed(data);
        m_scrollOffset = 0;
        update();
    });
    connect(&m_shell, &ShellSession::finished, this, [this](int status) {
        m_lastStatus = status;
        showPrompt();
    });
    connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
        m_blink = !m_blink;
        update();
    });
    m_blinkTimer.start(530);
    print(QStringLiteral("\x1b[1;34mPureC OS\x1b[0m terminal (Qt)\r\n"));
    showPrompt();
}

QSize TerminalWidget::minimumSizeHint() const
{
    return QSize(m_cellW * 20, m_cellH * 6);
}

void TerminalWidget::updateMetrics()
{
    QFontMetrics fm(m_font);
    m_cellW = std::max(fm.horizontalAdvance(QLatin1Char('M')), 1);
    m_cellH = std::max(fm.height(), 1);
    m_ascent = fm.ascent();
}

QColor TerminalWidget::colorFor(int index, bool foreground) const
{
    const Theme &t = theme();
    if (index < 0)
        return foreground ? t.text : t.surfaceDeep;
    if (index < 16)
        return kAnsi[index];
    if (index < 232) {
        int v = index - 16;
        auto level = [](int c) { return c == 0 ? 0 : 55 + c * 40; };
        return QColor(level(v / 36), level((v / 6) % 6), level(v % 6));
    }
    int g = 8 + (index - 232) * 10;
    return QColor(g, g, g);
}

void TerminalWidget::print(const QString &text)
{
    m_screen.feed(text.toUtf8());
}

QString TerminalWidget::prompt() const
{
    QString user = qEnvironmentVariable("USER", QStringLiteral("purec"));
    QString status = m_lastStatus == 0 ? QStringLiteral("32") : QStringLiteral("31");
    return QStringLiteral("\x1b[1;%1m%2@os\x1b[0m:\x1b[1;34m%3\x1b[0m$ ").arg(status, user, m_cwd);
}

void TerminalWidget::showPrompt()
{
    if (m_screen.cursorColumn() != 0)
        print(QStringLiteral("\r\n"));
    m_input.clear();
    m_cursor = 0;
    m_historyPos = -1;
    QString p = prompt();
    QString plain = p;
    plain.remove(QRegularExpression(QStringLiteral("\x1b\\[[0-9;]*m")));
    m_promptLen = plain.size();
    m_promptText = p;
    m_drawnCursor = m_promptLen;
    print(p);
    update();
}

void TerminalWidget::redrawInput()
{
    int cols = m_screen.columns();
    int total = m_promptLen + m_input.size();
    int cursorOffset = m_promptLen + m_cursor;
    int oldOffset = m_drawnCursor;
    QString out;
    int up = oldOffset / cols;
    if (up > 0)
        out += QStringLiteral("\x1b[%1A").arg(up);
    out += QStringLiteral("\r\x1b[J");
    out += m_promptText + m_input;
    int endRow = total > 0 ? (total - 1) / cols : 0;
    if (total > 0 && total % cols == 0)
        out += QStringLiteral(" \b");
    int endRowAfter = (total > 0 && total % cols == 0) ? total / cols : endRow;
    int targetRow = cursorOffset / cols;
    int targetCol = cursorOffset % cols;
    int back = endRowAfter - targetRow;
    if (back > 0)
        out += QStringLiteral("\x1b[%1A").arg(back);
    out += QStringLiteral("\r");
    if (targetCol > 0)
        out += QStringLiteral("\x1b[%1C").arg(targetCol);
    m_drawnCursor = cursorOffset;
    print(out);
}

void TerminalWidget::runCommand(const QString &command)
{
    print(command + QStringLiteral("\r\n"));
    execute(command.trimmed());
}

void TerminalWidget::execute(const QString &trimmed)
{
    m_input.clear();
    m_cursor = 0;
    m_drawnCursor = 0;
    if (trimmed.isEmpty()) {
        showPrompt();
        return;
    }
    if (trimmed == QLatin1String("clear")) {
        m_screen.clear();
        showPrompt();
        return;
    }
    if (trimmed == QLatin1String("exit")) {
        window()->close();
        return;
    }
    if (trimmed == QLatin1String("cd") || trimmed.startsWith(QLatin1String("cd "))) {
        builtinCd(trimmed.mid(2).trimmed());
        showPrompt();
        return;
    }
    if (!m_shell.start(trimmed)) {
        print(QStringLiteral("\x1b[31mcannot start the shell\x1b[0m\r\n"));
        m_lastStatus = 127;
        showPrompt();
    }
}

void TerminalWidget::builtinCd(const QString &arg)
{
    QString target = arg;
    if (target.isEmpty() || target == QLatin1String("~"))
        target = qEnvironmentVariable("HOME", QStringLiteral("/"));
    QString path = QDir::isAbsolutePath(target) ? target : QDir(m_cwd).absoluteFilePath(target);
    path = QDir::cleanPath(path);
    if (::chdir(path.toUtf8().constData()) != 0) {
        print(QStringLiteral("cd: %1: no such directory\r\n").arg(target));
        m_lastStatus = 1;
        return;
    }
    m_cwd = path;
    qputenv("PWD", path.toUtf8());
    m_lastStatus = 0;
}

void TerminalWidget::submit()
{
    QString line = m_input;
    if (m_shell.running()) {
        print(QStringLiteral("\r\n"));
        m_shell.write((line + QLatin1Char('\n')).toUtf8());
        m_input.clear();
        m_cursor = 0;
        m_drawnCursor = 0;
        m_promptLen = 0;
        m_promptText.clear();
        return;
    }
    if (!line.trimmed().isEmpty() && (m_history.isEmpty() || m_history.last() != line))
        m_history.append(line);
    int cols = m_screen.columns();
    int rest = ((m_promptLen + line.size()) / cols) - (m_drawnCursor / cols);
    if (rest > 0)
        print(QStringLiteral("\x1b[%1B").arg(rest));
    print(QStringLiteral("\r\n"));
    m_promptLen = 0;
    m_promptText.clear();
    execute(line.trimmed());
}

void TerminalWidget::resizeEvent(QResizeEvent *)
{
    m_screen.resize(std::max(width() / m_cellW, 1), std::max(height() / m_cellH, 1));
}

bool TerminalWidget::focusNextPrevChild(bool)
{
    return false;
}

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    int steps = event->angleDelta().y() / 40;
    m_scrollOffset = std::clamp(m_scrollOffset + steps, 0, m_screen.scrollbackSize());
    update();
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    int key = event->key();
    Qt::KeyboardModifiers mods = event->modifiers();
    bool ctrl = mods.testFlag(Qt::ControlModifier);
    m_scrollOffset = 0;
    m_blink = true;
    if (ctrl && key == Qt::Key_C) {
        if (m_shell.running()) {
            m_shell.interrupt();
        } else {
            print(QStringLiteral("^C\r\n"));
            showPrompt();
        }
        return;
    }
    if (ctrl && key == Qt::Key_D) {
        if (m_shell.running())
            m_shell.closeInput();
        return;
    }
    if (ctrl && key == Qt::Key_L) {
        m_screen.clear();
        if (!m_shell.running())
            showPrompt();
        update();
        return;
    }
    if (ctrl && key == Qt::Key_U) {
        m_input.clear();
        m_cursor = 0;
        redrawInput();
        update();
        return;
    }
    if (m_shell.running() && key != Qt::Key_Return && key != Qt::Key_Enter) {
        if (key == Qt::Key_Backspace) {
            if (!m_input.isEmpty()) {
                m_input.chop(1);
                print(QStringLiteral("\b \b"));
            }
        } else {
            QString text = event->text();
            if (!text.isEmpty() && text.at(0).unicode() >= 32 && text.at(0).unicode() != 127) {
                m_input += text;
                print(text);
            }
        }
        m_cursor = m_input.size();
        update();
        return;
    }
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        submit();
        break;
    case Qt::Key_Backspace:
        if (m_cursor > 0) {
            m_input.remove(m_cursor - 1, 1);
            --m_cursor;
            redrawInput();
        }
        break;
    case Qt::Key_Delete:
        if (m_cursor < m_input.size()) {
            m_input.remove(m_cursor, 1);
            redrawInput();
        }
        break;
    case Qt::Key_Left:
        if (m_cursor > 0) {
            --m_cursor;
            redrawInput();
        }
        break;
    case Qt::Key_Right:
        if (m_cursor < m_input.size()) {
            ++m_cursor;
            redrawInput();
        }
        break;
    case Qt::Key_Home:
        m_cursor = 0;
        redrawInput();
        break;
    case Qt::Key_End:
        m_cursor = m_input.size();
        redrawInput();
        break;
    case Qt::Key_Up:
        if (!m_history.isEmpty() && !m_shell.running()) {
            m_historyPos = m_historyPos < 0 ? m_history.size() - 1 : std::max(m_historyPos - 1, 0);
            m_input = m_history[m_historyPos];
            m_cursor = m_input.size();
            redrawInput();
        }
        break;
    case Qt::Key_Down:
        if (m_historyPos >= 0 && !m_shell.running()) {
            ++m_historyPos;
            m_input = m_historyPos < m_history.size() ? m_history[m_historyPos] : QString();
            if (m_historyPos >= m_history.size())
                m_historyPos = -1;
            m_cursor = m_input.size();
            redrawInput();
        }
        break;
    case Qt::Key_PageUp:
        m_scrollOffset = std::min(m_scrollOffset + m_screen.rows() / 2, m_screen.scrollbackSize());
        break;
    case Qt::Key_PageDown:
        m_scrollOffset = std::max(m_scrollOffset - m_screen.rows() / 2, 0);
        break;
    default: {
        QString text = event->text();
        if (!text.isEmpty() && text.at(0).unicode() >= 32 && text.at(0).unicode() != 127) {
            m_input.insert(m_cursor, text);
            m_cursor += text.size();
            redrawInput();
        }
        break;
    }
    }
    update();
}

void TerminalWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const Theme &t = theme();
    p.fillRect(rect(), t.surfaceDeep);
    int rows = m_screen.rows();
    int cols = m_screen.columns();
    int history = m_screen.scrollbackSize();
    int topLine = history - m_scrollOffset;
    for (int y = 0; y < rows; ++y) {
        int line = topLine + y;
        for (int x = 0; x < cols; ++x) {
            const Cell &c = line < history ? m_screen.historyCell(x, line) : m_screen.cell(x, line - history);
            QColor fg = colorFor(c.fg, true);
            QColor bg = colorFor(c.bg, false);
            if (c.reverse)
                std::swap(fg, bg);
            if (c.bold && c.fg >= 0 && c.fg < 8)
                fg = kAnsi[c.fg + 8];
            if (bg != t.surfaceDeep)
                p.fillRect(x * m_cellW, y * m_cellH, m_cellW, m_cellH, bg);
            if (c.ch != U' ') {
                p.setPen(fg);
                p.setFont(c.bold ? m_boldFont : m_font);
                p.drawText(x * m_cellW, y * m_cellH + m_ascent, QString::fromUcs4(&c.ch, 1));
                if (c.underline)
                    p.drawLine(x * m_cellW, y * m_cellH + m_ascent + 2, (x + 1) * m_cellW, y * m_cellH + m_ascent + 2);
            }
        }
    }
    if (m_scrollOffset == 0 && m_screen.cursorVisible() && m_blink && hasFocus()) {
        int cx = m_screen.cursorColumn() * m_cellW;
        int cy = m_screen.cursorRow() * m_cellH;
        p.fillRect(cx, cy, m_cellW, m_cellH, t.accent);
        const Cell &under = m_screen.cell(m_screen.cursorColumn(), m_screen.cursorRow());
        if (under.ch != U' ') {
            p.setPen(t.surfaceDeep);
            p.setFont(m_font);
            p.drawText(cx, cy + m_ascent, QString::fromUcs4(&under.ch, 1));
        }
    }
}

}
