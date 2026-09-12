# PureC OS — Справочник Системных Вызовов (Syscalls Reference)

Данный документ содержит полное описание всех системных вызовов (syscalls) ядра **PureC OS**, соглашения о вызовах, структуры данных, а также примеры использования каждого вызова через стандартную библиотеку (**PureC stdlib** / `<purec.h>` и hosted libc `<stdlib.h>`, `<stdio.h>`, `<unistd.h>`).

---

## Содержание

1. [Архитектура и Соглашение о Вызовах (ABI)](#1-архитектура-и-соглашение-о-вызовах-abi)
2. [Взаимодействие со Стандартной Библиотекой (Stdlib Architecture)](#2-взаимодействие-со-стандартной-библиотекой-stdlib-architecture)
3. [Сводная Таблица Системных Вызовов](#3-сводная-таблица-системных-вызовов)
4. [Подробное Описание Вызовов с Примерами](#4-подробное-описание-вызовов-с-примерами)
   - [4.1. Процессы и Управление Выполнением](#41-процессы-и-управление-выполнением)
   - [4.2. Управление Памятью (Куча)](#42-управление-памятью-куча)
   - [4.3. Переменные Окружения](#43-переменные-окружения)
   - [4.4. Файловая Система и VFS](#44-файловая-система-и-vfs)
   - [4.5. Диагностика и Метаданные ext2](#45-диагностика-и-метаданные-ext2)
   - [4.6. Консоль и Терминальный Ввод/Вывод](#46-консоль-и-терминальный-вводвывод)
   - [4.7. Графика и Framebuffer (GOP)](#47-графика-и-framebuffer-gop)
   - [4.8. Оконный Менеджер PureGUI](#48-оконный-менеджер-puregui)
   - [4.9. Манипулятор Мышь](#49-манипулятор-мышь)
   - [4.10. Аудиосистема](#410-аудиосистема)
   - [4.11. Сеть и Wi-Fi](#411-сеть-и-wi-fi)
   - [4.12. Накопители, Форматирование и Установка](#412-накопители-форматирование-и-установка)
   - [4.13. Управление Питанием и Системная Информация](#413-управление-питанием-и-системная-информация)

---

## 1. Архитектура и Соглашение о Вызовах (ABI)

Системные вызовы ядра выполняются через программное прерывание `int $0x80`. Все программы пользовательского пространства (ring 3) передают параметры через регистры общего назначения x86-64:

| Регистр | Назначение | Описание |
|---|---|---|
| `rax` | Номер системного вызова | Константа `SYS_*` |
| `rbx` | Аргумент 1 (`a1`) | Первый аргумент / указатель / дескриптор |
| `rcx` | Аргумент 2 (`a2`) | Второй аргумент / размер / флаги |
| `rdx` | Аргумент 3 (`a3`) | Третий аргумент / буфер / флаги |
| `rsi` | Аргумент 4 (`a4`) | Четвертый аргумент (применяется в `SYS_DRAW_RECT`, `SYS_DRAW_LINE`) |
| `rdi` | Аргумент 5 (`a5`) | Пятый аргумент (цвет в примитивах рисования) |

### Возвращаемые значения
- **Результат возвращается в регистре `rax`**:
  - `rax >= 0`: Успешное выполнение.
  - `rax < 0`: Код ошибки (отрицательные значения кодов `FS_ERROR_*` или системные ошибки `-1`).
- Все переданные пользователем указатели валидируются ядром: указатель должен указывать на действительные страницы Ring 3 текущего процесса, а для записи — на страницы с флагом записи (writable).

---

## 2. Взаимодействие со Стандартной Библиотекой (Stdlib Architecture)

В PureC OS приложения обычно **не вызывают** `int $0x80` напрямую вручную. Вместо этого они используют функции стандартной библиотеки:

1. **Библиотека ядра PureC (`<purec.h>`)**:
   Определяет прямые платформенные обертки вида `pc_*`:
   - Выполняют inline ассемблерную вставку `int $0x80`.
   - Занимаются приведением типов, упаковкой структур и проверкой результатов.
   - Пример: `pc_file_open()`, `pc_write()`, `pc_sleep()`, `pc_heap_grow()`, `pc_draw_rect()`.

2. **Hosted POSIX / C99 Libc (`<stdlib.h>`, `<stdio.h>`, `<unistd.h>`)**:
   Предоставляет стандартные сигнатуры языка C, которые внутри вызывают функции `pc_*`:
   - `malloc()`, `free()`, `realloc()`, `calloc()` реализованы поверх `pc_heap_grow()` (`SYS_HEAP_GROW`).
   - `exit()` и `abort()` вызывают `pc_exit()` (`SYS_EXIT`).
   - `open()`, `read()`, `write()`, `close()`, `lseek()`, `stat()`, `unlink()` вызывают соответствующие `pc_file_*` функции.
   - `fopen()`, `fread()`, `fwrite()`, `fseek()`, `fclose()`, `printf()` работают поверх дескрипторов и блочных записей.
   - `getenv()` вызывает `pc_getenv()` (`SYS_ENV_GET`).
   - `time()`, `clock()`, `gettimeofday()` вычисляют время по данным uptime из `SYS_CPU_INFO`.

---

## 3. Сводная Таблица Системных Вызовов

| Номер | Имя макроса | Обертка в stdlib | Краткое назначение |
|---|---|---|---|
| **1** | `SYS_WRITE` | `pc_write()`, `write()` | Вывод строки в последовательный порт и консоль |
| **2** | `SYS_CLEAR` | `pc_display_clear()` | Очистка экрана заданным цветом |
| **3** | `SYS_SLEEP` | `pc_sleep()`, `sleep()` | Блокировка потока на N миллисекунд |
| **39** | `SYS_GETPID` | `pc_getpid()` | Получение идентификатора текущего процесса |
| **59** | `SYS_EXEC` | `pc_exec()`, `pc_exec_with_args()` | Запуск ELF64 процесса |
| **60** | `SYS_EXIT` | `pc_exit()`, `exit()` | Завершение текущего процесса |
| **61** | `SYS_WAIT` | `pc_wait()` | Ожидание завершения дочернего процесса |
| **100** | `SYS_DRAW_RECT` | `pc_draw_rect()` | Рисование закрашенного прямоугольника |
| **101** | `SYS_DRAW_LINE` | `pc_syscall(SYS_DRAW_LINE, ...)` | Рисование отрезка прямой линии |
| **102** | `SYS_GET_MOUSE` | `pc_mouse_get()` | Опрос координат и кнопок мыши |
| **103** | `SYS_FB_INFO` | `pc_display_get_info()` | Получение параметров видеорежима (GOP) |
| **104** | `SYS_DRAW_TEXT` | `pc_draw_text()` | Отрисовка текста стандартным шрифтом 8x16 |
| **105** | `SYS_DRAW_TEXT_SIZED` | `pc_draw_text_sized()` | Отрисовка масштабируемого текста (8..48 px) |
| **106** | `SYS_SCROLL_RECT_UP` | `pc_syscall(SYS_SCROLL_RECT_UP, ...)` | Прокрутка прямоугольной области вверх |
| **107** | `SYS_SET_FONT_FACE` | `pc_syscall(SYS_SET_FONT_FACE, ...)` | Выбор начертания шрифта (Normal / Bold) |
| **108** | `SYS_GET_FONT_FACE` | `pc_syscall(SYS_GET_FONT_FACE, ...)` | Получение текущего начертания шрифта |
| **109** | `SYS_FB_BEGIN_UPDATE` | `pc_display_begin_update()` | Захват кадра (скрытие курсора перед рисованием) |
| **110** | `SYS_FB_END_UPDATE` | `pc_display_end_update()` | Окончание кадра (восстановление курсора) |
| **200** | `SYS_FILE_OPEN` (`SYS_OPEN`) | `pc_file_open()`, `open()`, `fopen()` | Открытие файла по пути |
| **201** | `SYS_FILE_READ` (`SYS_READ`) | `pc_file_read()`, `read()`, `fread()` | Чтение байт из дескриптора |
| **202** | `SYS_FILE_DELETE` (`SYS_UNLINK`) | `pc_file_delete()`, `unlink()` | Удаление файла |
| **203** | `SYS_FILE_RENAME` (`SYS_RENAME`) | `pc_file_rename()`, `rename()` | Переименование файла |
| **204** | `SYS_FILE_MOVE` | `pc_file_move()` | Перемещение файла в другой каталог |
| **205** | `SYS_DIR_LIST` | `pc_directory_list()` | Чтение содержимого каталога (краткие имена) |
| **206** | `SYS_FILE_CREATE` | `pc_file_create()`, `creat()` | Создание нового пустого файла |
| **207** | `SYS_DIR_CREATE` (`SYS_MKDIR`) | `pc_directory_create()`, `mkdir()` | Создание каталога |
| **208** | `SYS_DISK_LIST` | `pc_list_disks()` | Перечисление блочных дисковых устройств |
| **209** | `SYS_STORAGE_CONTROLLERS` | `pc_syscall(SYS_STORAGE_CONTROLLERS, ...)` | Список контроллеров хранения (AHCI, XHCI) |
| **210** | `SYS_FAT32_FORMAT` | `pc_syscall(SYS_FAT32_FORMAT, ...)` | Форматирование накопителя в FAT32 (GPT) |
| **211** | `SYS_FILE_WRITE` | `pc_file_write()`, `write()`, `fwrite()` | Запись буфера в файл целиком |
| **212** | `SYS_USB_RESCAN` | `pc_syscall(SYS_USB_RESCAN, ...)` | Повторное сканирование шины USB (XHCI/EHCI) |
| **213** | `SYS_FAT32_FORMAT_FORCE` | `pc_syscall(SYS_FAT32_FORMAT_FORCE, ...)` | Принудительное форматирование в FAT32 |
| **214** | `SYS_FAT32_FORMAT_UEFI` | `pc_syscall(SYS_FAT32_FORMAT_UEFI, ...)` | Форматирование с созданием UEFI ESP раздела |
| **215** | `SYS_CPU_INFO` | `pc_cpu_info()` | Информация о процессоре, загрузке и аптайме |
| **216** | `SYS_MEMORY_INFO` | `pc_memory_info()` | Статистика RAM (всего, занято, свободно) |
| **217** | `SYS_DISK_STATS` | `pc_syscall(SYS_DISK_STATS, ...)` | Суммарная статистика подключенных дисков |
| **218** | `SYS_REBOOT` | `pc_reboot()` | Перезагрузка компьютера |
| **219** | `SYS_SHUTDOWN` | `pc_shutdown()` | Выключение питания (ACPI / QEMU) |
| **220** | `SYS_BATTERY_INFO` | `pc_syscall(SYS_BATTERY_INFO, ...)` | Статус батареи ноутбука (ACPI) |
| **221** | `SYS_SCHED_YIELD` | `pc_syscall(SYS_SCHED_YIELD, ...)` | Передача управления следующему процессу |
| **222** | `SYS_FILE_CLOSE` (`SYS_CLOSE`) | `pc_file_close()`, `close()`, `fclose()` | Закрытие дескриптора файла |
| **223** | `SYS_FILE_APPEND` | `pc_syscall(SYS_FILE_APPEND, ...)` | Дописывание данных в конец файла |
| **224** | `SYS_GETCHAR` | `pc_read_line()`, `getchar()`, `fgetc()` | Блокирующее чтение символа с клавиатуры |
| **225** | `SYS_INSTALL_START` | `pc_install_start()` | Запуск асинхронного установщика ОС |
| **226** | `SYS_INSTALL_STATUS` | `pc_install_status()` | Запрос прогресса и этапа установки ОС |
| **227** | `SYS_TRY_GETCHAR` | `pc_try_getchar()` | Неблокирующее чтение символа с клавиатуры |
| **228** | `SYS_INSTALL_LOG` | `pc_install_log()` | Получение истории шагов установщика |
| **229** | `SYS_AUDIO_PLAY_TONE` | `pc_audio_play_tone()` | Воспроизведение звукового тона заданной частоты |
| **230** | `SYS_AUDIO_GET_STATUS` | `pc_audio_get_status()` | Статус аудиоподсистемы и кодеков |
| **231** | `SYS_AUDIO_GET_VOLUME` | `pc_audio_get_volume()` | Получение уровня громкости (0..100) |
| **232** | `SYS_AUDIO_SET_VOLUME` | `pc_audio_set_volume()` | Установка громкости звука (0..100) |
| **233** | `SYS_AUDIO_IS_MUTED` | `pc_audio_is_muted()` | Проверка отключения звука (mute) |
| **234** | `SYS_AUDIO_SET_MUTED` | `pc_audio_set_muted()` | Включение/выключение звука (mute) |
| **235** | `SYS_AUDIO_ADJUST_VOLUME` | `pc_audio_adjust_volume()` | Относительное изменение громкости (+/- delta) |
| **236** | `SYS_AUDIO_PLAY_TEST_SOUND` | `pc_audio_play_test()` | Проигрывание тестовой звуковой мелодии |
| **237** | `SYS_AUDIO_UPDATE` | `pc_syscall(SYS_AUDIO_UPDATE, ...)` | Обновление состояния аудиодвижка |
| **238** | `SYS_AUDIO_SELECT_OUTPUT_DEVICE`| `pc_audio_select_output()` | Переключение аудиовыхода (динамики/наушники) |
| **239** | `SYS_GET_COMMAND_LINE` | `pc_get_command_line()` | Чтение аргументов командной строки процесса |
| **240** | `SYS_ENV_GET` | `pc_getenv()`, `getenv()` | Чтение переменной окружения |
| **241** | `SYS_ENV_SET` | `pc_setenv()` | Установка переменной окружения |
| **242** | `SYS_ENV_UNSET` | `pc_unsetenv()` | Удаление переменной окружения |
| **243** | `SYS_ENV_LIST` | `pc_listenv()` | Получение списка всех переменных окружения |
| **244** | `SYS_GET_PROCESS_NAME` | `pc_get_process_name()` | Имя бинарника/алиаса текущего процесса |
| **245** | `SYS_MOUSE_DEBUG_GET` | `pc_syscall(SYS_MOUSE_DEBUG_GET, ...)` | Состояние оверлея отладки мыши |
| **246** | `SYS_MOUSE_DEBUG_SET` | `pc_syscall(SYS_MOUSE_DEBUG_SET, ...)` | Включение/выключение отладочного оверлея мыши |
| **247** | `SYS_CONSOLE_CONFIGURE` | `pc_console_configure()` | Настройка окна текстовой консоли на экране |
| **248** | `SYS_CONSOLE_CLEAR` | `pc_console_clear()` | Очистка окна текстовой консоли |
| **249** | `SYS_CONSOLE_DISABLE` | `pc_console_disable()` | Отключение текстовой консоли |
| **250** | `SYS_DESKTOP_REDRAW` | `pc_desktop_redraw()` | Запрос перерисовки рабочего стола композитором |
| **251** | `SYS_GUI_WINDOW_REGISTER` | `pc_gui_window_register()` | Регистрация окна процесса в PureGUI |
| **252** | `SYS_GUI_WINDOW_UPDATE` | `pc_gui_window_update()` | Обновление координат/размеров окна |
| **253** | `SYS_GUI_WINDOW_UNREGISTER` | `pc_gui_window_unregister()` | Удаление окна процесса из менеджера окон |
| **254** | `SYS_GUI_WINDOW_STATE` | `pc_gui_window_state()` | Опрос статуса окна (фокус, необходимость repaint) |
| **255** | `SYS_GUI_WINDOW_REPAINT_DONE` | `pc_gui_window_repaint_done()` | Подтверждение завершения перерисовки окна |
| **256** | `SYS_HEAP_GROW` | `pc_heap_grow()`, `malloc()` | Увеличение кучи процесса (аналог brk/sbrk) |
| **257** | `SYS_PROCESS_LIST` | `pc_process_list()` | Список запущенных процессов и статистика |
| **258** | `SYS_TRY_GET_SPECIAL` | `pc_try_get_special()` | Чтение спец. клавиш (стрелки, F-клавиши) |
| **259** | `SYS_NET_PING` | `pc_ping()` | ICMP Ping хоста по IP или доменному имени |
| **260** | `SYS_WIFI_SCAN` | `pc_wifi_scan()` | Запуск фонового сканирования Wi-Fi сетей |
| **261** | `SYS_WIFI_LIST` | `pc_wifi_list()` | Получение списка обнаруженных Wi-Fi точек |
| **262** | `SYS_WIFI_CONNECT` | `pc_wifi_connect()` | Подключение к Wi-Fi сети (SSID + пароль) |
| **263** | `SYS_WIFI_DISCONNECT` | `pc_wifi_disconnect()` | Отключение от текущей Wi-Fi сети |
| **264** | `SYS_WIFI_STATUS` | `pc_wifi_status()` | Статус Wi-Fi, IP, MAC и качество сигнала |
| **265** | `SYS_SAVE_KLOG` | `pc_save_klog()` | Сохранение логов ядра (klog) на накопитель |
| **266** | `SYS_GET_ROOT_DEVICE` | `pc_get_root_device()` | Имя дискового устройства корня VFS |
| **267** | `SYS_FAT32_FORMAT_CUSTOM` | `pc_format_custom()` | Форматирование с произвольной разметкой разделов |
| **268** | `SYS_FORMAT_DEVICE_EX` | `pc_format_device_ex()` | Форматирование устройства в FAT32 или ext2 |
| **269** | `SYS_INSTALL_START_EX` | `pc_install_start_ex()` | Запуск установщика с выбором ФС (FAT32/ext2) |
| **270** | `SYS_GET_FS_TYPE` | `pc_get_fs_type()` | Тип корневой файловой системы ("FAT32"/"ext2") |
| **271** | `SYS_EXT2_STAT` | `pc_ext2_stat()` | Получение stat-информации inode ext2 по пути |
| **272** | `SYS_EXT2_INODE` | `pc_ext2_inode()` | Получение метаданных inode ext2 по номеру |
| **273** | `SYS_EXT2_SUPER` | `pc_ext2_super()` | Чтение структуры суперблока ext2 |
| **274** | `SYS_EXT2_BLOCKS` | `pc_ext2_blocks()` | Список физических блоков файла в ext2 |
| **275** | `SYS_FILE_SEEK` | `pc_file_seek()`, `lseek()`, `fseek()` | Позиционирование указателя файла |
| **276** | `SYS_FILE_STAT` | `pc_file_stat()`, `stat()`, `fstat()` | Получение размера и атрибутов файла |
| **277** | `SYS_AUDIO_STOP_TONE` | `pc_audio_stop_tone()` | Остановка воспроизведения тона |
| **278** | `SYS_AUDIO_PCM_PUSH` | `pc_audio_pcm_push()`, `pc_audio_pcm_eos()` | Загрузка PCM сэмплов в звуковой буфер |
| **279** | `SYS_AUDIO_PCM_START` | `pc_audio_pcm_start()` | Запуск воспроизведения цифрового PCM аудио |
| **280** | `SYS_AUDIO_PCM_STOP` | `pc_audio_pcm_stop()` | Немедленная остановка воспроизведения PCM |
| **288** | `SYS_DIR_LIST_LONG` | `pc_directory_list_long()` | Расширенное чтение каталога (длинные имена) |

---

## 4. Подробное Описание Вызовов с Примерами

### 4.1. Процессы и Управление Выполнением

#### `SYS_GETPID` (39)
- **Регистры**: `rax = 39`
- **Обертка stdlib**: `int32_t pc_getpid(void)`
- **Описание**: Возвращает PID текущего процесса.
- **Пример**:
```c
#include <purec.h>

void print_pid_example(void) {
    int32_t pid = pc_getpid();
    pc_write("Текущий PID: ");
    pc_write_i64(pid);
    pc_write("\n");
}
```

---

#### `SYS_EXEC` (59)
- **Регистры**: `rax = 59`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)args` (или 0)
- **Обертки stdlib**:
  - `int32_t pc_exec(const char *path)`
  - `int32_t pc_exec_with_args(const char *path, const char *arguments)`
- **Описание**: Загружает ELF64 модуль по указанному пути, создает изолированное виртуальное адресное пространство и запускает процесс в ring 3. Возвращает PID созданного процесса или `< 0` при ошибке.
- **Пример**:
```c
#include <purec.h>

void spawn_child_example(void) {
    int32_t pid = pc_exec_with_args("/bin/program/hello", "--verbose");
    if (pid < 0) {
        pc_write("Ошибка запуска программы!\n");
        return;
    }
    pc_write("Процесс запущен, PID: ");
    pc_write_i64(pid);
    pc_write("\n");
}
```

---

#### `SYS_EXIT` (60)
- **Регистры**: `rax = 60`, `rbx = (int64_t)status`
- **Обертки stdlib**:
  - `void pc_exit(int32_t status) __attribute__((noreturn))`
  - `void exit(int status)` (также сбрасывает буферы потоков через `fflush`)
- **Описание**: Завершает работу текущего процесса и сохраняет код возврата для родителя.
- **Пример**:
```c
#include <purec.h>
#include <stdlib.h>

void terminate_example(void) {
    // Стандартный вызов libc exit
    exit(0);
    // Или напрямую через purec API:
    // pc_exit(0);
}
```

---

#### `SYS_WAIT` (61)
- **Регистры**: `rax = 61`, `rbx = (uint32_t)pid`, `rcx = (uintptr_t)status_ptr`, `rdx = nohang` (1 — без блокировки, 0 — с блокировкой)
- **Обертка stdlib**: `int32_t pc_wait(int32_t pid, int32_t *status, bool nohang)`
- **Описание**: Ожидает завершения процесса с указанным `pid`. Записывает статус завершения в `status`.
- **Пример**:
```c
#include <purec.h>

void wait_child_example(void) {
    int32_t pid = pc_exec("/bin/program/hello");
    if (pid > 0) {
        int32_t exit_code = 0;
        int32_t result = pc_wait(pid, &exit_code, false);
        if (result == pid) {
            pc_write("Дочерний процесс завершен с кодом: ");
            pc_write_i64(exit_code);
            pc_write("\n");
        }
    }
}
```

---

#### `SYS_SCHED_YIELD` (221)
- **Регистры**: `rax = 221`
- **Обертка stdlib**: `pc_syscall(SYS_SCHED_YIELD, 0, 0, 0)`
- **Описание**: Добровольно отдает остаток квантового времени планировщику ОС.
- **Пример**:
```c
#include <purec.h>

void cooperative_task(void) {
    for (int i = 0; i < 5; i++) {
        // Выполняем порцию вычислений
        pc_syscall(SYS_SCHED_YIELD, 0, 0, 0);
    }
}
```

---

#### `SYS_SLEEP` (3)
- **Регистры**: `rax = 3`, `rbx = milliseconds`
- **Обертки stdlib**:
  - `void pc_sleep(uint32_t milliseconds)`
  - `unsigned int sleep(unsigned int seconds)` (POSIX unistd)
- **Описание**: Переводит вызывающий поток в состояние ожидания таймера на заданное число миллисекунд.
- **Пример**:
```c
#include <purec.h>
#include <unistd.h>

void sleep_example(void) {
    pc_write("Ожидание 500 мс...\n");
    pc_sleep(500); // 0.5 секунды

    pc_write("Ожидание 1 секунды через POSIX sleep...\n");
    sleep(1);
}
```

---

#### `SYS_GET_COMMAND_LINE` (239)
- **Регистры**: `rax = 239`, `rbx = (uintptr_t)buffer`, `rcx = capacity`
- **Обертка stdlib**: `int32_t pc_get_command_line(char *buffer, uint32_t capacity)`
- **Описание**: Копирует всю строку аргументов командной строки текущего процесса в буфер.
- **Пример**:
```c
#include <purec.h>

void show_cmdline(void) {
    char cmd[128];
    if (pc_get_command_line(cmd, sizeof(cmd)) >= 0) {
        pc_write("Аргументы запуска: ");
        pc_write(cmd);
        pc_write("\n");
    }
}
```

---

#### `SYS_GET_PROCESS_NAME` (244)
- **Регистры**: `rax = 244`, `rbx = (uintptr_t)buffer`, `rcx = capacity`
- **Обертка stdlib**: `int32_t pc_get_process_name(char *buffer, uint32_t capacity)`
- **Описание**: Возвращает алиас или имя исполняемого файла, под которым был вызван процесс (используется для multicall бинарников вроде `/bin/program/system`).
- **Пример**:
```c
#include <purec.h>

void show_my_name(void) {
    char name[32];
    if (pc_get_process_name(name, sizeof(name)) >= 0) {
        pc_write("Имя программы: ");
        pc_write(name);
        pc_write("\n");
    }
}
```

---

#### `SYS_PROCESS_LIST` (257)
- **Регистры**: `rax = 257`, `rbx = (uintptr_t)processes`, `rcx = capacity`
- **Обертка stdlib**: `int32_t pc_process_list(struct process_monitor_info *processes, uint32_t capacity)`
- **Структура**:
```c
struct process_monitor_info {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t state;           // 1=READY, 2=RUNNING, 3=EXITED
    int32_t exit_code;
    uint32_t cpu_percent;
    uint32_t reserved;
    uint64_t runtime_ms;
    uint64_t resident_bytes;
    char name[32];
};
```
- **Описание**: Возвращает список всех активных процессов в системе (используется диспетчером задач `/bin/program/monitor`).
- **Пример**:
```c
#include <purec.h>

void ps_command(void) {
    struct process_monitor_info list[16];
    int32_t count = pc_process_list(list, 16);
    if (count < 0) return;

    for (int i = 0; i < count; i++) {
        pc_write("PID ");
        pc_write_u64(list[i].pid);
        pc_write(": ");
        pc_write(list[i].name);
        pc_write(" (RAM: ");
        pc_write_u64(list[i].resident_bytes / 1024);
        pc_write(" KB)\n");
    }
}
```

---

### 4.2. Управление Памятью (Куча)

#### `SYS_HEAP_GROW` (256)
- **Регистры**: `rax = 256`, `rbx = increment_bytes`
- **Обертка stdlib**:
  - `void *pc_heap_grow(uint64_t size)`
  - Стандартные функции аллокатора libc: `malloc()`, `calloc()`, `realloc()`, `free()`
- **Описание**: Увеличивает размер кучи процесса на `size` байт и возвращает предыдущую границу (break pointer). Если `size == 0`, просто возвращает текущую вершину кучи.
- **Пример**:
```c
#include <purec.h>
#include <stdlib.h>
#include <string.h>

void memory_example(void) {
    // Высокоуровневое выделение памяти через stdlib (malloc внутри вызывает SYS_HEAP_GROW):
    char *buffer = (char *)malloc(1024);
    if (buffer) {
        strcpy(buffer, "Тестовая строка в динамической памяти");
        pc_write(buffer);
        pc_write("\n");
        free(buffer);
    }

    // Низкоуровневое выделение напрямую через системный вызов:
    void *raw_page = pc_heap_grow(4096);
    if (raw_page) {
        pc_write("Выделена новая арена кучи по адресу: 0x");
        pc_write_u64((uint64_t)(uintptr_t)raw_page);
        pc_write("\n");
    }
}
```

---

### 4.3. Переменные Окружения

#### `SYS_ENV_GET` (240)
- **Регистры**: `rax = 240`, `rbx = (uintptr_t)name`, `rcx = (uintptr_t)buffer`, `rdx = capacity`
- **Обертки stdlib**:
  - `int32_t pc_getenv(const char *name, char *buffer, uint32_t capacity)`
  - `char *getenv(const char *name)`
- **Пример**:
```c
#include <purec.h>
#include <stdlib.h>

void env_get_example(void) {
    // Через hosted libc:
    char *path = getenv("PATH");
    if (path) {
        pc_write("PATH = ");
        pc_write(path);
        pc_write("\n");
    }

    // Через purec:
    char val[128];
    if (pc_getenv("USER", val, sizeof(val)) >= 0) {
        pc_write("USER = ");
        pc_write(val);
        pc_write("\n");
    }
}
```

---

#### `SYS_ENV_SET` (241)
- **Регистры**: `rax = 241`, `rbx = (uintptr_t)name`, `rcx = (uintptr_t)value`
- **Обертка stdlib**: `int32_t pc_setenv(const char *name, const char *value)`
- **Описание**: Устанавливает значение переменной окружения.
- **Пример**:
```c
#include <purec.h>

void env_set_example(void) {
    pc_setenv("THEME", "dark");
    pc_write("Переменная THEME установлена\n");
}
```

---

#### `SYS_ENV_UNSET` (242)
- **Регистры**: `rax = 242`, `rbx = (uintptr_t)name`
- **Обертка stdlib**: `int32_t pc_unsetenv(const char *name)`
- **Описание**: Удаляет переменную окружения.
- **Пример**:
```c
#include <purec.h>

void env_unset_example(void) {
    pc_unsetenv("THEME");
}
```

---

#### `SYS_ENV_LIST` (243)
- **Регистры**: `rax = 243`, `rbx = (uintptr_t)variables`, `rcx = capacity`
- **Обертка stdlib**: `int32_t pc_listenv(struct process_environment_variable *variables, uint32_t capacity)`
- **Структура**:
```c
struct process_environment_variable {
    uint8_t used;
    char name[32];
    char value[128];
};
```
- **Пример**:
```c
#include <purec.h>

void print_all_env(void) {
    struct process_environment_variable vars[16];
    int32_t count = pc_listenv(vars, 16);
    for (int i = 0; i < count; i++) {
        if (vars[i].used) {
            pc_write(vars[i].name);
            pc_write("=");
            pc_write(vars[i].value);
            pc_write("\n");
        }
    }
}
```

---

### 4.4. Файловая Система и VFS

#### `SYS_FILE_OPEN` / `SYS_OPEN` (200)
- **Регистры**: `rax = 200`, `rbx = (uintptr_t)path`
- **Обертки stdlib**:
  - `int32_t pc_file_open(const char *path)`
  - `int open(const char *path, int flags, ...)`
  - `FILE *fopen(const char *path, const char *mode)`
- **Описание**: Открывает существующий файл и возвращает его дескриптор.
- **Пример**:
```c
#include <purec.h>
#include <stdio.h>

void open_example(void) {
    // 1. Через стандартный stdio fopen:
    FILE *f = fopen("/purec/install.cfg", "r");
    if (f) {
        pc_write("Файл успешно открыт через fopen\n");
        fclose(f);
    }

    // 2. Напрямую через дескриптор purec:
    int32_t fd = pc_file_open("/purec/install.cfg");
    if (fd >= 0) {
        pc_write("Дескриптор получен: ");
        pc_write_i64(fd);
        pc_write("\n");
        pc_file_close(fd);
    }
}
```

---

#### `SYS_FILE_READ` / `SYS_READ` (201)
- **Регистры**: `rax = 201`, `rbx = (uint32_t)descriptor`, `rcx = (uintptr_t)buffer`, `rdx = capacity`
- **Обертки stdlib**:
  - `int32_t pc_file_read(int32_t descriptor, void *buffer, uint32_t capacity)`
  - `ssize_t read(int fd, void *buffer, size_t count)`
  - `size_t fread(void *ptr, size_t size, size_t count, FILE *stream)`
- **Пример**:
```c
#include <purec.h>

void read_example(void) {
    int32_t fd = pc_file_open("/etc/hostname");
    if (fd >= 0) {
        char buf[64];
        int32_t bytes = pc_file_read(fd, buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            pc_write("Hostname: ");
            pc_write(buf);
            pc_write("\n");
        }
        pc_file_close(fd);
    }
}
```

---

#### `SYS_FILE_WRITE` (211)
- **Регистры**: `rax = 211`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)buffer`, `rdx = size`
- **Обертки stdlib**:
  - `int32_t pc_file_write(const char *path, const void *buffer, uint32_t size)`
  - `ssize_t write(int fd, const void *buf, size_t count)`
  - `size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream)`
- **Описание**: Атомарно записывает все содержимое файла по заданному пути (модель full-file write VFS).
- **Пример**:
```c
#include <purec.h>

void write_example(void) {
    const char *text = "Hello from PureC OS!";
    int32_t res = pc_file_write("/test.txt", text, pc_strlen(text));
    if (res >= 0) {
        pc_write("Файл /test.txt успешно записан!\n");
    }
}
```

---

#### `SYS_FILE_APPEND` (223)
- **Регистры**: `rax = 223`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)buffer`, `rdx = size`
- **Обертка stdlib**: `pc_syscall(SYS_FILE_APPEND, (uint64_t)path, (uint64_t)buffer, size)`
- **Описание**: Дописывает данные в конец файла.
- **Пример**:
```c
#include <purec.h>

void append_example(void) {
    const char *extra = "\nДополнительная строка.";
    pc_syscall(SYS_FILE_APPEND, (uint64_t)"/test.txt", (uint64_t)extra, pc_strlen(extra));
}
```

---

#### `SYS_FILE_SEEK` (275)
- **Регистры**: `rax = 275`, `rbx = (uint32_t)descriptor`, `rcx = (uint64_t)offset`, `rdx = (uint32_t)whence` (0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END)
- **Обертки stdlib**:
  - `int64_t pc_file_seek(int32_t descriptor, int64_t offset, uint32_t whence)`
  - `off_t lseek(int fd, off_t offset, int origin)`
  - `int fseek(FILE *stream, long offset, int origin)`
- **Пример**:
```c
#include <purec.h>

void seek_example(void) {
    int32_t fd = pc_file_open("/test.txt");
    if (fd >= 0) {
        // Перемещаемся на 6-й байт от начала
        pc_file_seek(fd, 6, SEEK_SET);
        char buf[16];
        int32_t n = pc_file_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            pc_write("Прочитано после seek: ");
            pc_write(buf);
            pc_write("\n");
        }
        pc_file_close(fd);
    }
}
```

---

#### `SYS_FILE_STAT` (276)
- **Регистры**: `rax = 276`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)out`
- **Обертки stdlib**:
  - `int32_t pc_file_stat(const char *path, struct file_stat_info *out)`
  - `int stat(const char *path, struct stat *info)`
- **Структура**:
```c
struct file_stat_info {
    uint64_t size;
    uint32_t is_directory;
    uint32_t reserved;
};
```
- **Пример**:
```c
#include <purec.h>

void stat_example(void) {
    struct file_stat_info st;
    if (pc_file_stat("/purec/install.cfg", &st) >= 0) {
        pc_write("Размер файла: ");
        pc_write_u64(st.size);
        pc_write(" байт. Каталог: ");
        pc_write(st.is_directory ? "Да\n" : "Нет\n");
    }
}
```

---

#### `SYS_FILE_CLOSE` / `SYS_CLOSE` (222)
- **Регистры**: `rax = 222`, `rbx = (uint32_t)descriptor`
- **Обертки stdlib**:
  - `int32_t pc_file_close(int32_t descriptor)`
  - `int close(int fd)`
  - `int fclose(FILE *stream)`
- **Описание**: Освобождает дескриптор файла в таблице процесса и закрывает связанный файл в VFS.
- **Пример**:
```c
#include <purec.h>

void close_example(int32_t fd) {
    pc_file_close(fd);
}
```

---

#### `SYS_FILE_CREATE` (206)
- **Регистры**: `rax = 206`, `rbx = (uintptr_t)path`
- **Обертки stdlib**:
  - `int32_t pc_file_create(const char *path)`
  - `int creat(const char *path, int mode)`
- **Описание**: Создает пустой файл в VFS.
- **Пример**:
```c
#include <purec.h>

void touch_example(void) {
    if (pc_file_create("/newfile.dat") >= 0) {
        pc_write("Файл создан!\n");
    }
}
```

---

#### `SYS_FILE_DELETE` / `SYS_UNLINK` (202)
- **Регистры**: `rax = 202`, `rbx = (uintptr_t)path`
- **Обертки stdlib**:
  - `int32_t pc_file_delete(const char *path)`
  - `int unlink(const char *path)`
- **Описание**: Удаляет файл по указанному пути.
- **Пример**:
```c
#include <purec.h>

void delete_example(void) {
    pc_file_delete("/newfile.dat");
}
```

---

#### `SYS_FILE_RENAME` / `SYS_RENAME` (203)
- **Регистры**: `rax = 203`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)new_name`
- **Обертка stdlib**: `int32_t pc_file_rename(const char *path, const char *new_name)`
- **Описание**: Переименовывает файл в пределах текущего каталога.
- **Пример**:
```c
#include <purec.h>

void rename_example(void) {
    pc_file_rename("/old_name.txt", "new_name.txt");
}
```

---

#### `SYS_FILE_MOVE` (204)
- **Регистры**: `rax = 204`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)destination_dir`
- **Обертка stdlib**: `int32_t pc_file_move(const char *path, const char *destination_directory)`
- **Описание**: Перемещает файл в указанный каталог.
- **Пример**:
```c
#include <purec.h>

void move_example(void) {
    pc_file_move("/temp.txt", "/config");
}
```

---

#### `SYS_DIR_CREATE` / `SYS_MKDIR` (207)
- **Регистры**: `rax = 207`, `rbx = (uintptr_t)path`
- **Обертка stdlib**: `int32_t pc_directory_create(const char *path)`
- **Описание**: Создает новый каталог в файловой системе.
- **Пример**:
```c
#include <purec.h>

void mkdir_example(void) {
    pc_directory_create("/my_docs");
}
```

---

#### `SYS_DIR_LIST` (205)
- **Регистры**: `rax = 205`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)entries`, `rdx = capacity`
- **Обертка stdlib**: `int32_t pc_directory_list(const char *path, struct fs_directory_entry *entries, uint32_t capacity)`
- **Структура**:
```c
struct fs_directory_entry {
    char name[13];       // FAT 8.3 формат
    uint32_t size;
    uint8_t attributes;  // 0x10 = каталог
};
```
- **Пример**:
```c
#include <purec.h>

void ls_short_example(void) {
    struct fs_directory_entry entries[32];
    int32_t count = pc_directory_list("/", entries, 32);
    for (int i = 0; i < count; i++) {
        pc_write(entries[i].name);
        if (entries[i].attributes & FS_ATTRIBUTE_DIRECTORY) pc_write("/");
        pc_write("\n");
    }
}
```

---

#### `SYS_DIR_LIST_LONG` (288)
- **Регистры**: `rax = 288`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)entries`, `rdx = capacity`
- **Обертка stdlib**: `int32_t pc_directory_list_long(const char *path, struct fs_directory_entry_long *entries, uint32_t capacity)`
- **Структура**:
```c
struct fs_directory_entry_long {
    char name[256];      // Длинное имя LFN
    uint32_t size;
    uint8_t attributes;
    uint8_t reserved[3];
};
```
- **Пример**:
```c
#include <purec.h>

void ls_long_example(void) {
    struct fs_directory_entry_long entries[32];
    int32_t count = pc_directory_list_long("/bin/program", entries, 32);
    for (int i = 0; i < count; i++) {
        pc_write(entries[i].name);
        pc_write("  [");
        pc_write_u64(entries[i].size);
        pc_write(" bytes]\n");
    }
}
```

---

#### `SYS_GET_ROOT_DEVICE` (266)
- **Регистры**: `rax = 266`, `rbx = (uintptr_t)buffer`, `rcx = capacity`
- **Обертка stdlib**: `int32_t pc_get_root_device(char *buffer, uint32_t capacity)`
- **Описание**: Возвращает имя дискового раздела, с которого смонтирован корень `/` (например, `ahci0p2`).
- **Пример**:
```c
#include <purec.h>

void show_root_dev(void) {
    char dev[32];
    if (pc_get_root_device(dev, sizeof(dev)) >= 0) {
        pc_write("Root смонтирован с устройства: ");
        pc_write(dev);
        pc_write("\n");
    }
}
```

---

#### `SYS_GET_FS_TYPE` (270)
- **Регистры**: `rax = 270`, `rbx = (uintptr_t)buffer`, `rcx = capacity`
- **Обертка stdlib**: `int32_t pc_get_fs_type(char *buffer, uint32_t capacity)`
- **Описание**: Возвращает строку типа корневой ФС (`FAT32` или `ext2`).
- **Пример**:
```c
#include <purec.h>

void show_fs_type(void) {
    char fs_type[16];
    if (pc_get_fs_type(fs_type, sizeof(fs_type)) >= 0) {
        pc_write("Тип корневой ФС: ");
        pc_write(fs_type);
        pc_write("\n");
    }
}
```

---

### 4.5. Диагностика и Метаданные ext2

#### `SYS_EXT2_STAT` (271)
- **Регистры**: `rax = 271`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)out`
- **Обертка stdlib**: `int32_t pc_ext2_stat(const char *path, struct ext2_stat_info *out)`
- **Структура**:
```c
struct ext2_stat_info {
    uint32_t ino;
    uint16_t mode;
    uint16_t links;
    uint32_t size;
    uint32_t blocks;
    uint32_t uid;
    uint32_t gid;
    uint32_t atime, ctime, mtime, dtime;
    uint32_t flags;
    uint32_t blocks_ptr[15];
    uint32_t generation;
    uint32_t file_acl, dir_acl;
};
```
- **Пример**:
```c
#include <purec.h>

void ext2_stat_example(void) {
    struct ext2_stat_info info;
    if (pc_ext2_stat("/boot/loader.cfg", &info) >= 0) {
        pc_write("ext2 Inode: ");
        pc_write_u64(info.ino);
        pc_write(", блоки: ");
        pc_write_u64(info.blocks);
        pc_write("\n");
    }
}
```

---

#### `SYS_EXT2_INODE` (272)
- **Регистры**: `rax = 272`, `rbx = ino`, `rcx = (uintptr_t)out`
- **Обертка stdlib**: `int32_t pc_ext2_inode(uint32_t ino, struct ext2_stat_info *out)`
- **Описание**: Получает метаданные inode ext2 напрямую по номеру.
- **Пример**:
```c
#include <purec.h>

void ext2_inode_example(uint32_t ino) {
    struct ext2_stat_info info;
    if (pc_ext2_inode(ino, &info) >= 0) {
        pc_write("Размер файла из inode: ");
        pc_write_u64(info.size);
        pc_write("\n");
    }
}
```

---

#### `SYS_EXT2_SUPER` (273)
- **Регистры**: `rax = 273`, `rbx = (uintptr_t)out`
- **Обертка stdlib**: `int32_t pc_ext2_super(struct ext2_super_info *out)`
- **Структура**:
```c
struct ext2_super_info {
    uint32_t total_inodes;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t groups_count;
    uint32_t first_data_block;
    uint32_t inodes_per_block;
    uint16_t inode_size;
    uint16_t magic;           // 0xEF53
    uint32_t partition_lba;
    uint32_t state;
    uint32_t errors;
};
```
- **Пример**:
```c
#include <purec.h>

void ext2_super_example(void) {
    struct ext2_super_info sb;
    if (pc_ext2_super(&sb) >= 0) {
        pc_write("Размер блока ext2: ");
        pc_write_u64(sb.block_size);
        pc_write(" байт, всего блоков: ");
        pc_write_u64(sb.total_blocks);
        pc_write("\n");
    }
}
```

---

#### `SYS_EXT2_BLOCKS` (274)
- **Регистры**: `rax = 274`, `rbx = (uintptr_t)path`, `rcx = (uintptr_t)out`
- **Обертка stdlib**: `int32_t pc_ext2_blocks(const char *path, struct ext2_blocks_info *out)`
- **Структура**:
```c
struct ext2_blocks_info {
    uint32_t ino;
    uint32_t logical_count;
    uint32_t blocks[64];
};
```
- **Пример**:
```c
#include <purec.h>

void ext2_blocks_example(void) {
    struct ext2_blocks_info blk;
    if (pc_ext2_blocks("/boot/loader.cfg", &blk) >= 0) {
        pc_write("Логических блоков: ");
        pc_write_u64(blk.logical_count);
        pc_write("\n");
    }
}
```

---

### 4.6. Консоль и Терминальный Ввод/Вывод

#### `SYS_WRITE` (1)
- **Регистры**: `rax = 1`, `rbx = (uintptr_t)buffer`, `rcx = length`, `rdx = 1`
- **Обертки stdlib**:
  - `void pc_write(const char *text)`
  - `void pc_write_u64(uint64_t value)`
  - `void pc_write_i64(int64_t value)`
  - `ssize_t write(1, buffer, count)` (POSIX)
  - `printf(...)`, `puts(...)` (stdio)
- **Описание**: Выводит символы в последовательный порт отладки (COM1) и в активную экранную консоль GOP.
- **Пример**:
```c
#include <purec.h>
#include <stdio.h>

void write_console_example(void) {
    pc_write("Прямой вывод через purec stdlib\n");
    printf("Форматированный вывод: %d + %d = %d\n", 2, 2, 4);
}
```

---

#### `SYS_GETCHAR` (224)
- **Регистры**: `rax = 224`
- **Обертки stdlib**: `int getchar(void)`, `int fgetc(FILE *stream)`
- **Описание**: Блокирующее чтение одного символа с клавиатуры.
- **Пример**:
```c
#include <purec.h>
#include <stdio.h>

void wait_key_example(void) {
    pc_write("Нажмите любую клавишу для продолжения...\n");
    int c = getchar();
    pc_write("Нажата клавиша с кодом: ");
    pc_write_i64(c);
    pc_write("\n");
}
```

---

#### `SYS_TRY_GETCHAR` (227)
- **Регистры**: `rax = 227`
- **Обертка stdlib**: `int32_t pc_try_getchar(void)`
- **Описание**: Неблокирующий опрос клавиатуры. Возвращает ASCII код или `-1`, если буфер пуст.
- **Пример**:
```c
#include <purec.h>

void game_loop_poll(void) {
    int32_t key = pc_try_getchar();
    if (key == 'q' || key == 'Q') {
        pc_write("Выход из игры!\n");
    }
}
```

---

#### `SYS_TRY_GET_SPECIAL` (258)
- **Регистры**: `rax = 258`
- **Обертка stdlib**: `int32_t pc_try_get_special(void)`
- **Описание**: Неблокирующий опрос специальных управляющих клавиш (стрелки вверх/вниз/влево/вправо, F-клавиши).
- **Пример**:
```c
#include <purec.h>

void special_key_example(void) {
    int32_t code = pc_try_get_special();
    if (code > 0) {
        pc_write("Специальная клавиша: ");
        pc_write_i64(code);
        pc_write("\n");
    }
}
```

---

#### `SYS_CONSOLE_CONFIGURE` (247)
- **Регистры**: `rax = 247`, `rbx = (uintptr_t)req`
- **Обертка stdlib**: `bool pc_console_configure(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t fg, uint32_t bg)`
- **Структура**:
```c
struct framebuffer_console_request {
    uint32_t x, y;
    uint32_t width, height;
    uint32_t foreground, background;
};
```
- **Описание**: Ограничивает область текстового вывода заданным прямоугольником окна терминала на экране.
- **Пример**:
```c
#include <purec.h>

void setup_terminal_console(void) {
    // Консоль в координатах 100, 100 размером 640x400
    pc_console_configure(100, 100, 640, 400, 0xFFFFFFFF, 0xFF1E1E2E);
}
```

---

#### `SYS_CONSOLE_CLEAR` (248)
- **Регистры**: `rax = 248`
- **Обертка stdlib**: `void pc_console_clear(void)`
- **Описание**: Очищает активное окно консоли фоновым цветом.
- **Пример**:
```c
#include <purec.h>

void clear_term(void) {
    pc_console_clear();
}
```

---

#### `SYS_CONSOLE_DISABLE` (249)
- **Регистры**: `rax = 249`
- **Обертка stdlib**: `void pc_console_disable(void)`
- **Описание**: Отключает экранную консоль и восстанавливает полноэкранный графический режим.
- **Пример**:
```c
#include <purec.h>

void disable_term(void) {
    pc_console_disable();
}
```

---

### 4.7. Графика и Framebuffer (GOP)

#### `SYS_CLEAR` (2)
- **Регистры**: `rax = 2`, `rbx = color` (формат 0x00RRGGBB)
- **Обертка stdlib**: `void pc_display_clear(uint32_t color)`
- **Описание**: Заливает весь экран (framebuffer) заданным цветом.
- **Пример**:
```c
#include <purec.h>

void clear_screen(void) {
    // Темно-синий фон Catppuccin
    pc_display_clear(0x1E1E2E);
}
```

---

#### `SYS_DRAW_RECT` (100)
- **Регистры**: `rax = 100`, `rbx = x`, `rcx = y`, `rdx = w`, `rsi = h`, `rdi = color`
- **Обертка stdlib**: `void pc_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)`
- **Описание**: Рисует сплошной прямоугольник в заданных координатах.
- **Пример**:
```c
#include <purec.h>

void draw_button(void) {
    pc_display_begin_update();
    pc_draw_rect(50, 50, 120, 40, 0x89B4FA); // Синяя кнопка
    pc_display_end_update();
}
```

---

#### `SYS_DRAW_LINE` (101)
- **Регистры**: `rax = 101`, `rbx = x0`, `rcx = y0`, `rdx = x1`, `rsi = y1`, `rdi = color`
- **Обертка stdlib**: Вызывается через `syscall5`
- **Описание**: Рисует линию между точками `(x0, y0)` и `(x1, y1)`.
- **Пример**:
```c
#include <purec.h>

void draw_line_example(void) {
    // Рисование диагональной линии красного цвета (0xFF0000)
    __asm__ volatile(
        "int $0x80"
        :
        : "a"((uint64_t)SYS_DRAW_LINE), "b"((uint64_t)10), "c"((uint64_t)10),
          "d"((uint64_t)200), "S"((uint64_t)200), "D"((uint64_t)0xFF0000)
        : "memory"
    );
}
```

---

#### `SYS_FB_INFO` (103)
- **Регистры**: `rax = 103`, `rbx = (uintptr_t)info`
- **Обертка stdlib**: `bool pc_display_get_info(struct pc_display_info *info)`
- **Структура**:
```c
struct pc_display_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t size_bytes;
    uint8_t bpp;
    bool available;
};
```
- **Пример**:
```c
#include <purec.h>

void print_resolution(void) {
    struct pc_display_info fb;
    if (pc_display_get_info(&fb)) {
        pc_write("Разрешение экрана: ");
        pc_write_u64(fb.width);
        pc_write("x");
        pc_write_u64(fb.height);
        pc_write(" @ ");
        pc_write_u64(fb.bpp);
        pc_write("bpp\n");
    }
}
```

---

#### `SYS_DRAW_TEXT` (104)
- **Регистры**: `rax = 104`, `rbx = (uintptr_t)req`
- **Обертка stdlib**: `void pc_draw_text(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg)`
- **Структура**:
```c
struct framebuffer_text_request {
    uint32_t x, y;
    const char *text;
    uint32_t fg, bg;
    uint32_t size; // 8
};
```
- **Пример**:
```c
#include <purec.h>

void draw_labels(void) {
    pc_display_begin_update();
    pc_draw_text(60, 60, "Click Me", 0x000000, 0x89B4FA);
    pc_display_end_update();
}
```

---

#### `SYS_DRAW_TEXT_SIZED` (105)
- **Регистры**: `rax = 105`, `rbx = (uintptr_t)req`
- **Обертка stdlib**: `void pc_draw_text_sized(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg, uint32_t size)`
- **Описание**: Рисует масштабируемый текст (размер от 8 до 48 пикселей).
- **Пример**:
```c
#include <purec.h>

void draw_title(void) {
    pc_display_begin_update();
    pc_draw_text_sized(100, 50, "PureC OS 2.0", 0xCDD6F4, 0x1E1E2E, 24);
    pc_display_end_update();
}
```

---

#### `SYS_SCROLL_RECT_UP` (106)
- **Регистры**: `rax = 106`, `rbx = (uintptr_t)req`
- **Структура**:
```c
struct framebuffer_scroll_request {
    uint32_t x, y, w, h;
    uint32_t amount;
    uint32_t fill_color;
};
```
- **Описание**: Прокручивает указанный прямоугольник вверх на `amount` пикселей, а снизу освободившееся место заливает цветом `fill_color`.
- **Пример**:
```c
#include <purec.h>

void scroll_terminal_area(void) {
    struct framebuffer_scroll_request req = {
        .x = 0, .y = 0, .w = 800, .h = 600,
        .amount = 16,
        .fill_color = 0x000000
    };
    pc_syscall(SYS_SCROLL_RECT_UP, (uint64_t)&req, 0, 0);
}
```

---

#### `SYS_SET_FONT_FACE` (107) и `SYS_GET_FONT_FACE` (108)
- **Регистры**: `SYS_SET_FONT_FACE` (`rax = 107`, `rbx = face`), `SYS_GET_FONT_FACE` (`rax = 108`)
- **Описание**: Устанавливает/получает гарнитуру шрифта (0 = Regular, 1 = Bold).
- **Пример**:
```c
#include <purec.h>

void font_example(void) {
    // Установка полужирного шрифта:
    pc_syscall(SYS_SET_FONT_FACE, 1, 0, 0);
    pc_draw_text(10, 10, "Bold Text", 0xFFFFFF, 0x000000);

    // Чтение текущего шрифта:
    int64_t face = pc_syscall(SYS_GET_FONT_FACE, 0, 0, 0);
}
```

---

#### `SYS_FB_BEGIN_UPDATE` (109) и `SYS_FB_END_UPDATE` (110)
- **Регистры**: `rax = 109`, `rax = 110`
- **Обертки stdlib**:
  - `void pc_display_begin_update(void)`
  - `void pc_display_end_update(void)`
- **Описание**: Синхронизация отрисовки. `begin_update` временно скрывает аппаратный курсор мыши, чтобы прямое рисование в framebuffer не затирало фон под курсором. `end_update` восстанавливает курсор.
- **Пример**:
```c
#include <purec.h>

void atomic_frame_render(void) {
    pc_display_begin_update();
    pc_draw_rect(0, 0, 200, 200, 0x11111B);
    pc_draw_text(10, 10, "Rendering...", 0xFFFFFF, 0x11111B);
    pc_display_end_update();
}
```

---

#### `SYS_DESKTOP_REDRAW` (250)
- **Регистры**: `rax = 250`
- **Обертка stdlib**: `void pc_desktop_redraw(void)`
- **Описание**: Запрашивает у оконного менеджера и рабочего стола принудительную перерисовку фона, обоев и значков.
- **Пример**:
```c
#include <purec.h>

void request_desktop_refresh(void) {
    pc_desktop_redraw();
}
```

---

### 4.8. Оконный Менеджер PureGUI

Оконный менеджер PureGUI позволяет процессам регистрировать свои окна, координировать фокус и перерисовку.

#### `SYS_GUI_WINDOW_REGISTER` (251)
- **Регистры**: `rax = 251`, `rbx = (uintptr_t)req`
- **Обертка stdlib**: `bool pc_gui_window_register(const struct gui_window_request *request)`
- **Структура**:
```c
struct gui_window_request {
    uint32_t x, y;
    uint32_t width, height;
};
```
- **Пример**:
```c
#include <purec.h>

void register_my_window(void) {
    struct gui_window_request w = { .x = 100, .y = 100, .width = 400, .height = 300 };
    if (pc_gui_window_register(&w)) {
        pc_write("Окно зарегистрировано в PureGUI!\n");
    }
}
```

---

#### `SYS_GUI_WINDOW_UPDATE` (252)
- **Регистры**: `rax = 252`, `rbx = (uintptr_t)req`
- **Обертка stdlib**: `bool pc_gui_window_update(const struct gui_window_request *request)`
- **Описание**: Обновляет позицию или размер окна после перетаскивания пользователем.
- **Пример**:
```c
#include <purec.h>

void move_window(uint32_t new_x, uint32_t new_y) {
    struct gui_window_request w = { .x = new_x, .y = new_y, .width = 400, .height = 300 };
    pc_gui_window_update(&w);
}
```

---

#### `SYS_GUI_WINDOW_UNREGISTER` (253)
- **Регистры**: `rax = 253`
- **Обертка stdlib**: `void pc_gui_window_unregister(void)`
- **Описание**: Удаляет окно вызывающего процесса из стека окон оконного менеджера при закрытии программы.
- **Пример**:
```c
#include <purec.h>

void close_window_example(void) {
    pc_gui_window_unregister();
}
```

---

#### `SYS_GUI_WINDOW_STATE` (254)
- **Регистры**: `rax = 254`
- **Обертка stdlib**: `uint32_t pc_gui_window_state(void)`
- **Флаги**:
  - `GUI_WINDOW_STATE_FOCUSED (1)`: Окно имеет фокус клавиатурного ввода.
  - `GUI_WINDOW_STATE_REPAINT (2)`: Окно было перекрыто и требует перерисовки содержимого.
- **Пример**:
```c
#include <purec.h>

void check_window_events(void) {
    uint32_t state = pc_gui_window_state();
    if (state & GUI_WINDOW_STATE_REPAINT) {
        // Перерисовать окно
        pc_gui_window_repaint_done();
    }
}
```

---

#### `SYS_GUI_WINDOW_REPAINT_DONE` (255)
- **Регистры**: `rax = 255`
- **Обертка stdlib**: `void pc_gui_window_repaint_done(void)`
- **Описание**: Подтверждает оконному менеджеру, что событие перерисовки окна успешно обработано.
- **Пример**:
```c
#include <purec.h>

void done_repainting(void) {
    pc_gui_window_repaint_done();
}
```

---

### 4.9. Манипулятор Мышь

#### `SYS_GET_MOUSE` (102)
- **Регистры**: `rax = 102`, `rbx = (uintptr_t)state`
- **Обертка stdlib**: `bool pc_mouse_get(struct mouse_state *state)`
- **Структура**:
```c
struct mouse_state {
    int32_t x, y;        // Абсолютные координаты курсора
    int32_t dx, dy;      // Относительное смещение
    uint8_t buttons;     // bit 0: Left, bit 1: Right, bit 2: Middle
    bool has_data;
};
```
- **Пример**:
```c
#include <purec.h>

void poll_mouse_example(void) {
    struct mouse_state m;
    if (pc_mouse_get(&m)) {
        if (m.buttons & 1) {
            pc_write("Нажата левая кнопка мыши в точке: ");
            pc_write_i64(m.x);
            pc_write(", ");
            pc_write_i64(m.y);
            pc_write("\n");
        }
    }
}
```

---

#### `SYS_MOUSE_DEBUG_GET` (245) и `SYS_MOUSE_DEBUG_SET` (246)
- **Регистры**:
  - `SYS_MOUSE_DEBUG_GET`: `rax = 245`
  - `SYS_MOUSE_DEBUG_SET`: `rax = 246`, `rbx = enabled` (1 / 0)
- **Обертки stdlib**: Вызываются через `pc_syscall`
- **Описание**: Включает или отключает экранный оверлей с координатами и счетчиками пакетов мыши.
- **Пример**:
```c
#include <purec.h>

void toggle_mouse_overlay(bool on) {
    pc_syscall(SYS_MOUSE_DEBUG_SET, on ? 1 : 0, 0, 0);
}
```

---

### 4.10. Аудиосистема

Аудиоподсистема PureC OS поддерживает вывод звука через системный динамик (PC Speaker) или HD Audio (Intel HDA).

#### `SYS_AUDIO_PLAY_TONE` (229) и `SYS_AUDIO_STOP_TONE` (277)
- **Регистры**:
  - `SYS_AUDIO_PLAY_TONE`: `rax = 229`, `rbx = freq_hz` (30..8000), `rcx = duration_ms` (1..5000)
  - `SYS_AUDIO_STOP_TONE`: `rax = 277`
- **Обертки stdlib**:
  - `int32_t pc_audio_play_tone(uint32_t frequency_hz, uint32_t duration_ms)`
  - `void pc_audio_stop_tone(void)`
- **Пример**:
```c
#include <purec.h>

void beep_sound(void) {
    // Звуковой сигнал 440 Гц (нота Ля) длительностью 250 миллисекунд
    pc_audio_play_tone(440, 250);
}
```

---

#### `SYS_AUDIO_GET_STATUS` (230)
- **Регистры**: `rax = 230`, `rbx = (uintptr_t)status`
- **Обертка stdlib**: `bool pc_audio_get_status(struct audio_status *status)`
- **Структура**:
```c
struct audio_status {
    uint32_t volume;                 // 0..100
    uint32_t muted;                  // 1 или 0
    uint32_t backend;                // 0=NONE, 1=PC_SPEAKER, 2=HDA
    uint32_t available_backends;
    uint32_t pcm_ready;
    uint32_t test_active;
    uint32_t output_device_count;
    uint32_t selected_output_device;
    uint32_t hda_codec;
    uint32_t hda_dac_node;
    uint32_t hda_pin_node;
};
```
- **Пример**:
```c
#include <purec.h>

void show_audio_info(void) {
    struct audio_status st;
    if (pc_audio_get_status(&st)) {
        pc_write("Громкость: ");
        pc_write_u64(st.volume);
        pc_write("%, Backend: ");
        pc_write(st.backend == 2 ? "Intel HD Audio" : "PC Speaker");
        pc_write("\n");
    }
}
```

---

#### `SYS_AUDIO_GET_VOLUME` (231), `SYS_AUDIO_SET_VOLUME` (232), `SYS_AUDIO_ADJUST_VOLUME` (235)
- **Регистры**:
  - `SYS_AUDIO_GET_VOLUME`: `rax = 231`
  - `SYS_AUDIO_SET_VOLUME`: `rax = 232`, `rbx = volume` (0..100)
  - `SYS_AUDIO_ADJUST_VOLUME`: `rax = 235`, `rbx = delta` (signed)
- **Обертки stdlib**:
  - `int32_t pc_audio_get_volume(void)`
  - `void pc_audio_set_volume(uint32_t volume)`
  - `void pc_audio_adjust_volume(int32_t delta)`
- **Пример**:
```c
#include <purec.h>

void volume_control_example(void) {
    pc_audio_set_volume(80);     // Установить 80%
    pc_audio_adjust_volume(-10); // Убавить на 10%
    int32_t cur = pc_audio_get_volume();
    pc_write("Текущая громкость: ");
    pc_write_i64(cur);
    pc_write("%\n");
}
```

---

#### `SYS_AUDIO_IS_MUTED` (233) и `SYS_AUDIO_SET_MUTED` (234)
- **Регистры**:
  - `SYS_AUDIO_IS_MUTED`: `rax = 233`
  - `SYS_AUDIO_SET_MUTED`: `rax = 234`, `rbx = muted` (1 / 0)
- **Обертки stdlib**:
  - `bool pc_audio_is_muted(void)`
  - `void pc_audio_set_muted(bool muted)`
- **Пример**:
```c
#include <purec.h>

void toggle_mute(void) {
    bool muted = pc_audio_is_muted();
    pc_audio_set_muted(!muted);
}
```

---

#### `SYS_AUDIO_PLAY_TEST_SOUND` (236)
- **Регистры**: `rax = 236`
- **Обертка stdlib**: `void pc_audio_play_test(void)`
- **Описание**: Проигрывает тестовую звуковую мелодию.
- **Пример**:
```c
#include <purec.h>

void test_audio(void) {
    pc_audio_play_test();
}
```

---

#### `SYS_AUDIO_SELECT_OUTPUT_DEVICE` (238)
- **Регистры**: `rax = 238`, `rbx = index`
- **Обертка stdlib**: `bool pc_audio_select_output(uint32_t index)`
- **Описание**: Переключает вывод звука на другое устройство (например, выход на наушники).
- **Пример**:
```c
#include <purec.h>

void switch_to_headphones(void) {
    pc_audio_select_output(1);
}
```

---

#### `SYS_AUDIO_PCM_PUSH` (278), `SYS_AUDIO_PCM_START` (279), `SYS_AUDIO_PCM_STOP` (280)
- **Регистры**:
  - `SYS_AUDIO_PCM_PUSH`: `rax = 278`, `rbx = (uintptr_t)samples`, `rcx = frame_count` (0 = EOS)
  - `SYS_AUDIO_PCM_START`: `rax = 279`
  - `SYS_AUDIO_PCM_STOP`: `rax = 280`
- **Обертки stdlib**:
  - `int32_t pc_audio_pcm_push(const int16_t *samples, uint32_t frames)`
  - `void pc_audio_pcm_eos(void)`
  - `int32_t pc_audio_pcm_start(void)`
  - `void pc_audio_pcm_stop(void)`
- **Описание**: Потоковое воспроизведение несжатого аудио (PCM 16-бит моно, 22050 Гц).
- **Пример**:
```c
#include <purec.h>

void stream_pcm_wave(const int16_t *buffer, uint32_t total_samples) {
    // Отправляем чанк сэмплов в буфер ядра
    pc_audio_pcm_push(buffer, total_samples);
    // Сигнализируем о завершении потока
    pc_audio_pcm_eos();
    // Начинаем воспроизведение
    pc_audio_pcm_start();
}
```

---

### 4.11. Сеть и Wi-Fi

#### `SYS_NET_PING` (259)
- **Регистры**: `rax = 259`, `rbx = (uintptr_t)req`, `rcx = (uintptr_t)res`
- **Обертка stdlib**: `int32_t pc_ping(const char *target, uint16_t sequence, uint32_t timeout_ms, struct network_ping_result *result)`
- **Структуры**:
```c
struct network_ping_request {
    char target[128]; // IPv4 или доменное имя (DNS разрешается ядром)
    uint32_t timeout_ms;
    uint16_t sequence;
    uint16_t reserved;
};

struct network_ping_result {
    uint32_t address;
    uint32_t round_trip_ms;
    uint16_t sequence;
    uint8_t ttl;
    uint8_t reserved;
};
```
- **Пример**:
```c
#include <purec.h>

void ping_test(void) {
    struct network_ping_result res;
    int32_t status = pc_ping("8.8.8.8", 1, 2000, &res);
    if (status == 0) {
        pc_write("Ответ получен! RTT: ");
        pc_write_u64(res.round_trip_ms);
        pc_write(" мс, TTL: ");
        pc_write_u64(res.ttl);
        pc_write("\n");
    } else {
        pc_write("Хост недоступен (таймаут)\n");
    }
}
```

---

#### `SYS_WIFI_SCAN` (260), `SYS_WIFI_LIST` (261), `SYS_WIFI_STATUS` (264)
- **Регистры**:
  - `SYS_WIFI_SCAN`: `rax = 260`
  - `SYS_WIFI_LIST`: `rax = 261`, `rbx = (uintptr_t)networks`, `rcx = capacity`
  - `SYS_WIFI_STATUS`: `rax = 264`, `rbx = (uintptr_t)status`
- **Обертки stdlib**:
  - `int32_t pc_wifi_scan(void)`
  - `int32_t pc_wifi_list(struct wifi_network_info *networks, uint32_t capacity)`
  - `bool pc_wifi_status(struct wifi_status_info *status)`
- **Структуры**:
```c
struct wifi_network_info {
    char ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t security; // 0=Open, 1=WEP, 2=WPA2, 3=WPA3
    uint8_t reserved;
};
```
- **Пример**:
```c
#include <purec.h>

void scan_wifi_networks(void) {
    pc_wifi_scan();
    pc_sleep(1500); // Даем время адаптеру отсканировать эфир

    struct wifi_network_info nets[16];
    int32_t count = pc_wifi_list(nets, 16);
    pc_write("Найдено Wi-Fi сетей: ");
    pc_write_i64(count);
    pc_write("\n");

    for (int i = 0; i < count; i++) {
        pc_write("SSID: ");
        pc_write(nets[i].ssid);
        pc_write(" (Signal: ");
        pc_write_i64(nets[i].rssi);
        pc_write(" dBm)\n");
    }
}
```

---

#### `SYS_WIFI_CONNECT` (262) и `SYS_WIFI_DISCONNECT` (263)
- **Регистры**:
  - `SYS_WIFI_CONNECT`: `rax = 262`, `rbx = (uintptr_t)req`
  - `SYS_WIFI_DISCONNECT`: `rax = 263`
- **Обертки stdlib**:
  - `int32_t pc_wifi_connect(const char *ssid, const char *password)`
  - `int32_t pc_wifi_disconnect(void)`
- **Пример**:
```c
#include <purec.h>

void connect_home_wifi(void) {
    if (pc_wifi_connect("MyHomeNetwork", "SecretPass123") == 0) {
        pc_write("Подключение к Wi-Fi инициировано...\n");
    }
}
```

---

### 4.12. Накопители, Форматирование и Установка

#### `SYS_DISK_LIST` (208)
- **Регистры**: `rax = 208`, `rbx = (uintptr_t)devices`, `rcx = capacity`
- **Обертка stdlib**: `int32_t pc_list_disks(struct storage_device_info *devices, uint32_t capacity)`
- **Структура**:
```c
struct storage_device_info {
    char name[9];        // например "ahci0"
    char model[41];
    char serial[21];
    uint64_t sector_count;
    uint32_t sector_size;
    uint8_t channel, drive;
    uint8_t writable, selected;
    uint8_t transport, controller, port;
    uint8_t operational;
};
```
- **Пример**:
```c
#include <purec.h>

void list_all_disks(void) {
    struct storage_device_info disks[8];
    int32_t count = pc_list_disks(disks, 8);
    for (int i = 0; i < count; i++) {
        uint64_t gigabytes = (disks[i].sector_count * disks[i].sector_size) / (1024 * 1024 * 1024);
        pc_write(disks[i].name);
        pc_write(": ");
        pc_write(disks[i].model);
        pc_write(" (");
        pc_write_u64(gigabytes);
        pc_write(" GB)\n");
    }
}
```

---

#### `SYS_STORAGE_CONTROLLERS` (209)
- **Регистры**: `rax = 209`, `rbx = (uintptr_t)info`, `rcx = capacity`
- **Обертка stdlib**: `pc_syscall(SYS_STORAGE_CONTROLLERS, (uint64_t)info, capacity, 0)`
- **Описание**: Перечисляет обнаруженные PCI контроллеры дисков (AHCI, NVMe, USB XHCI).
- **Пример**:
```c
#include <purec.h>

void check_controllers(void) {
    struct storage_controller_info ctrls[4];
    int32_t count = (int32_t)pc_syscall(SYS_STORAGE_CONTROLLERS, (uint64_t)ctrls, 4, 0);
    pc_write("Найдено контроллеров: ");
    pc_write_i64(count);
    pc_write("\n");
}
```

---

#### `SYS_DISK_STATS` (217)
- **Регистры**: `rax = 217`, `rbx = (uintptr_t)info`
- **Структура**:
```c
struct disk_monitor_info {
    uint32_t device_count;
    uint32_t operational_count;
    uint64_t total_bytes;
};
```
- **Пример**:
```c
#include <purec.h>

void show_disk_stats(void) {
    struct disk_monitor_info stats;
    if (pc_syscall(SYS_DISK_STATS, (uint64_t)&stats, 0, 0) == 0) {
        pc_write("Дисков в системе: ");
        pc_write_u64(stats.operational_count);
        pc_write(", Общий объем: ");
        pc_write_u64(stats.total_bytes / (1024 * 1024 * 1024));
        pc_write(" GB\n");
    }
}
```

---

#### `SYS_USB_RESCAN` (212)
- **Регистры**: `rax = 212`, `rbx = (uintptr_t)status` (или 0)
- **Обертка stdlib**: `pc_syscall(SYS_USB_RESCAN, (uint64_t)status, 0, 0)`
- **Описание**: Производит принудительное сканирование портов XHCI и EHCI, переинициализирует новые флеш-накопители и мыши, а также монтирует корень при нахождении загрузочного диска.
- **Пример**:
```c
#include <purec.h>

void rescan_usb(void) {
    int64_t found = pc_syscall(SYS_USB_RESCAN, 0, 0, 0);
    pc_write("Обнаружено новых USB устройств: ");
    pc_write_i64(found);
    pc_write("\n");
}
```

---

#### `SYS_FORMAT_DEVICE_EX` (268) и `SYS_FAT32_FORMAT_CUSTOM` (267)
- **Регистры**:
  - `SYS_FORMAT_DEVICE_EX`: `rax = 268`, `rbx = (uintptr_t)req`
  - `SYS_FAT32_FORMAT_CUSTOM`: `rax = 267`, `rbx = (uintptr_t)req`
- **Обертки stdlib**:
  - `int32_t pc_format_device_ex(const char *device, const char *serial, uint8_t fs_type)`
  - `int32_t pc_format_custom(const char *device, uint32_t partition_count, const uint64_t *sizes_gb)`
- **Описание**: Привилегированные системные вызовы для форматирования дисков в файловую систему FAT32 или ext2. Требуют capability `PROCESS_CAP_STORAGE_ADMIN`.
- **Пример**:
```c
#include <purec.h>

void format_ext2_example(void) {
    // Форматирование устройства в ext2:
    int32_t res = pc_format_device_ex("ahci0", "SERIAL12345", 1 /* FS_TYPE_EXT2 */);
    if (res >= 0) {
        pc_write("Диск отформатирован в ext2!\n");
    }
}
```

---

#### `SYS_INSTALL_START` (225), `SYS_INSTALL_START_EX` (269), `SYS_INSTALL_STATUS` (226), `SYS_INSTALL_LOG` (228)
- **Регистры**:
  - `SYS_INSTALL_START`: `rax = 225`, `rbx = (uintptr_t)device`, `rcx = (uintptr_t)serial`
  - `SYS_INSTALL_START_EX`: `rax = 269`, `rbx = (uintptr_t)req`
  - `SYS_INSTALL_STATUS`: `rax = 226`, `rbx = (uintptr_t)status`
  - `SYS_INSTALL_LOG`: `rax = 228`, `rbx = (uintptr_t)log`
- **Обертки stdlib**:
  - `int32_t pc_install_start(const char *device, const char *serial)`
  - `int32_t pc_install_start_ex(const char *device, const char *serial, uint8_t fs_type)`
  - `bool pc_install_status(struct install_status *status)`
  - `bool pc_install_log(struct install_log *log)`
- **Структура**:
```c
struct install_status {
    uint32_t state;     // 1=RUNNING, 2=COMPLETED, 3=FAILED
    uint32_t progress;  // 0..100%
    int32_t result;
    char stage[48];     // Текстовое описание текущей операции
};
```
- **Описание**: Управляет процессом развертывания PureC OS на целевой накопитель через асинхронный воркер ядра.
- **Пример**:
```c
#include <purec.h>

void monitor_installation(void) {
    // Запуск установки с выбором ext2
    pc_install_start_ex("ahci0", "DRIVE_SERIAL", 1 /* FS_TYPE_EXT2 */);

    struct install_status st;
    while (true) {
        pc_install_status(&st);
        pc_write("Этап: ");
        pc_write(st.stage);
        pc_write(" [");
        pc_write_u64(st.progress);
        pc_write("%]\n");

        if (st.state == 2) {
            pc_write("Установка успешно завершена!\n");
            break;
        } else if (st.state == 3) {
            pc_write("Ошибка при установке!\n");
            break;
        }
        pc_sleep(250);
    }
}
```

---

### 4.13. Управление Питанием и Системная Информация

#### `SYS_CPU_INFO` (215)
- **Регистры**: `rax = 215`, `rbx = (uintptr_t)info`
- **Обертка stdlib**: `bool pc_cpu_info(struct cpu_monitor_info *info)`
- **Структура**:
```c
struct cpu_monitor_info {
    char name[49];
    uint32_t logical_processors;
    uint32_t usage_percent;
    uint64_t frequency_hz;
    uint64_t uptime_ms;
};
```
- **Пример**:
```c
#include <purec.h>

void show_cpu_stats(void) {
    struct cpu_monitor_info cpu;
    if (pc_cpu_info(&cpu)) {
        pc_write("CPU: ");
        pc_write(cpu.name);
        pc_write("\nЯдер: ");
        pc_write_u64(cpu.logical_processors);
        pc_write(", Нагрузка: ");
        pc_write_u64(cpu.usage_percent);
        pc_write("%, Аптайм: ");
        pc_write_u64(cpu.uptime_ms / 1000);
        pc_write(" сек.\n");
    }
}
```

---

#### `SYS_MEMORY_INFO` (216)
- **Регистры**: `rax = 216`, `rbx = (uintptr_t)info`
- **Обертка stdlib**: `bool pc_memory_info(struct memory_monitor_info *info)`
- **Структура**:
```c
struct memory_monitor_info {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t available_bytes;
    uint64_t framebuffer_bytes;
};
```
- **Пример**:
```c
#include <purec.h>

void show_ram_stats(void) {
    struct memory_monitor_info mem;
    if (pc_memory_info(&mem)) {
        pc_write("Память: Занято ");
        pc_write_u64(mem.used_bytes / (1024 * 1024));
        pc_write(" MB / Всего ");
        pc_write_u64(mem.total_bytes / (1024 * 1024));
        pc_write(" MB\n");
    }
}
```

---

#### `SYS_BATTERY_INFO` (220)
- **Регистры**: `rax = 220`, `rbx = (uintptr_t)out`
- **Структура**:
```c
struct battery_info {
    uint32_t present;
    uint32_t percent;
    uint32_t charging;
    uint32_t remaining_minutes;
    uint32_t voltage_mv;
    uint32_t current_ma;
    char name[32];
    char status_text[32];
};
```
- **Пример**:
```c
#include <purec.h>

void show_battery(void) {
    struct battery_info bat;
    if (pc_syscall(SYS_BATTERY_INFO, (uint64_t)&bat, 0, 0) == 0 && bat.present) {
        pc_write("Заряд батареи: ");
        pc_write_u64(bat.percent);
        pc_write("% (");
        pc_write(bat.charging ? "Заряжается" : "От батареи");
        pc_write(")\n");
    }
}
```

---

#### `SYS_REBOOT` (218) и `SYS_SHUTDOWN` (219)
- **Регистры**:
  - `SYS_REBOOT`: `rax = 218`
  - `SYS_SHUTDOWN`: `rax = 219`
- **Обертки stdlib**:
  - `void pc_reboot(void)`
  - `void pc_shutdown(void)`
- **Пример**:
```c
#include <purec.h>

void reboot_system_example(void) {
    pc_write("Перезагрузка...\n");
    pc_reboot();
}

void shutdown_system_example(void) {
    pc_write("Выключение питания...\n");
    pc_shutdown();
}
```

---

#### `SYS_SAVE_KLOG` (265)
- **Регистры**: `rax = 265`, `rbx = (uintptr_t)req`
- **Обертка stdlib**: `int32_t pc_save_klog(const char *device, const char *path)`
- **Структура**:
```c
struct save_klog_request {
    char device[32];
    char path[64];
};
```
- **Описание**: Сбрасывает кольцевой буфер сообщений ядра (`klog`) в файл на указанном накопителе (например, для отладки крашей и логов загрузки).
- **Пример**:
```c
#include <purec.h>

void dump_kernel_log(void) {
    if (pc_save_klog("ahci0p2", "/boot/klog.txt") >= 0) {
        pc_write("Лог ядра успешно сохранен в /boot/klog.txt\n");
    }
}
```

