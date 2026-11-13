#include "platform.h"
#include "terminalwidget.h"
#include "theme.h"

#include <QtGui/QFont>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

int main(int argc, char **argv)
{
    purec::preparePlatform(argc, argv);
    QApplication app(argc, argv);
    purec::installFonts();
    QFont font(purec::fontFamily(false));
    font.setPixelSize(14);
    app.setFont(font);
    app.setStyleSheet(purec::styleSheet());
    app.setApplicationName(QStringLiteral("PureC Terminal"));

    purec::TerminalWidget terminal;
    terminal.setWindowTitle(QStringLiteral("PureC Terminal (Qt)"));
    terminal.resize(760, 480);
    terminal.show();
    terminal.setFocus();
    return app.exec();
}
