#include "vtscreen.h"

#include <algorithm>

namespace purec {

static const int kScrollbackLimit = 2000;

VtScreen::VtScreen(int columns, int rows)
    : m_columns(std::max(columns, 1)), m_rows(std::max(rows, 1)), m_bottom(m_rows - 1)
{
    m_cells.fill(Cell(), m_columns * m_rows);
}

const Cell &VtScreen::cell(int column, int row) const
{
    return m_cells[row * m_columns + column];
}

const Cell &VtScreen::historyCell(int column, int line) const
{
    static const Cell empty;
    const QVector<Cell> &row = m_scrollback[line];
    return column < row.size() ? row[column] : empty;
}

Cell VtScreen::blank() const
{
    Cell c;
    c.bg = m_pen.bg;
    return c;
}

void VtScreen::resize(int columns, int rows)
{
    columns = std::max(columns, 1);
    rows = std::max(rows, 1);
    if (columns == m_columns && rows == m_rows)
        return;
    QVector<Cell> next(columns * rows);
    int copyRows = std::min(rows, m_rows);
    int copyCols = std::min(columns, m_columns);
    int shift = std::max(0, m_cy - rows + 1);
    for (int y = 0; y < copyRows; ++y)
        for (int x = 0; x < copyCols; ++x)
            next[y * columns + x] = m_cells[(y + shift) * m_columns + x];
    m_cells = next;
    m_columns = columns;
    m_rows = rows;
    m_cy -= shift;
    m_top = 0;
    m_bottom = m_rows - 1;
    clampCursor();
    ++m_revision;
}

void VtScreen::clear()
{
    m_cells.fill(Cell(), m_columns * m_rows);
    m_scrollback.clear();
    m_cx = m_cy = 0;
    m_wrapPending = false;
    ++m_revision;
}

void VtScreen::clampCursor()
{
    m_cx = std::clamp(m_cx, 0, m_columns - 1);
    m_cy = std::clamp(m_cy, 0, m_rows - 1);
}

void VtScreen::scrollUp(int top, int bottom)
{
    if (top == 0 && bottom == m_rows - 1) {
        QVector<Cell> line(m_columns);
        for (int x = 0; x < m_columns; ++x)
            line[x] = m_cells[x];
        m_scrollback.append(line);
        if (m_scrollback.size() > kScrollbackLimit)
            m_scrollback.removeFirst();
    }
    for (int y = top; y < bottom; ++y)
        for (int x = 0; x < m_columns; ++x)
            m_cells[y * m_columns + x] = m_cells[(y + 1) * m_columns + x];
    for (int x = 0; x < m_columns; ++x)
        m_cells[bottom * m_columns + x] = blank();
}

void VtScreen::scrollDown(int top, int bottom)
{
    for (int y = bottom; y > top; --y)
        for (int x = 0; x < m_columns; ++x)
            m_cells[y * m_columns + x] = m_cells[(y - 1) * m_columns + x];
    for (int x = 0; x < m_columns; ++x)
        m_cells[top * m_columns + x] = blank();
}

void VtScreen::lineFeed()
{
    if (m_cy == m_bottom)
        scrollUp(m_top, m_bottom);
    else if (m_cy < m_rows - 1)
        ++m_cy;
}

void VtScreen::reverseIndex()
{
    if (m_cy == m_top)
        scrollDown(m_top, m_bottom);
    else if (m_cy > 0)
        --m_cy;
}

void VtScreen::put(char32_t ch)
{
    if (m_wrapPending) {
        m_cx = 0;
        lineFeed();
        m_wrapPending = false;
    }
    Cell c = m_pen;
    c.ch = ch;
    m_cells[m_cy * m_columns + m_cx] = c;
    if (m_cx == m_columns - 1)
        m_wrapPending = true;
    else
        ++m_cx;
}

void VtScreen::control(unsigned char c)
{
    switch (c) {
    case '\n': case 0x0B: case 0x0C:
        m_wrapPending = false;
        lineFeed();
        break;
    case '\r':
        m_wrapPending = false;
        m_cx = 0;
        break;
    case '\b':
        m_wrapPending = false;
        if (m_cx > 0)
            --m_cx;
        break;
    case '\t':
        m_wrapPending = false;
        m_cx = std::min((m_cx / 8 + 1) * 8, m_columns - 1);
        break;
    default:
        break;
    }
}

int VtScreen::param(int index, int fallback) const
{
    if (index >= m_params.size() || m_params[index] <= 0)
        return fallback;
    return m_params[index];
}

void VtScreen::eraseLine(int mode)
{
    int from = mode == 0 ? m_cx : 0;
    int to = mode == 1 ? m_cx : m_columns - 1;
    for (int x = from; x <= to; ++x)
        m_cells[m_cy * m_columns + x] = blank();
}

void VtScreen::eraseDisplay(int mode)
{
    if (mode == 2 || mode == 3) {
        for (Cell &c : m_cells)
            c = blank();
        if (mode == 3)
            m_scrollback.clear();
        return;
    }
    if (mode == 0) {
        eraseLine(0);
        for (int y = m_cy + 1; y < m_rows; ++y)
            for (int x = 0; x < m_columns; ++x)
                m_cells[y * m_columns + x] = blank();
    } else {
        eraseLine(1);
        for (int y = 0; y < m_cy; ++y)
            for (int x = 0; x < m_columns; ++x)
                m_cells[y * m_columns + x] = blank();
    }
}

void VtScreen::sgr()
{
    if (m_params.isEmpty())
        m_params.append(0);
    for (int i = 0; i < m_params.size(); ++i) {
        int p = std::max(m_params[i], 0);
        if (p == 0) {
            m_pen = Cell();
        } else if (p == 1) {
            m_pen.bold = true;
        } else if (p == 4) {
            m_pen.underline = true;
        } else if (p == 7) {
            m_pen.reverse = true;
        } else if (p == 22) {
            m_pen.bold = false;
        } else if (p == 24) {
            m_pen.underline = false;
        } else if (p == 27) {
            m_pen.reverse = false;
        } else if (p >= 30 && p <= 37) {
            m_pen.fg = p - 30;
        } else if (p == 39) {
            m_pen.fg = -1;
        } else if (p >= 40 && p <= 47) {
            m_pen.bg = p - 40;
        } else if (p == 49) {
            m_pen.bg = -1;
        } else if (p >= 90 && p <= 97) {
            m_pen.fg = p - 90 + 8;
        } else if (p >= 100 && p <= 107) {
            m_pen.bg = p - 100 + 8;
        } else if ((p == 38 || p == 48) && i + 2 < m_params.size() && m_params[i + 1] == 5) {
            int idx = m_params[i + 2] & 255;
            if (p == 38)
                m_pen.fg = idx;
            else
                m_pen.bg = idx;
            i += 2;
        }
    }
}

void VtScreen::csi(char final)
{
    m_wrapPending = false;
    switch (final) {
    case 'A': m_cy = std::max(m_top, m_cy - param(0, 1)); break;
    case 'B': m_cy = std::min(m_bottom, m_cy + param(0, 1)); break;
    case 'C': m_cx = std::min(m_columns - 1, m_cx + param(0, 1)); break;
    case 'D': m_cx = std::max(0, m_cx - param(0, 1)); break;
    case 'E': m_cx = 0; m_cy = std::min(m_bottom, m_cy + param(0, 1)); break;
    case 'F': m_cx = 0; m_cy = std::max(m_top, m_cy - param(0, 1)); break;
    case 'G': m_cx = param(0, 1) - 1; break;
    case 'd': m_cy = param(0, 1) - 1; break;
    case 'H': case 'f': m_cy = param(0, 1) - 1; m_cx = param(1, 1) - 1; break;
    case 'J': eraseDisplay(m_params.isEmpty() ? 0 : std::max(m_params[0], 0)); break;
    case 'K': eraseLine(m_params.isEmpty() ? 0 : std::max(m_params[0], 0)); break;
    case 'm': sgr(); break;
    case 'S': for (int n = param(0, 1); n > 0; --n) scrollUp(m_top, m_bottom); break;
    case 'T': for (int n = param(0, 1); n > 0; --n) scrollDown(m_top, m_bottom); break;
    case 'L':
        for (int n = param(0, 1); n > 0; --n)
            if (m_cy >= m_top && m_cy <= m_bottom) scrollDown(m_cy, m_bottom);
        break;
    case 'M':
        for (int n = param(0, 1); n > 0; --n)
            if (m_cy >= m_top && m_cy <= m_bottom) scrollUp(m_cy, m_bottom);
        break;
    case 'P':
        for (int n = param(0, 1); n > 0; --n) {
            for (int x = m_cx; x < m_columns - 1; ++x)
                m_cells[m_cy * m_columns + x] = m_cells[m_cy * m_columns + x + 1];
            m_cells[m_cy * m_columns + m_columns - 1] = blank();
        }
        break;
    case '@':
        for (int n = param(0, 1); n > 0; --n) {
            for (int x = m_columns - 1; x > m_cx; --x)
                m_cells[m_cy * m_columns + x] = m_cells[m_cy * m_columns + x - 1];
            m_cells[m_cy * m_columns + m_cx] = blank();
        }
        break;
    case 'X':
        for (int x = m_cx; x < std::min(m_columns, m_cx + param(0, 1)); ++x)
            m_cells[m_cy * m_columns + x] = blank();
        break;
    case 'r':
        m_top = std::clamp(param(0, 1) - 1, 0, m_rows - 1);
        m_bottom = std::clamp(param(1, m_rows) - 1, m_top, m_rows - 1);
        m_cx = 0;
        m_cy = m_top;
        break;
    case 's': m_savedX = m_cx; m_savedY = m_cy; break;
    case 'u': m_cx = m_savedX; m_cy = m_savedY; break;
    case 'h': case 'l':
        if (m_privateMode && param(0, 0) == 25)
            m_cursorVisible = final == 'h';
        break;
    default:
        break;
    }
    clampCursor();
}

void VtScreen::feed(const QByteArray &data)
{
    for (unsigned char b : data) {
        switch (m_state) {
        case State::Ground:
            if (m_utf8Left > 0) {
                if ((b & 0xC0) == 0x80) {
                    m_utf8Code = (m_utf8Code << 6) | (b & 0x3F);
                    if (--m_utf8Left == 0)
                        put(m_utf8Code);
                    break;
                }
                m_utf8Left = 0;
            }
            if (b == 0x1B) {
                m_state = State::Escape;
            } else if (b < 0x20 || b == 0x7F) {
                control(b);
            } else if (b < 0x80) {
                put(b);
            } else if ((b & 0xE0) == 0xC0) {
                m_utf8Code = b & 0x1F;
                m_utf8Left = 1;
            } else if ((b & 0xF0) == 0xE0) {
                m_utf8Code = b & 0x0F;
                m_utf8Left = 2;
            } else if ((b & 0xF8) == 0xF0) {
                m_utf8Code = b & 0x07;
                m_utf8Left = 3;
            }
            break;
        case State::Escape:
            m_state = State::Ground;
            if (b == '[') {
                m_state = State::Csi;
                m_params.clear();
                m_params.append(-1);
                m_privateMode = false;
            } else if (b == ']') {
                m_state = State::Osc;
            } else if (b == '(' || b == ')') {
                m_state = State::Charset;
            } else if (b == 'M') {
                reverseIndex();
            } else if (b == 'D') {
                lineFeed();
            } else if (b == 'E') {
                m_cx = 0;
                lineFeed();
            } else if (b == '7') {
                m_savedX = m_cx;
                m_savedY = m_cy;
            } else if (b == '8') {
                m_cx = m_savedX;
                m_cy = m_savedY;
            } else if (b == 'c') {
                m_pen = Cell();
                eraseDisplay(2);
                m_cx = m_cy = 0;
            }
            break;
        case State::Charset:
            m_state = State::Ground;
            break;
        case State::Osc:
            if (b == 0x07 || b == 0x1B || b == 0x9C)
                m_state = State::Ground;
            break;
        case State::Csi:
            if (b >= '0' && b <= '9') {
                int &last = m_params.last();
                last = (last < 0 ? 0 : last) * 10 + (b - '0');
            } else if (b == ';') {
                m_params.append(-1);
            } else if (b == '?') {
                m_privateMode = true;
            } else if (b >= 0x40 && b <= 0x7E) {
                csi(char(b));
                m_state = State::Ground;
            } else if (b < 0x20) {
                control(b);
            }
            break;
        }
    }
    ++m_revision;
}

}
