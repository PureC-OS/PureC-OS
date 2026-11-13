#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QSize>
#include <QtCore/QVector>
#include <QtGui/QColor>

namespace purec {

struct Cell {
    char32_t ch = U' ';
    int fg = -1;
    int bg = -1;
    bool bold = false;
    bool reverse = false;
    bool underline = false;
};

class VtScreen
{
public:
    VtScreen(int columns, int rows);

    void resize(int columns, int rows);
    void feed(const QByteArray &data);
    void clear();

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    int cursorColumn() const { return m_cx; }
    int cursorRow() const { return m_cy; }
    bool cursorVisible() const { return m_cursorVisible; }
    int scrollbackSize() const { return m_scrollback.size(); }
    const Cell &cell(int column, int row) const;
    const Cell &historyCell(int column, int line) const;
    uint64_t revision() const { return m_revision; }

private:
    enum class State { Ground, Escape, Csi, Osc, Charset };

    void put(char32_t ch);
    void control(unsigned char c);
    void csi(char final);
    void sgr();
    void lineFeed();
    void reverseIndex();
    void scrollUp(int top, int bottom);
    void scrollDown(int top, int bottom);
    void eraseLine(int mode);
    void eraseDisplay(int mode);
    void clampCursor();
    int param(int index, int fallback) const;
    Cell blank() const;

    int m_columns;
    int m_rows;
    int m_cx = 0;
    int m_cy = 0;
    int m_savedX = 0;
    int m_savedY = 0;
    int m_top = 0;
    int m_bottom;
    bool m_wrapPending = false;
    bool m_cursorVisible = true;
    Cell m_pen;
    QVector<Cell> m_cells;
    QVector<QVector<Cell>> m_scrollback;
    State m_state = State::Ground;
    QVector<int> m_params;
    bool m_privateMode = false;
    int m_utf8Left = 0;
    char32_t m_utf8Code = 0;
    uint64_t m_revision = 0;
};

}
