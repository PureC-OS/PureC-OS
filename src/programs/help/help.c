#include "../../libc/include/purec.h"

int help_main(void){
    pc_write("PureC OS Help\n");
    pc_write("Builtins: clear pwd echo env set unset ping panic exit\n");
    pc_write("  ping [-c count] <ip|host|url>\n");
    pc_write("EXT2 debug: stat <path> | inode <num> | super | blocks <path> | fsinfo | dumpi <num>\n");
    pc_write("System programs resolve through PATH=/bin/program/system:/bin/program:/bin:\n");
    pc_write("  help | cd [directory] | ls [directory] | cat <file> | touch <file> | mkdir <directory>\n");
    pc_write("  nano <file> | hexedit <file> | disks | usbscan | dmesg | savelog\n");
    pc_write("  install | setup | update | mkfs.fat32\n");
    pc_write("  uname | about | systeminfo | htop | font | snake | tetris | files | gui-demo\n");
    pc_write("  mouse | debug | battery | reboot | poweroff | shutdown | halt\n");
    return 0;
}

void _start(void){
    pc_exit(help_main());
}

