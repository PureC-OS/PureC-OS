#include "theme.h"

#include <QtCore/QString>

namespace purec {

const Theme &theme()
{
    static const Theme value;
    return value;
}

QString styleSheet()
{
    const Theme &t = theme();
    return QStringLiteral(
        "QWidget { background: %1; color: %2; }"
        "QLineEdit { background: %3; color: %2; border: 1px solid %4; border-radius: 8px; padding: 10px 12px; selection-background-color: %5; selection-color: %1; }"
        "QLineEdit:focus { border: 1px solid %5; }"
        "QPushButton { background: %5; color: %1; border: none; border-radius: 8px; padding: 12px 24px; font-weight: bold; }"
        "QPushButton:hover { background: #B4D0FB; }"
        "QPushButton:pressed { background: #6C9BE8; }"
        "QPushButton:disabled { background: %4; color: %6; }"
        "QLabel#error { color: %7; }"
        "QLabel#muted { color: %6; }")
        .arg(t.background.name(), t.text.name(), t.surface.name(), t.muted.name(),
             t.accent.name(), t.muted.name(), t.danger.name());
}

}
