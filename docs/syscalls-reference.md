# PureC OS — Справочник Системных Вызовов (Syscalls Reference)

> **Номера сгенерированы** из `src/arch/x86_64/entry/syscalls/syscall_64.tbl` скриптом `mk/syscalltbl.py` (`make syscalls`) и не редактируются вручную. Чтобы изменить номер, правьте таблицу и запустите `make syscalls`.

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
   - [4.7. Framebuffer (GOP)](#47-framebuffer-gop)
   - [4.8. Удалённые вызовы (графика и PureGUI)](#48-удалённые-вызовы-графика-и-puregui)
   - [4.9. Манипулятор Мышь](#49-манипулятор-мышь)
   - [4.10. Аудиосистема](#410-аудиосистема)
   - [4.11. Сеть и Wi-Fi](#411-сеть-и-wi-fi)
   - [4.12. Накопители, Форматирование и Установка](#412-накопители-форматирование-и-установка)
   - [4.13. Управление Питанием и Системная Информация](#413-управление-питанием-и-системная-информация)
   - [4.14. Права Доступа и sudo](#414-права-доступа-и-sudo)
   - [4.15. Экран и Ввод (видеосервер, этап 2)](#415-экран-и-ввод-видеосервер-этап-2)

---

## 1. Архитектура и Соглашение о Вызовах (ABI)

Системные вызовы ядра выполняются инструкцией `syscall` (вход через MSR `LSTAR`, точка `arch_syscall_entry` в `src/arch/x86_64/entry/entry_64.asm`), возврат — `sysret` или, если ядро изменило кадр (`execve`, сигналы), `iretq`. Программное прерывание `int $0x80` больше не поддерживается: вызов приводит к `#GP` и `SIGSEGV`. Соглашение о регистрах такое же, как в x86_64 Linux:

| Регистр | Назначение | Описание |
|---|---|---|
| `rax` | Номер системного вызова | Константа `SYS_*` |
| `rdi` | Аргумент 1 (`a1`) | Первый аргумент / указатель / дескриптор |
| `rsi` | Аргумент 2 (`a2`) | Второй аргумент / размер / флаги |
| `rdx` | Аргумент 3 (`a3`) | Третий аргумент / буфер / флаги |
| `r10` | Аргумент 4 (`a4`) | Четвертый аргумент (например, флаги в `SYS_SENDTO` / `SYS_RECVFROM`) |
| `r8` | Аргумент 5 (`a5`) | Пятый аргумент (например, адрес `sockaddr` в `SYS_SENDTO` / `SYS_RECVFROM`) |
| `r9` | Аргумент 6 (`a6`) | Шестой аргумент (сейчас не используется) |

Инструкция `syscall` сама портит `rcx` (адрес возврата) и `r11` (флаги), остальные регистры ядро сохраняет.

### Возвращаемые значения
- **Результат возвращается в регистре `rax`**:
  - `rax >= 0`: Успешное выполнение.
  - `rax < 0`: Код ошибки (отрицательные значения кодов `FS_ERROR_*` или системные ошибки `-1`).
- Все переданные пользователем указатели валидируются ядром: указатель должен указывать на действительные страницы Ring 3 текущего процесса, а для записи — на страницы с флагом записи (writable).

---

## 2. Взаимодействие со Стандартной Библиотекой (Stdlib Architecture)

В PureC OS приложения обычно **не вызывают** `syscall` напрямую вручную. Вместо этого они используют функции стандартной библиотеки:

1. **Библиотека ядра PureC (`<purec.h>`)**:
   Определяет прямые платформенные обертки вида `pc_*`:
   - Выполняют инструкцию `syscall` через общий inline-хелпер `__pc_syscall6` из `<purec/sysasm.h>`.
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
| **0** | `SYS_FD_READ` | — | Чтение из дескриптора любого типа |
| **1** | `SYS_FD_WRITE` | — | Запись в дескриптор любого типа |
| **2** | `SYS_FD_OPEN` | — | Открытие дескриптора |
| **3** | `SYS_FD_CTL` | — | Управление дескриптором (`FD_CTL_GETFD`, `FD_CTL_SETFD`, `FD_CTL_DUPFD`) |
| **4** | `SYS_FD_STAT` | — | Информация о дескрипторе |
| **5** | `SYS_FD_PATH` | — | Путь, связанный с дескриптором |
| **6** | `SYS_FD_SEEK` | — | Позиционирование дескриптора |
| **7** | `SYS_FD_TRUNCATE` | — | Усечение файла по дескриптору |
| **8** | `SYS_WRITE` | `pc_write()`, `write()` | Вывод строки в последовательный порт и консоль |
| **9** | `SYS_PIPE` | — | Создание канала |
| **10** | `SYS_DUP` | — | Дублирование дескриптора |
| **11** | `SYS_DUP2` | — | Дублирование дескриптора в заданный номер |
| **12** | `SYS_CHDIR` | — | Смена текущего каталога |
| **13** | `SYS_GETCWD` | — | Текущий каталог |
| **14** | `SYS_SYNC` | `pc_sync()` | Сброс буферов файловой системы на диск |
| **15** | `SYS_FILE_STAT_EX` | `pc_file_stat_ex()` | Расширенные атрибуты файла |
| **16** | `SYS_LSTAT_EX` | — | Атрибуты файла без перехода по символической ссылке |
| **17** | `SYS_READLINK` | — | Чтение цели символической ссылки |
| **18** | `SYS_SYMLINK` | — | Создание символической ссылки |
| **19** | `SYS_CHMOD` | `pc_chmod()` | Смена прав доступа к файлу |
| **20** | `SYS_CHOWN` | `pc_chown()` | Смена владельца файла (uid, gid) |
| **21** | `SYS_UTIMES` | `pc_utimes()` | Время доступа и изменения файла |
| **22** | `SYS_RENAME_PATH` | `rename()` | Переименование или перемещение пути |
| **23** | `SYS_FILE_MOVE` | `pc_file_move()` | Перемещение файла в другой каталог |
| **24** | `SYS_FILE_OPEN` (`SYS_OPEN`) | `pc_file_open()`, `open()`, `fopen()` | Открытие файла по пути |
| **25** | `SYS_FILE_READ` (`SYS_READ`) | `pc_file_read()`, `read()`, `fread()` | Чтение байт из дескриптора |
| **26** | `SYS_FILE_WRITE` | `pc_file_write()`, `write()`, `fwrite()` | Запись буфера в файл целиком |
| **27** | `SYS_FILE_APPEND` | `pc_syscall(SYS_FILE_APPEND, ...)` | Дописывание данных в конец файла |
| **28** | `SYS_FILE_SEEK` | `pc_file_seek()`, `lseek()`, `fseek()` | Позиционирование указателя файла |
| **29** | `SYS_FILE_STAT` | `pc_file_stat()`, `stat()`, `fstat()` | Получение размера и атрибутов файла |
| **30** | `SYS_FILE_CLOSE` (`SYS_CLOSE`) | `pc_file_close()`, `close()`, `fclose()` | Закрытие дескриптора файла |
| **31** | `SYS_FILE_DELETE` (`SYS_UNLINK`) | `pc_file_delete()`, `unlink()` | Удаление файла |
| **32** | `SYS_FILE_RENAME` (`SYS_RENAME`) | `pc_file_rename()`, `rename()` | Переименование файла |
| **33** | `SYS_FILE_CREATE` | `pc_file_create()`, `creat()` | Создание нового пустого файла |
| **34** | `SYS_DIR_CREATE` (`SYS_MKDIR`) | `pc_directory_create()`, `mkdir()` | Создание каталога |
| **35** | `SYS_DIR_LIST` | `pc_directory_list()` | Чтение содержимого каталога (краткие имена) |
| **36** | `SYS_DIR_LIST_LONG` | `pc_directory_list_long()` | Расширенное чтение каталога (длинные имена) |
| **37** | `SYS_FORK` | `pc_fork()` | Создание дочернего процесса |
| **38** | `SYS_EXEC` | `pc_exec()`, `pc_exec_with_args()` | Запуск ELF64 процесса |
| **39** | `SYS_EXECVE` | — | Замена образа процесса (`execve`) |
| **40** | `SYS_EXIT` | `pc_exit()`, `exit()` | Завершение текущего процесса |
| **41** | `SYS_WAIT` | `pc_wait()` | Ожидание завершения дочернего процесса |
| **42** | `SYS_WAITPID` | — | Ожидание конкретного дочернего процесса |
| **43** | `SYS_GETPID` | `pc_getpid()` | Получение идентификатора текущего процесса |
| **44** | `SYS_GETPPID` | — | PID родительского процесса |
| **45** | `SYS_GET_COMMAND_LINE` | `pc_get_command_line()` | Чтение аргументов командной строки процесса |
| **46** | `SYS_GET_PROCESS_NAME` | `pc_get_process_name()` | Имя бинарника/алиаса текущего процесса |
| **47** | `SYS_ENV_GET` | `pc_getenv()`, `getenv()` | Чтение переменной окружения |
| **48** | `SYS_ENV_SET` | `pc_setenv()` | Установка переменной окружения |
| **49** | `SYS_ENV_UNSET` | `pc_unsetenv()` | Удаление переменной окружения |
| **50** | `SYS_ENV_LIST` | `pc_listenv()` | Получение списка всех переменных окружения |
| **51** | `SYS_PROCESS_LIST` | `pc_process_list()` | Список запущенных процессов и статистика |
| **52** | `SYS_TRACE_ATTACH` | — | Подключение трассировки к процессу (strace) |
| **53** | `SYS_TRACE_READ` | — | Чтение записей трассировки |
| **54** | `SYS_TRACE_CONTROL` | — | Управление трассировкой процесса |
| **55** | `SYS_IPC_SEND` | `pc_ipc_send()` | Отправка сообщения другому процессу по PID |
| **56** | `SYS_IPC_RECV` | `pc_ipc_recv()` | Приём сообщения с ожиданием (таймаут в мс) |
| **57** | `SYS_KILL` | — | Отправка сигнала процессу |
| **58** | `SYS_SIGACTION` | — | Установка обработчика сигнала |
| **59** | `SYS_SIGPROCMASK` | — | Маска заблокированных сигналов |
| **60** | `SYS_SIGPENDING` | — | Ожидающие сигналы |
| **61** | `SYS_SIGRETURN` | — | Возврат из обработчика сигнала |
| **62** | `SYS_SIGALTSTACK` | — | Альтернативный стек для обработчиков сигналов |
| **63** | `SYS_PAUSE` | — | Ожидание сигнала |
| **64** | `SYS_THREAD_CREATE` | — | Создание потока |
| **65** | `SYS_THREAD_EXIT` | — | Завершение потока |
| **66** | `SYS_FUTEX` | — | Ожидание и пробуждение по адресу (futex) |
| **67** | `SYS_SET_FS_BASE` | — | Установка `FS_BASE` (TLS) |
| **68** | `SYS_GETTID` | — | Идентификатор потока |
| **69** | `SYS_SCHED_YIELD` | `pc_syscall(SYS_SCHED_YIELD, ...)` | Передача управления следующему процессу |
| **70** | `SYS_SLEEP` | `pc_sleep()`, `sleep()` | Блокировка потока на N миллисекунд |
| **71** | `SYS_MMAP` | — | Отображение памяти (VMA) |
| **72** | `SYS_MUNMAP` | — | Освобождение диапазона памяти |
| **73** | `SYS_MPROTECT` | — | Смена прав доступа к страницам |
| **74** | `SYS_MREMAP` | — | Перемещение и изменение размера отображения |
| **75** | `SYS_MADVISE` | — | Подсказка ядру о поведении памяти |
| **76** | `SYS_HEAP_GROW` | `pc_heap_grow()`, `malloc()` | Увеличение кучи процесса (аналог brk/sbrk) |
| **77** | `SYS_MEMFD_CREATE` | — | Анонимный файл в памяти |
| **78** | `SYS_SHM_OPEN` | — | Открытие объекта общей памяти |
| **79** | `SYS_SHM_UNLINK` | — | Удаление объекта общей памяти |
| **80** | `SYS_POLL` | — | Ожидание готовности дескрипторов |
| **81** | `SYS_EPOLL_CREATE` | — | Создание экземпляра epoll |
| **82** | `SYS_EPOLL_CTL` | — | Добавление, изменение и удаление дескриптора в epoll |
| **83** | `SYS_EPOLL_WAIT` | — | Ожидание событий epoll |
| **84** | `SYS_EVENTFD` | — | Дескриптор-счётчик событий (eventfd) |
| **85** | `SYS_TIMERFD_CREATE` | — | Дескриптор таймера (timerfd) |
| **86** | `SYS_TIMERFD_SETTIME` | — | Настройка таймера |
| **87** | `SYS_TIMERFD_GETTIME` | — | Чтение таймера |
| **88** | `SYS_SOCKET` | — | Создание сокета, возвращает fd |
| **89** | `SYS_SOCKETPAIR` | — | Пара связанных сокетов |
| **90** | `SYS_BIND` | — | Привязка сокета к адресу |
| **91** | `SYS_CONNECT` | — | Подключение сокета |
| **92** | `SYS_LISTEN` | — | Прослушивание сокета |
| **93** | `SYS_ACCEPT` | — | Приём соединения, возвращает fd |
| **94** | `SYS_SOCK_SHUTDOWN` | — | Частичное закрытие сокета |
| **95** | `SYS_SOCKNAME` | — | Адрес сокета (локальный или удалённый) |
| **96** | `SYS_SETSOCKOPT` | — | Установка параметров сокета |
| **97** | `SYS_GETSOCKOPT` | — | Чтение параметров сокета |
| **98** | `SYS_SENDTO` | `sendto()` | Отправка данных сокета (с адресом получателя для UDP) |
| **99** | `SYS_RECVFROM` | `recvfrom()` | Приём данных сокета с адресом отправителя |
| **100** | `SYS_SOCK_SEND` | — | Отправка данных в сокет |
| **101** | `SYS_SOCK_RECV` | — | Приём данных из сокета |
| **102** | `SYS_SENDMSG` | — | Отправка сообщения (в том числе с дескрипторами) |
| **103** | `SYS_RECVMSG` | — | Приём сообщения |
| **104** | `SYS_TCP_SOCKET` | `pc_tcp_socket()` | Создание TCP-сокета (handle) |
| **105** | `SYS_TCP_CONNECT` | `pc_tcp_connect()` | Подключение TCP-сокета по IPv4 и порту |
| **106** | `SYS_TCP_LISTEN` | `pc_tcp_listen()` | Перевод сокета в режим прослушивания |
| **107** | `SYS_TCP_ACCEPT` | `pc_tcp_accept()` | Приём входящего соединения |
| **108** | `SYS_TCP_SEND` | `pc_tcp_send()` | Отправка данных по TCP |
| **109** | `SYS_TCP_RECV` | `pc_tcp_recv()` | Приём данных по TCP (0 — конец потока) |
| **110** | `SYS_TCP_CLOSE` | `pc_tcp_close()` | Закрытие TCP-сокета |
| **111** | `SYS_TCP_ABORT` | `pc_tcp_abort()` | Сброс соединения (RST) и освобождение сокета |
| **112** | `SYS_TCP_INFO` | `pc_tcp_info()` | Состояние TCP-соединения |
| **113** | `SYS_DNS_RESOLVE` | — | Разрешение имени в IPv4 |
| **114** | `SYS_NET_PING` | `pc_ping()` | ICMP Ping хоста по IP или доменному имени |
| **115** | `SYS_NET_IF_LIST` | `pc_net_if_list()` | Список проводных интерфейсов: link/IP/GW/DNS/DHCP/статистика |
| **116** | `SYS_NET_CONFIGURE` | `pc_net_configure()` | Настройка сетевого интерфейса (IP, маска, шлюз, DNS); root или `NET_ADMIN` |
| **117** | `SYS_NET_SOCKETS` | `pc_net_sockets()` | Список сокетов системы (не-root видит только свои) |
| **118** | `SYS_WIFI_SCAN` | `pc_wifi_scan()` | Запуск фонового сканирования Wi-Fi сетей |
| **119** | `SYS_WIFI_LIST` | `pc_wifi_list()` | Получение списка обнаруженных Wi-Fi точек |
| **120** | `SYS_WIFI_CONNECT` | `pc_wifi_connect()` | Подключение к Wi-Fi сети (SSID + пароль) |
| **121** | `SYS_WIFI_DISCONNECT` | `pc_wifi_disconnect()` | Отключение от текущей Wi-Fi сети |
| **122** | `SYS_WIFI_STATUS` | `pc_wifi_status()` | Статус Wi-Fi, IP, MAC и качество сигнала |
| **123** | `SYS_GETUID` | `pc_getuid()` | UID текущего процесса |
| **124** | `SYS_GETGID` | `pc_getgid()` | GID текущего процесса |
| **125** | `SYS_SETUID` | `pc_setuid()` | Смена UID процесса |
| **126** | `SYS_SETGID` | `pc_setgid()` | Смена GID процесса |
| **127** | `SYS_SUDO_REMOVED` | - | Удалён: sudo теперь setuid-программа, слот занят sys_ni_syscall |
| **128** | `SYS_USER_LOOKUP` | `pc_user_lookup()` | Поиск пользователя по имени или UID |
| **129** | `SYS_SESSION_BEGIN` | `pc_session_begin()` | Начало сессии пользователя (только root) |
| **130** | `SYS_FILE_PERM` | `pc_file_perm()` | Владелец и режим файла |
| **131** | `SYS_TTY_READ` | — | Чтение ввода консоли |
| **132** | `SYS_TTY_IOCTL` | `pc_tty_ioctl()` | Управление терминалом (`TTY_*`) |
| **133** | `SYS_OPENPTY` | `pc_openpty()` | Пара псевдотерминала (master, slave) |
| **134** | `SYS_VT_ACTIVATE` | — | Переключение виртуальной консоли (`vtcon`) |
| **135** | `SYS_VT_ACTIVE` | — | Индекс активной виртуальной консоли |
| **136** | `SYS_GETCHAR` | `pc_read_line()`, `getchar()`, `fgetc()` | Блокирующее чтение символа с клавиатуры |
| **137** | `SYS_TRY_GETCHAR` | `pc_try_getchar()` | Неблокирующее чтение символа с клавиатуры |
| **138** | `SYS_TRY_GET_SPECIAL` | `pc_try_get_special()` | Чтение спец. клавиш (стрелки, F-клавиши) |
| **139** | `SYS_KEY_EVENT_GET` | `pc_key_event_get()` | Чтение события клавиатуры |
| **140** | `SYS_GET_MOUSE` | `pc_mouse_get()` | Опрос координат и кнопок мыши |
| **141** | `SYS_MOUSE_DEBUG_GET` | `pc_syscall(SYS_MOUSE_DEBUG_GET, ...)` | Состояние оверлея отладки мыши |
| **142** | `SYS_MOUSE_DEBUG_SET` | `pc_syscall(SYS_MOUSE_DEBUG_SET, ...)` | Включение/выключение отладочного оверлея мыши |
| **143** | `SYS_INPUT_OPEN` | `pc_input_open()` | Поток сырых событий ввода |
| **144** | `SYS_FB_INFO` | `pc_display_get_info()` | Получение параметров видеорежима (GOP) |
| **145** | `SYS_FB_MAP` | `pc_fb_map()` | Отображение видеопамяти в процесс |
| **146** | `SYS_FB_DAMAGE` | `pc_fb_damage()` | Показ области буфера на устройство |
| **147** | `SYS_DISPLAY_SCALE_INFO` | `pc_display_scale_info()` | Масштаб интерфейса дисплея |
| **148** | `SYS_DISPLAY_CLAIM` | `pc_display_claim()` | Захват экрана процессом (видеосервер) |
| **149** | `SYS_DISPLAY_RELEASE` | `pc_display_release()` | Возврат экрана ядру |
| **150** | `SYS_DISPLAY_SET_MODE` | `pc_display_set_mode()`, `pc_display_set_mode_scale()` | Смена разрешения экрана на лету (ширина, высота, bpp, масштаб в %) |
| **151** | ~~`SYS_CLEAR`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **152** | ~~`SYS_DRAW_RECT`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **153** | ~~`SYS_DRAW_LINE`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **154** | ~~`SYS_SCROLL_RECT_UP`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **155** | ~~`SYS_FB_BEGIN_UPDATE`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **156** | ~~`SYS_FB_END_UPDATE`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **157** | ~~`SYS_FB_BLIT`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **158** | ~~`SYS_CONSOLE_CONFIGURE`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **159** | ~~`SYS_CONSOLE_CLEAR`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **160** | ~~`SYS_CONSOLE_DISABLE`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **161** | ~~`SYS_DESKTOP_REDRAW`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **162** | ~~`SYS_DESKTOP_CLAIM`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **163** | ~~`SYS_DESKTOP_BLIT`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **164** | ~~`SYS_DESKTOP_WAIT`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **165** | ~~`SYS_DESKTOP_WM`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **166** | ~~`SYS_GUI_WINDOW_REGISTER`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **167** | ~~`SYS_GUI_WINDOW_UPDATE`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **168** | ~~`SYS_GUI_WINDOW_UNREGISTER`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **169** | ~~`SYS_GUI_WINDOW_STATE`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **170** | ~~`SYS_GUI_WINDOW_REPAINT_DONE`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **171** | ~~`SYS_GUI_WINDOW_INFO`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **172** | ~~`SYS_GUI_WINDOW_FLAGS`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **173** | ~~`SYS_GUI_WINDOW_CMD`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **174** | ~~`SYS_GUI_WINDOW_TITLE`~~ удалён | — | Графика и оконный менеджер удалены из ядра (2026-11-15), вызов возвращает `-KERR_NOSYS` |
| **175** | ~~`SYS_GUI_START`~~ удалён | — | Определён только в заголовке, ядро не обрабатывает |
| **176** | `SYS_AUDIO_GET_STATUS` | `pc_audio_get_status()` | Статус аудиоподсистемы и кодеков |
| **177** | `SYS_AUDIO_GET_VOLUME` | `pc_audio_get_volume()` | Получение уровня громкости (0..100) |
| **178** | `SYS_AUDIO_SET_VOLUME` | `pc_audio_set_volume()` | Установка громкости звука (0..100) |
| **179** | `SYS_AUDIO_IS_MUTED` | `pc_audio_is_muted()` | Проверка отключения звука (mute) |
| **180** | `SYS_AUDIO_SET_MUTED` | `pc_audio_set_muted()` | Включение/выключение звука (mute) |
| **181** | `SYS_AUDIO_ADJUST_VOLUME` | `pc_audio_adjust_volume()` | Относительное изменение громкости (+/- delta) |
| **182** | `SYS_AUDIO_PLAY_TEST_SOUND` | `pc_audio_play_test()` | Проигрывание тестовой звуковой мелодии |
| **183** | `SYS_AUDIO_PLAY_TONE` | `pc_audio_play_tone()` | Воспроизведение звукового тона заданной частоты |
| **184** | `SYS_AUDIO_STOP_TONE` | `pc_audio_stop_tone()` | Остановка воспроизведения тона |
| **185** | `SYS_AUDIO_PCM_PUSH` | `pc_audio_pcm_push()`, `pc_audio_pcm_eos()` | Загрузка PCM сэмплов в звуковой буфер |
| **186** | `SYS_AUDIO_PCM_START` | `pc_audio_pcm_start()` | Запуск воспроизведения цифрового PCM аудио |
| **187** | `SYS_AUDIO_PCM_STOP` | `pc_audio_pcm_stop()` | Немедленная остановка воспроизведения PCM |
| **188** | `SYS_AUDIO_UPDATE` | `pc_syscall(SYS_AUDIO_UPDATE, ...)` | Обновление состояния аудиодвижка |
| **189** | `SYS_AUDIO_SELECT_OUTPUT_DEVICE`| `pc_audio_select_output()` | Переключение аудиовыхода (динамики/наушники) |
| **190** | `SYS_DISK_LIST` | `pc_list_disks()` | Перечисление блочных дисковых устройств |
| **191** | `SYS_DISK_STATS` | `pc_syscall(SYS_DISK_STATS, ...)` | Суммарная статистика подключенных дисков |
| **192** | `SYS_DISK_IO` | `pc_disk_io()` | Прямое чтение/запись секторов диска; право `STORAGE_ADMIN` |
| **193** | `SYS_STORAGE_CONTROLLERS` | `pc_syscall(SYS_STORAGE_CONTROLLERS, ...)` | Список контроллеров хранения (AHCI, XHCI) |
| **194** | `SYS_FS_FORMAT` | `pc_syscall(SYS_FS_FORMAT, ...)` | Форматирование накопителя в ext2 |
| **195** | `SYS_FS_FORMAT_FORCE` | `pc_syscall(SYS_FS_FORMAT_FORCE, ...)` | Принудительное форматирование в ext2 |
| **196** | `SYS_FS_FORMAT_CUSTOM` | `pc_format_custom()` | Форматирование с произвольной разметкой разделов |
| **197** | `SYS_FORMAT_DEVICE_EX` | `pc_format_device_ex()` | Форматирование устройства в FAT32 или ext2 |
| **198** | `SYS_EXT2_STAT` | `pc_ext2_stat()` | Получение stat-информации inode ext2 по пути |
| **199** | `SYS_EXT2_INODE` | `pc_ext2_inode()` | Получение метаданных inode ext2 по номеру |
| **200** | `SYS_EXT2_SUPER` | `pc_ext2_super()` | Чтение структуры суперблока ext2 |
| **201** | `SYS_EXT2_BLOCKS` | `pc_ext2_blocks()` | Список физических блоков файла в ext2 |
| **202** | `SYS_INSTALL_DEVICE` | `pc_syscall(SYS_INSTALL_DEVICE, ...)` | Установка: GPT + ext2 системный раздел (ESP не форматируется) |
| **203** | `SYS_INSTALL_START` | `pc_install_start()` | Запуск асинхронного установщика ОС |
| **204** | `SYS_INSTALL_START_EX` | `pc_install_start_ex()` | Запуск установщика с выбором ФС (FAT32/ext2) |
| **205** | `SYS_INSTALL_STATUS` | `pc_install_status()` | Запрос прогресса и этапа установки ОС |
| **206** | `SYS_INSTALL_LOG` | `pc_install_log()` | Получение истории шагов установщика |
| **207** | `SYS_GET_ROOT_DEVICE` | `pc_get_root_device()` | Имя дискового устройства корня VFS |
| **208** | `SYS_GET_FS_TYPE` | `pc_get_fs_type()` | Тип корневой файловой системы ("FAT32"/"ext2") |
| **209** | `SYS_SAVE_KLOG` | `pc_save_klog()` | Сохранение логов ядра (klog) на накопитель |
| **210** | `SYS_TIME_GET` | `pc_time_get()` | Текущее время и часовой пояс |
| **211** | `SYS_TIME_SET` | `pc_time_set()` | Установка времени (только root) |
| **212** | `SYS_TIME_ZONE` | `pc_time_zone_get()`, `pc_time_zone_set()` | Часовой пояс (0 — чтение, 1 — запись, только root) |
| **213** | `SYS_CLOCK_ADJUST` | `pc_clock_adjust()` | Ступенчатая или плавная коррекция часов, запись в RTC; root или право `SYS_TIME` |
| **214** | `SYS_GETRANDOM` | `pc_getrandom()` | Криптографически стойкие случайные байты |
| **215** | `SYS_CPU_INFO` | `pc_cpu_info()` | Информация о процессоре, загрузке и аптайме |
| **216** | `SYS_CPU_CORE_INFO` | `pc_cpu_core_info()` | Per-core счётчики total/idle ticks для realtime-мониторинга |
| **217** | `SYS_MEMORY_INFO` | `pc_memory_info()` | Статистика RAM (всего, занято, свободно) |
| **218** | `SYS_BATTERY_INFO` | `pc_battery_info()` | Статус батареи ноутбука (ACPI, presence реален, % unknown до AML) |
| **219** | `SYS_THERMAL_INFO` | — | Температура и вентиляторы из ACPI (`struct thermal_info`) |
| **220** | `SYS_REBOOT` | `pc_reboot()` | Перезагрузка компьютера |
| **221** | `SYS_SHUTDOWN` | `pc_shutdown()` | Выключение питания (ACPI / QEMU) |
| **222** | `SYS_KLOG_WRITE` | `syslog()` | Запись строки в klog от имени процесса |
| **223** | `SYS_USB_RESCAN` | `pc_syscall(SYS_USB_RESCAN, ...)` | Повторное сканирование шины USB (XHCI/EHCI) |
| — | ~~`SYS_DRAW_TEXT`~~ удалён | — | Текст рисует userspace-бэкенд `src/gfx/text.h` поверх `SYS_DRAW_RECT`; `pc_draw_text()` в libc сохранён и работает без ядра |
| — | ~~`SYS_DRAW_TEXT_SIZED`~~ удалён | — | Аналогично, `pc_draw_text_sized()` рендерится в userspace |
| — | ~~`SYS_SET_FONT_FACE`~~ удалён | — | Начертание — настройка бэкенда (`gfx_font_face_t`), у ядра шрифтов нет |
| — | ~~`SYS_GET_FONT_FACE`~~ удалён | — | См. выше |

---

## 4. Подробное Описание Вызовов с Примерами

### 4.1. Процессы и Управление Выполнением

#### `SYS_GETPID` (43)
- **Регистры**: `rax = 43`
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

#### `SYS_EXEC` (38)
- **Регистры**: `rax = 38`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)args` (или 0)
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

#### `SYS_EXIT` (40)
- **Регистры**: `rax = 40`, `rdi = (int64_t)status`
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

#### `SYS_WAIT` (41)
- **Регистры**: `rax = 41`, `rdi = (uint32_t)pid`, `rsi = (uintptr_t)status_ptr`, `rdx = nohang` (1 — без блокировки, 0 — с блокировкой)
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

#### `SYS_SCHED_YIELD` (69)
- **Регистры**: `rax = 69`
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

#### `SYS_SLEEP` (70)
- **Регистры**: `rax = 70`, `rdi = milliseconds`
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

#### `SYS_GET_COMMAND_LINE` (45)
- **Регистры**: `rax = 45`, `rdi = (uintptr_t)buffer`, `rsi = capacity`
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

#### `SYS_GET_PROCESS_NAME` (46)
- **Регистры**: `rax = 46`, `rdi = (uintptr_t)buffer`, `rsi = capacity`
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

#### `SYS_PROCESS_LIST` (51)
- **Регистры**: `rax = 51`, `rdi = (uintptr_t)processes`, `rsi = capacity`, `rdx = offset` (0 — с начала)
- **Обертки stdlib**: `int32_t pc_process_list(struct process_monitor_info *processes, uint32_t capacity)` и `int32_t pc_process_list_page(struct process_monitor_info *processes, uint32_t capacity, uint32_t offset)`
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
- **Описание**: Возвращает список всех активных процессов в системе (используется диспетчером задач `/bin/program/monitor`). Таблица процессов динамическая (лимит = свободная RAM). Возвращает полное число процессов; заполняет не больше `capacity` записей. Запрос больше таблицы — не ошибка.
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

#### `SYS_HEAP_GROW` (76)
- **Регистры**: `rax = 76`, `rdi = increment_bytes`
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

#### `SYS_ENV_GET` (47)
- **Регистры**: `rax = 47`, `rdi = (uintptr_t)name`, `rsi = (uintptr_t)buffer`, `rdx = capacity`
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

#### `SYS_ENV_SET` (48)
- **Регистры**: `rax = 48`, `rdi = (uintptr_t)name`, `rsi = (uintptr_t)value`
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

#### `SYS_ENV_UNSET` (49)
- **Регистры**: `rax = 49`, `rdi = (uintptr_t)name`
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

#### `SYS_ENV_LIST` (50)
- **Регистры**: `rax = 50`, `rdi = (uintptr_t)variables`, `rsi = capacity`
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

#### `SYS_FILE_OPEN` / `SYS_OPEN` (24)
- **Регистры**: `rax = 24`, `rdi = (uintptr_t)path`
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

#### `SYS_FILE_READ` / `SYS_READ` (25)
- **Регистры**: `rax = 25`, `rdi = (uint32_t)descriptor`, `rsi = (uintptr_t)buffer`, `rdx = capacity`
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

#### `SYS_FILE_WRITE` (26)
- **Регистры**: `rax = 26`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)buffer`, `rdx = size`
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

#### `SYS_FILE_APPEND` (27)
- **Регистры**: `rax = 27`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)buffer`, `rdx = size`
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

#### `SYS_FILE_SEEK` (28)
- **Регистры**: `rax = 28`, `rdi = (uint32_t)descriptor`, `rsi = (uint64_t)offset`, `rdx = (uint32_t)whence` (0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END)
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

#### `SYS_FILE_STAT` (29)
- **Регистры**: `rax = 29`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)out`
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

#### `SYS_FILE_CLOSE` / `SYS_CLOSE` (30)
- **Регистры**: `rax = 30`, `rdi = (uint32_t)descriptor`
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

#### `SYS_FILE_CREATE` (33)
- **Регистры**: `rax = 33`, `rdi = (uintptr_t)path`
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

#### `SYS_FILE_DELETE` / `SYS_UNLINK` (31)
- **Регистры**: `rax = 31`, `rdi = (uintptr_t)path`
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

#### `SYS_FILE_RENAME` / `SYS_RENAME` (32)
- **Регистры**: `rax = 32`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)new_name`
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

#### `SYS_FILE_MOVE` (23)
- **Регистры**: `rax = 23`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)destination_dir`
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

#### `SYS_DIR_CREATE` / `SYS_MKDIR` (34)
- **Регистры**: `rax = 34`, `rdi = (uintptr_t)path`
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

#### `SYS_DIR_LIST` (35)
- **Регистры**: `rax = 35`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)entries`, `rdx = capacity`
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

#### `SYS_DIR_LIST_LONG` (36)
- **Регистры**: `rax = 36`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)entries`, `rdx = capacity`
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

#### `SYS_GET_ROOT_DEVICE` (207)
- **Регистры**: `rax = 207`, `rdi = (uintptr_t)buffer`, `rsi = capacity`
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

#### `SYS_GET_FS_TYPE` (208)
- **Регистры**: `rax = 208`, `rdi = (uintptr_t)buffer`, `rsi = capacity`
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

#### `SYS_RENAME_PATH` (22)
- **Регистры**: `rax = 22`, `rdi = old_path`, `rsi = new_path`
- **Обертка stdlib**: `int rename(const char *old_path, const char *new_path)`
- **Описание**: Переименовывает или перемещает путь. Пути разрешаются относительно текущего каталога процесса.
- **Возврат**: `0` или код `FS_ERROR_*`.

#### `SYS_SYNC` (14)
- **Регистры**: `rax = 14`
- **Обертка stdlib**: `int32_t pc_sync(void)`
- **Описание**: Сбрасывает буферы файловой системы (буферный кэш) на диск.
- **Возврат**: `0` или `-KERR_IO`.

---

### 4.5. Диагностика и Метаданные ext2

#### `SYS_EXT2_STAT` (198)
- **Регистры**: `rax = 198`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)out`
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

#### `SYS_EXT2_INODE` (199)
- **Регистры**: `rax = 199`, `rdi = ino`, `rsi = (uintptr_t)out`
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

#### `SYS_EXT2_SUPER` (200)
- **Регистры**: `rax = 200`, `rdi = (uintptr_t)out`
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

#### `SYS_EXT2_BLOCKS` (201)
- **Регистры**: `rax = 201`, `rdi = (uintptr_t)path`, `rsi = (uintptr_t)out`
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

#### `SYS_WRITE` (8)
- **Регистры**: `rax = 8`, `rdi = (uintptr_t)buffer`, `rsi = length`, `rdx = 1`
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

#### `SYS_GETCHAR` (136)
- **Регистры**: `rax = 136`
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

#### `SYS_TRY_GETCHAR` (137)
- **Регистры**: `rax = 137`
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

#### `SYS_TRY_GET_SPECIAL` (138)
- **Регистры**: `rax = 138`
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

#### `SYS_VT_ACTIVATE` (134)
- **Регистры**: `rax = 134`, `rdi = индекс консоли`
- **Описание**: Переключает активную виртуальную консоль (`vtcon`, tty1–tty6). Root может переключать любую консоль; остальные процессы — только если их stdin принадлежит pty этой консоли.
- **Возврат**: `0` или `-KERR_BUSY`, если переключение не удалось.

#### `SYS_VT_ACTIVE` (135)
- **Регистры**: `rax = 135`
- **Описание**: Возвращает индекс текущей активной виртуальной консоли.

---

### 4.7. Framebuffer (GOP)

#### `SYS_FB_INFO` (144)
- **Регистры**: `rax = 144`, `rdi = (uintptr_t)info`
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

### 4.8. Удалённые вызовы (графика и PureGUI)

С 2026-11-15 графика ядра, композитор, стол и оконный менеджер удалены из ядра и из дерева `src/userspace`. Номера остались в `src/kernel/syscall/syscall.h`, но обработчика в `syscall.c` нет: вызов возвращает `-KERR_NOSYS`. Обёртки в libc могут остаться, но вызов из них получает ту же ошибку.

- **Графика ядра**: `SYS_CLEAR` (151), `SYS_DRAW_RECT` (152), `SYS_DRAW_LINE` (153), `SYS_SCROLL_RECT_UP` (154), `SYS_FB_BEGIN_UPDATE` (155), `SYS_FB_END_UPDATE` (156), `SYS_FB_BLIT` (157)
- **Текстовая консоль окна**: `SYS_CONSOLE_CONFIGURE` (158), `SYS_CONSOLE_CLEAR` (159), `SYS_CONSOLE_DISABLE` (160)
- **Стол и композитор**: `SYS_DESKTOP_REDRAW` (161), `SYS_DESKTOP_CLAIM` (162), `SYS_DESKTOP_BLIT` (163), `SYS_DESKTOP_WAIT` (164), `SYS_DESKTOP_WM` (165), `SYS_GUI_START` (175)
- **PureGUI (окна)**: `SYS_GUI_WINDOW_REGISTER` (166), `SYS_GUI_WINDOW_UPDATE` (167), `SYS_GUI_WINDOW_UNREGISTER` (168), `SYS_GUI_WINDOW_STATE` (169), `SYS_GUI_WINDOW_REPAINT_DONE` (170), `SYS_GUI_WINDOW_INFO` (171), `SYS_GUI_WINDOW_FLAGS` (172), `SYS_GUI_WINDOW_CMD` (173), `SYS_GUI_WINDOW_TITLE` (174)

Окна, стол и вход в графическую среду теперь реализуются отдельными пакетами (видеосервер `videod` и клиенты через `pkg`), поверх `SYS_DISPLAY_CLAIM` / `SYS_FB_MAP` / `SYS_INPUT_OPEN` (раздел 4.15).

### 4.9. Манипулятор Мышь

#### `SYS_GET_MOUSE` (140)
- **Регистры**: `rax = 140`, `rdi = (uintptr_t)state`
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

#### `SYS_MOUSE_DEBUG_GET` (141) и `SYS_MOUSE_DEBUG_SET` (142)
- **Регистры**:
  - `SYS_MOUSE_DEBUG_GET`: `rax = 141`
  - `SYS_MOUSE_DEBUG_SET`: `rax = 142`, `rdi = enabled` (1 / 0)
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

#### `SYS_AUDIO_PLAY_TONE` (183) и `SYS_AUDIO_STOP_TONE` (184)
- **Регистры**:
  - `SYS_AUDIO_PLAY_TONE`: `rax = 183`, `rdi = freq_hz` (30..8000), `rsi = duration_ms` (1..5000)
  - `SYS_AUDIO_STOP_TONE`: `rax = 184`
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

#### `SYS_AUDIO_GET_STATUS` (176)
- **Регистры**: `rax = 176`, `rdi = (uintptr_t)status`
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

#### `SYS_AUDIO_GET_VOLUME` (177), `SYS_AUDIO_SET_VOLUME` (178), `SYS_AUDIO_ADJUST_VOLUME` (181)
- **Регистры**:
  - `SYS_AUDIO_GET_VOLUME`: `rax = 177`
  - `SYS_AUDIO_SET_VOLUME`: `rax = 178`, `rdi = volume` (0..100)
  - `SYS_AUDIO_ADJUST_VOLUME`: `rax = 181`, `rdi = delta` (signed)
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

#### `SYS_AUDIO_IS_MUTED` (179) и `SYS_AUDIO_SET_MUTED` (180)
- **Регистры**:
  - `SYS_AUDIO_IS_MUTED`: `rax = 179`
  - `SYS_AUDIO_SET_MUTED`: `rax = 180`, `rdi = muted` (1 / 0)
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

#### `SYS_AUDIO_PLAY_TEST_SOUND` (182)
- **Регистры**: `rax = 182`
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

#### `SYS_AUDIO_SELECT_OUTPUT_DEVICE` (189)
- **Регистры**: `rax = 189`, `rdi = index`
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

#### `SYS_AUDIO_PCM_PUSH` (185), `SYS_AUDIO_PCM_START` (186), `SYS_AUDIO_PCM_STOP` (187)
- **Регистры**:
  - `SYS_AUDIO_PCM_PUSH`: `rax = 185`, `rdi = (uintptr_t)samples`, `rsi = frame_count` (0 = EOS)
  - `SYS_AUDIO_PCM_START`: `rax = 186`
  - `SYS_AUDIO_PCM_STOP`: `rax = 187`
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

#### `SYS_NET_PING` (114)
- **Регистры**: `rax = 114`, `rdi = (uintptr_t)req`, `rsi = (uintptr_t)res`
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

#### `SYS_WIFI_SCAN` (118), `SYS_WIFI_LIST` (119), `SYS_WIFI_STATUS` (122)
- **Регистры**:
  - `SYS_WIFI_SCAN`: `rax = 118`
  - `SYS_WIFI_LIST`: `rax = 119`, `rdi = (uintptr_t)networks`, `rsi = capacity`
  - `SYS_WIFI_STATUS`: `rax = 122`, `rdi = (uintptr_t)status`
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

#### `SYS_WIFI_CONNECT` (120) и `SYS_WIFI_DISCONNECT` (121)
- **Регистры**:
  - `SYS_WIFI_CONNECT`: `rax = 120`, `rdi = (uintptr_t)req`
  - `SYS_WIFI_DISCONNECT`: `rax = 121`
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

#### `SYS_SENDTO` (98) и `SYS_RECVFROM` (99)
- **Регистры**: `rax = 98/404`, `rdi = fd`, `rsi = buffer`, `rdx = length`, `r10 = flags`, `r8 = struct sockaddr_in *` (необязательно)
- **Обертки stdlib**: `sendto()`, `recvfrom()` (POSIX, `<sys/socket.h>`)
- **Описание**: Отправка и приём данных сокета с адресом. Адрес можно не передавать (`rdi = 0`), тогда работает как `send()` / `recv()`.
- **Возврат**: число переданных байт или `-errno`.

#### `SYS_NET_CONFIGURE` (116)
- **Регистры**: `rax = 116`, `rdi = struct net_if_config *`
- **Обертка stdlib**: `int32_t pc_net_configure(const struct net_if_config *config)`
- **Описание**: Меняет параметры сетевого интерфейса (`name`, `address`, `netmask`, `gateway`, `dns`, код операции `op`). Требует root или право `NET_ADMIN`.
- **Возврат**: `0` или код ошибки.

#### `SYS_NET_SOCKETS` (117)
- **Регистры**: `rax = 117`, `rdi = struct net_socket_entry *`, `rsi = capacity` (до 512), `rdx = offset`
- **Обертка stdlib**: `int32_t pc_net_sockets(struct net_socket_entry *out, uint32_t capacity, uint32_t offset)`
- **Описание**: Список сокетов системы. Для процессов не из root записи чужих пользователей обнуляются (pid, fd, имя процесса).
- **Возврат**: число записей.

---

### 4.12. Накопители, Форматирование и Установка

#### `SYS_DISK_LIST` (190)
- **Регистры**: `rax = 190`, `rdi = (uintptr_t)devices`, `rsi = capacity`
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

#### `SYS_STORAGE_CONTROLLERS` (193)
- **Регистры**: `rax = 193`, `rdi = (uintptr_t)info`, `rsi = capacity`
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

#### `SYS_DISK_STATS` (191)
- **Регистры**: `rax = 191`, `rdi = (uintptr_t)info`
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

#### `SYS_USB_RESCAN` (223)
- **Регистры**: `rax = 223`, `rdi = (uintptr_t)status` (или 0)
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

#### `SYS_FORMAT_DEVICE_EX` (197) и `SYS_FS_FORMAT_CUSTOM` (196)
- **Регистры**:
  - `SYS_FORMAT_DEVICE_EX`: `rax = 197`, `rdi = (uintptr_t)req`
  - `SYS_FS_FORMAT_CUSTOM`: `rax = 196`, `rdi = (uintptr_t)req`
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

#### `SYS_INSTALL_START` (203), `SYS_INSTALL_START_EX` (204), `SYS_INSTALL_STATUS` (205), `SYS_INSTALL_LOG` (206)
- **Регистры**:
  - `SYS_INSTALL_START`: `rax = 203`, `rdi = (uintptr_t)device`, `rsi = (uintptr_t)serial`
  - `SYS_INSTALL_START_EX`: `rax = 204`, `rdi = (uintptr_t)req`
  - `SYS_INSTALL_STATUS`: `rax = 205`, `rdi = (uintptr_t)status`
  - `SYS_INSTALL_LOG`: `rax = 206`, `rdi = (uintptr_t)log`
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

#### `SYS_DISK_IO` (192)
- **Регистры**: `rax = 192`, `rdi = struct disk_io_request *`
- **Обертка stdlib**: `int32_t pc_disk_io(const char *device, uint64_t lba, void *buffer, uint32_t count, bool write)`
- **Структура**: `char device[16]`, `uint64_t lba`, `uint64_t buffer`, `uint32_t count` (не более 128 секторов), `uint32_t write`.
- **Описание**: Прямое чтение или запись секторов блочного устройства, минуя VFS. Требует право `STORAGE_ADMIN`.
- **Возврат**: `0` или код ошибки.

---

### 4.13. Управление Питанием и Системная Информация

#### `SYS_CPU_INFO` (215)
- **Регистры**: `rax = 215`, `rdi = (uintptr_t)info`
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

#### `SYS_MEMORY_INFO` (217)
- **Регистры**: `rax = 217`, `rdi = (uintptr_t)info`
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

#### `SYS_CPU_CORE_INFO` (216)
- **Регистры**: `rax = 216`, `rdi = (uintptr_t)info`
- **Обертка stdlib**: `int32_t pc_cpu_core_info(struct cpu_core_info *info)`
- **Структура**:
```c
#define CPU_CORE_MAX_COUNT 16
struct cpu_core_entry {
    uint32_t id;
    uint32_t online;      // 1 = online/idle, 0 = parked/offline
    uint64_t total_ticks; // scheduler ticks на ядре
    uint64_t idle_ticks;  // из них idle
};
struct cpu_core_info {
    uint32_t count;
    uint32_t reserved;
    struct cpu_core_entry cores[CPU_CORE_MAX_COUNT];
};
```
- **Описание**: Возвращает сырые счётчики для realtime-расчёта: `usage = 100 - idle_delta*100/total_delta` между двумя вызовами (в `monitor` интервал 500мс, в `cpuinfo/htop` 200мс).
- **Пример**:
```c
#include <purec.h>

static uint32_t core_pct(uint64_t ta, uint64_t ia, uint64_t tb, uint64_t ib){
    uint64_t e = tb > ta ? tb - ta : 0;
    uint64_t ie = ib > ia ? ib - ia : 0;
    if(!e) return 0;
    if(ie > e) ie = e;
    return 100 - (uint32_t)((ie*100)/e);
}

void show_cores(void){
    struct cpu_core_info a={0}, b={0};
    if(pc_cpu_core_info(&a) < 0) return;
    pc_sleep(200);
    if(pc_cpu_core_info(&b) < 0) return;
    for(uint32_t i=0;i<b.count;i++){
        uint32_t u = core_pct(a.cores[i].total_ticks, a.cores[i].idle_ticks,
                              b.cores[i].total_ticks, b.cores[i].idle_ticks);
        pc_write("cpu"); pc_write_u64(b.cores[i].id);
        pc_write(b.cores[i].online ? ": " : " offline\n");
        if(b.cores[i].online){ pc_write_u64(u); pc_write("%\n"); }
    }
}
```

---

#### `SYS_BATTERY_INFO` (218)
- **Регистры**: `rax = 218`, `rdi = (uintptr_t)out`
- **Структура**:
```c
struct battery_info {
    uint32_t present;
    uint32_t percent; // BATTERY_PERCENT_UNKNOWN (0xFFFFFFFF), пока нет AML executor (_BST)
    uint32_t charging; // 1 = на сети, 0 = от батареи/неизвестно (см. status_text)
    uint32_t remaining_minutes; // 0 = неизвестно
    uint32_t voltage_mv; // 0 = неизвестно
    uint32_t current_ma; // 0 = неизвестно
    char name[32];
    char status_text[32]; // "Charging"/"Discharging"/"Charged"/"Unknown"/"No battery"
};
```
- **Обёртка**: `bool pc_battery_info(struct battery_info *info)`
- **Пример**:
```c
#include <purec.h>

void show_battery(void) {
    struct battery_info bat;
    if (pc_battery_info(&bat) && bat.present) {
        pc_write("Заряд батареи: ");
        if (bat.percent == BATTERY_PERCENT_UNKNOWN)
            pc_write("unknown");
        else
            pc_write_u64(bat.percent);
        pc_write(" (");
        pc_write(bat.status_text);
        pc_write(")\n");
    }
}
```

---

#### `SYS_DISPLAY_SET_MODE` (150) и `SYS_THERMAL_INFO` (219)
- **Регистры**:
  - `SYS_DISPLAY_SET_MODE`: `rax = 150`, `rdi = width`, `rsi = height`, `rdx = bpp`, `r10 = scale_percent` (0 — без масштаба)
  - `SYS_THERMAL_INFO`: `rax = 219`, `rdi = (uintptr_t)out`
- **Структуры**:
```c
struct thermal_info {
    uint32_t available;
    uint32_t thermal_zone_count;
    uint32_t fan_count;
    int32_t temperature_deci_c; // десятые доли °C
    uint32_t fan_rpm;
    char thermal_zone[32];
    char fan[32];
};
```
- **Возврат**: `SYS_DISPLAY_SET_MODE` — результат `display_mode_apply()`; `SYS_THERMAL_INFO` — 0 при успехе, -1 если данных нет или буфер недоступен.
- **Обёртки**:
  - `int32_t pc_display_set_mode(uint32_t width, uint32_t height, uint32_t bpp)`
  - `int32_t pc_display_set_mode_scale(uint32_t width, uint32_t height, uint32_t bpp, uint32_t scale_percent)`
  - у `SYS_THERMAL_INFO` обёртки в libc нет, вызывается через `pc_syscall()`.

---

#### `SYS_REBOOT` (220) и `SYS_SHUTDOWN` (221)
- **Регистры**:
  - `SYS_REBOOT`: `rax = 220`
  - `SYS_SHUTDOWN`: `rax = 221`
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

#### `SYS_SAVE_KLOG` (209)
- **Регистры**: `rax = 209`, `rdi = (uintptr_t)req`
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



#### `SYS_KLOG_WRITE` (222)
- **Регистры**: `rax = 222`, `rdi = уровень` (0 — INFO, 1 — OK, 2 — WARN, 3 — ERROR), `rsi = строка`
- **Обертка stdlib**: `syslog()`
- **Описание**: Пишет строку в klog в виде `имя[pid]: текст`. Переводы строк заменяются пробелами, строка обрезается до 239 символов.
- **Возврат**: `0`.

#### `SYS_CLOCK_ADJUST` (213)
- **Регистры**: `rax = 213`, `rdi = операция`, `rsi = delta_ms`, `rdx = int64_t *previous` (необязательно)
- **Обертка stdlib**: `int32_t pc_clock_adjust(uint32_t op, int64_t delta_ms, int64_t *previous_ms)`
- **Операции**: `CLOCK_ADJ_STEP` (0) — ступенчатая установка, `CLOCK_ADJ_SLEW` (1) — плавная коррекция, `CLOCK_ADJ_QUERY` (2) — только чтение, `CLOCK_ADJ_SAVE_RTC` (3) — сохранить время в RTC.
- **Права**: root или право `SYS_TIME`.
- **Возврат**: `0` или код ошибки; в `previous` — предыдущее значение остатка коррекции (мс).

---

### 4.14. Права Доступа и sudo

#### `SYS_SUDO` (127)
- **Регистры**: `rax = 127`, `rdi = (uintptr_t)password` (строка C с нулевым байтом в конце)
- **Обертка stdlib**: `int32_t pc_sudo(const char *password)`
- **Возврат**: `0` — пароль верный, процесс стал root (uid 0, gid 0); `-1` — неверный пароль; `-2` — пользователь не входит в группу `wheel`.
- **Описание**: Ядро по uid находит имя пользователя, проверяет членство в группе `wheel` (по списку членов в `/etc/group` или по gid записи пользователя) и сверяет пароль с `/etc/shadow` через purecrypt (SHA-512). При отказе процесс засыпает на 1,5 с, а каждая попытка записывается в журнал ядра. Исходники: `src/kernel/syscall/sudo.c`. Утилита `/bin/sudo` построена поверх этого вызова.
- **Пример**:
```c
#include <purec.h>

int elevate_to_root(const char *password) {
    int32_t status = pc_sudo(password);
    if (status == 0) {
        pc_write("Права root получены\n");
    } else if (status == -2) {
        pc_write("Пользователь не входит в группу wheel\n");
    } else {
        pc_write("Неверный пароль\n");
    }
    return status;
}
```

---

### 4.15. Экран и Ввод (видеосервер, этап 2)

Вызовы этого раздела нужны userspace-видеосервису (`videod`, пакет video-backend). Процесс с правом `PROCESS_CAP_DISPLAY` (бит `1<<2`) или root забирает экран через `SYS_DISPLAY_CLAIM`. Пока экран занят, ядро не пишет в видеопамять: стол, окна, курсор и klog не выводятся. Ядро возвращает экран само, если владелец завершился или сделал `exec`, а при панике отбирает его первым делом (`gop_kernel_reclaim`). Страницы видеопамяти помечены `VMM_PAGE_DEVICE`: они не освобождаются, не уходят в своп и не учитываются при выборе жертвы OOM.

#### `SYS_DISPLAY_CLAIM` (148)
- **Регистры**: `rax = 148`
- **Обертка stdlib**: `int32_t pc_display_claim(void)`
- **Возврат**: `0` — экран захвачен; `-EBUSY` (`KERR_BUSY` = 16) — экран уже занят другим процессом.
- **Описание**: Переводит вывод на экран в режим владельца. Ядро при этом не реагирует на ввод, а пересылает события владельцу через `SYS_INPUT_OPEN`.
- **Пример**:
```c
#include <purec.h>

int32_t take_screen(void) {
    return pc_display_claim();
}
```

#### `SYS_FB_MAP` (145)
- **Регистры**: `rax = 145`, `rdi = (uintptr_t)out`
- **Обертка stdlib**: `int32_t pc_fb_map(struct fb_map_info *out)`
- **Структура**:
```c
struct fb_map_info {
    uint64_t address;
    uint64_t bytes;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_bytes;
    uint32_t bpp;
    uint32_t generation;
    uint32_t reserved;
};
```
- **Описание**: Отображает фронтовый буфер видеопамяти в адресное пространство владельца. Работает только для 32 bpp. Размеры и шаг строки (`pitch_bytes`) берутся из заполненной структуры.
- **Пример**:
```c
#include <purec.h>

int32_t map_screen(struct fb_map_info *info) {
    if (pc_fb_map(info) != 0) {
        return -1;
    }
    uint32_t *pixels = (uint32_t *)(uintptr_t)info->address;
    pixels[0] = 0x00FF0000;
    return 0;
}
```

#### `SYS_INPUT_OPEN` (143)
- **Регистры**: `rax = 143`
- **Обертка stdlib**: `int32_t pc_input_open(void)`
- **Возврат**: новый дескриптор типа `FD_KIND_INPUT` (6) либо код ошибки. Дескриптор доступен только владельцу экрана, открыть его можно только один раз одновременно.
- **Структура события** (40 байт):
```c
#define INPUT_RECORD_KEY   1
#define INPUT_RECORD_MOUSE 2

struct input_record {
    uint32_t type;
    uint32_t time_ms;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    int32_t wheel;
    uint32_t buttons;
    uint16_t scancode;
    uint8_t pressed;
    uint8_t repeat;
    uint8_t mods;
    uint8_t reserved[3];
};
```
- **Описание**: Из дескриптора читаются сырые события `struct input_record` через `read`, а также через `poll` и `epoll`. Для клавиш `scancode` содержит HID-код (usage). Для мыши заполнены координаты, смещение, колесо и кнопки.
- **Пример**:
```c
#include <purec.h>
#include <unistd.h>

void read_one_event(void) {
    int32_t fd = pc_input_open();
    if (fd < 0) {
        return;
    }
    struct input_record record;
    if (read(fd, &record, sizeof(record)) == sizeof(record) && record.type == INPUT_RECORD_KEY && record.pressed) {
        pc_write("Нажата клавиша\n");
    }
    close(fd);
}
```

#### `SYS_FB_DAMAGE` (146)
- **Регистры**: `rax = 146`, `rdi = x`, `rsi = y`, `rdx = w`, `r10 = h`
- **Обертка stdlib**: `int32_t pc_fb_damage(uint32_t x, uint32_t y, uint32_t w, uint32_t h)`
- **Возврат**: `0` при успехе.
- **Описание**: Сообщает ядру, какая прямоугольная область фронтового буфера изменилась, и отправляет её на устройство. Нужен для VMSVGA, где без такого сообщения изменения на экране не появляются.
- **Пример**:
```c
#include <purec.h>

void flush_frame(uint32_t width, uint32_t height) {
    pc_fb_damage(0, 0, width, height);
}
```

#### `SYS_DISPLAY_RELEASE` (149)
- **Регистры**: `rax = 149`
- **Обертка stdlib**: `int32_t pc_display_release(void)`
- **Возврат**: `0`.
- **Описание**: Возвращает экран ядру: вывод стола, окон и klog снова идёт на экран.
- **Пример**:
```c
#include <purec.h>

void give_screen_back(void) {
    pc_display_release();
}
```

---

## TCP-сокеты (300–308)

Стек TCP живёт в ядре (`src/net/transport/tcp.c`); сокет принадлежит создавшему
процессу и закрывается (RST) при его завершении. В `rbx` для блокирующих вызовов
упаковано `handle | timeout_ms << 32` (0 — не блокировать, `0xFFFFFFFF` — ждать
бесконечно). Ошибки: -1 invalid, -2 нет сокетов, -3 не подключён, -4 reset,
-5 таймаут, -6 нет маршрута, -7 порт занят, -8 would block.

| № | Имя | Аргументы (rbx, rcx, rdx) | Результат |
|---|-----|---------------------------|-----------|
| **104** | `SYS_TCP_SOCKET` | — | handle |
| **105** | `SYS_TCP_CONNECT` | handle+timeout, ipv4, port | 0 |
| **106** | `SYS_TCP_LISTEN` | handle, port, backlog (≤4) | 0 |
| **107** | `SYS_TCP_ACCEPT` | handle+timeout | новый handle |
| **108** | `SYS_TCP_SEND` | handle+timeout, buf, len | принято байт |
| **109** | `SYS_TCP_RECV` | handle+timeout, buf, len | байт, 0 = EOF |
| **110** | `SYS_TCP_CLOSE` | handle | 0 |
| **111** | `SYS_TCP_ABORT` | handle | 0 |
| **112** | `SYS_TCP_INFO` | handle, `struct net_tcp_info *` | 0 |

Обёртки: `pc_tcp_*` в `libc/include/purec.h`.
