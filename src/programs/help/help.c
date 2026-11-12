#include "../../libc/include/purec.h"

int help_main(void){
    pc_write("PureC OS Help\n");
    pc_write("Shell builtins: cd pwd echo export unset type kill exit\n");
    pc_write("Keys: Up/Down history, Tab completion, Ctrl+C interrupt, Ctrl+L clear\n");
    pc_write("Terminal: Ctrl+Shift+T new tab, Ctrl+Shift+W close, Ctrl+Tab switch, Ctrl+/- font size\n");
    pc_write("System programs resolve through PATH=/usr/bin:/bin:/usr/games:/sbin:\n");
    pc_write("  help | cd [directory] | pwd | clear | echo [text] | ls [directory] | cat <file>\n");
    pc_write("  touch <file> | mkdir <directory> | rm [-rRfvd] <path> | mkfs <fs> <dev>\n");
    pc_write("  ping [-c n] <host> | nano <file> | vim <file> | hexedit <file> | disks | usbscan | dmesg | savelog\n");
    pc_write("  install | setup | update | mkfs.ext2\n");
    pc_write("  uname | about | systeminfo | htop | font | snake | tetris | files | gui-demo\n");
    pc_write("  mouse | debug | battery | cpuinfo | raminfo | hddcheck | reboot | poweroff | shutdown | halt\n");
    pc_write("  devmgr\n");
    return 0;
}

void _start(void){
    pc_exit(help_main());
}