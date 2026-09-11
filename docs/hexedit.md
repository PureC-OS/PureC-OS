# HexEdit — PureC OS

## Назначение
HexEdit — оконный hex-редактор для PureC OS. Работает в графике, не в консоли. Умеет открывать файлы и директории, показывать файл как таблицу `offset | hex | ascii`, редактировать по-байтно и по-тетраде, сохранять через VFS.

## Интеграция в систему
- Бинарник: `/bin/program/hexedit` — Limine-модуль. Загрузчик передает образ в `boot_get_module()` (`src/boot/boot.c:137`), ядро грузит ELF в `process_spawn_module()` (`src/kernel/process/process.c:176`). Поэтому для запуска не нужен FAT32.
- `src/boot/limine.conf:21,45` — `module_path: boot():/bin/program/hexedit` в primary и fallback.
- `Makefile:60` — `cp $(PROGRAM_DIR)/hexedit $(ISO_ROOT)/bin/program/hexedit` при сборке ISO.
- Рабочий стол: `src/userspace/userspace.c:54,144,391,461` — иконка `HX HexEdit`, `userspace_run_program("/bin/program/hexedit")`, перетаскивание.
- Files: `src/programs/files/app.c:122` — `h` на файле открывает HexEdit.
- Shell: `src/programs/terminal/shell.c:243` — резолв через `PATH=/bin/program:/bin`.

## Запуск
```
hexedit
hexedit /dmesg.txt
hexedit /bin
hexedit /kernel/version
```
Без аргумента — дерево `/`. С директорией — дерево этой директории. С файлом — загрузка файла и подсветка в дереве.

## Структура
```
src/programs/hexedit/
  Makefile
  include/hexedit/
    types.hpp
    buffer/buffer.hpp
    tree/tree.hpp
    window/window.hpp
    view/view.hpp
    input/input.hpp
    editor/editor.hpp
  src/
    main/main.cpp
    buffer/buffer.cpp
    tree/tree.cpp
    window/window.cpp
    view/view.cpp
    input/input.cpp
    editor/editor.cpp
```

- `types.hpp` — константы `BYTES_PER_ROW=16`, размеры панелей, `HexDirEntry`, `HexPromptMode`.
- `buffer/buffer.hpp/cpp` — буфер `pc_heap_grow` (`src/libc/include/purec.h:49`), `pc_file_open/read/write` (`src/libc/include/purec.h:51`), курсор, тетрада, `hex_visible_rows`, `hex_ensure_visible`, `hex_parse_hex`.
- `tree/tree.hpp/cpp` — VFS-директория `pc_directory_list` (`src/libc/include/purec.h:55`), `hex_normalize_dir`, `hex_join_path`, `hex_parent_dir`, `hex_refresh_dir`, сортировка директории первыми.
- `window/window.hpp/cpp` — обертка над `libpuregui` (`src/libgui/include/puregui.h:56`) — `pg_window_center/begin/end/poll`, `HexWindow`.
- `view/view.hpp/cpp` — отрисовка `pg_window_rect/text/clear` (`src/libgui/draw.c:41`), `SIDEBAR_W=240`, toolbar/header/status/sidebar/hex, индикатор `HEX/ASCII INS/OVR`.
- `input/input.hpp/cpp` — `hex_handle_hex/ascii/special/mouse/prompt`, специальная клавиатура (`src/drivers/input/keyboard.h:6`).
- `editor/editor.hpp/cpp` — `hexedit_run()` — главный цикл `pg_event` (`src/libgui/include/puregui.h:34`), dispatch `PG_EVENT_CLOSE/MOVE/MINIMIZE/FOCUS/REPAINT/KEY/SPECIAL_KEY/MOUSE_UP`.
- `main/main.cpp` — `_start`, `pc_get_command_line`/`pc_getenv("PWD")`/`pc_strlen`/`pc_copy`, резолв относительного пути → `hexedit_run`.

Все модули используют только `libpurec` и `libpuregui`, без костылей и без прямых `int 0x80`.

## Сборка
```sh
make hexedit
make -C src/programs/hexedit
make programs
make iso
```
`src/programs/hexedit/Makefile` — отдельный таргет, `src/programs/Makefile:5` — `hexedit: $(PROGRAM_DIR)/hexedit`, `Makefile:14` — `hexedit: libraries`.

Тулчейн `mk/toolchain.mk:5` — `x86_64-elf-g++ -std=c++20 -fno-exceptions -fno-rtti -mgeneral-regs-only -mcmodel=small`.

## Управление
- Стрелки — байт/тетрада, `Up/Down` — строка, `PageUp/PageDown` — страница, `Home/End` — начало/конец строки, `F1` — `INS/OVR`.
- `Tab` — переключение `HEX ↔ ASCII`, клик мышью в hex/ascii — позиционирование, клик в сайдбаре — `..` вверх или вход/открытие.
- `Backspace/Delete` — удаление, ввод `0-9 A-F` в HEX и ` `..`~` в ASCII — вставка/замена.
- `Ctrl+S` — сохранить (`pc_file_write`), `Ctrl+X` — выход (двойное нажатие при `dirty`), `Ctrl+G` — goto `0x...`, `Ctrl+F` — поиск ascii с wrap.
- Статус-бар — offset `hex/dec`, `val 0x.. 'c'`, `bytes *`, подсказки. Промпт — `Goto/Search`.

## Библиотеки
Использует только:
- `src/libc/include/purec.h:30` — `pc_heap_grow`, `pc_file_*`, `pc_directory_list`, `pc_display_get_info`, `pc_sleep`, `pc_get_command_line`, `pc_write`.
- `src/libgui/include/puregui.h:74` — `pg_window_*`, `pg_theme_default`.
- `src/lib/string.h` — `memcpy/memset` внутри `libpurec`.
Прямые syscalls не используются.

## Ограничения
- Один файл за сессию, максимум `64` записей в директории, буфер растет до `UINT32_MAX`, `pc_heap_grow` требует непрерывности — при фрагментации `reserve` вернет false.
- Поиск — только ascii-подстрока, без regex.
- FAT32 `VFS_MAX_OPEN_FILES=32` (`src/fs/vfs.c:7`), запись возможна только при смонтированном FAT32 (`fat32_is_mounted`).
