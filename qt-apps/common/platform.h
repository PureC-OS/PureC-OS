#pragma once

#include <QtCore/QString>

namespace purec {

void preparePlatform(int &argc, char **argv);
QString fontFamily(bool monospace);
void installFonts();

}
