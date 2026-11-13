#pragma once

#include "shellsession.h"
#include "vtscreen.h"

#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>

namespace purec {

class TerminalWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);

    QSize minimumSizeHint() const override;
    void runCommand(const QString &command);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private:
    void showPrompt();
    void redrawInput();
    void submit();
    void execute(const QString &trimmed);
    void builtinCd(const QString &arg);
    void print(const QString &text);
    QString prompt() const;
    QColor colorFor(int index, bool foreground) const;
    void updateMetrics();

    VtScreen m_screen;
    ShellSession m_shell;
    QFont m_font;
    QFont m_boldFont;
    int m_cellW = 8;
    int m_cellH = 16;
    int m_ascent = 12;
    int m_scrollOffset = 0;
    QString m_input;
    int m_cursor = 0;
    int m_promptLen = 0;
    int m_drawnCursor = 0;
    QString m_promptText;
    QStringList m_history;
    int m_historyPos = -1;
    bool m_blink = true;
    QTimer m_blinkTimer;
    QString m_cwd;
    int m_lastStatus = 0;
};

}
