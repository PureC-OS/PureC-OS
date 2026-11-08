#include "loginwindow.h"
#include "platform.h"
#include "theme.h"

#include <QtGui/QFont>
#include <QtWidgets/QApplication>

int main(int argc, char **argv)
{
    purec::preparePlatform(argc, argv);
    QApplication app(argc, argv);
    purec::installFonts();
    QFont font(purec::fontFamily(false));
    font.setPixelSize(15);
    app.setFont(font);
    app.setStyleSheet(purec::styleSheet());

    purec::LoginWindow window;
    window.resize(1024, 700);
    window.show();
    return app.exec();
}
