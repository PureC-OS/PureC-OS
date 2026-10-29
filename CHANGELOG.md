# Changelog

## V1.2.28 — Права доступа и аккаунты, новый установщик, драйвер RNG, ext2 без заглушек, VFS на таблице монтирования, драйверы железа

### VFS на таблице монтирования (`src/fs/vfs*.c`, `vfs_ops.h`)
- VFS переписан на таблицу монтирования: `vfs_mount.c` (до 16 точек, `vfs_mount_find`
  по самому длинному префиксу), `vfs_path.c` (нормализация путей), `vfs_ops.h`
  (`struct vfs_fs_ops`: open/read/write/seek/stat/list/create/unlink/rename/move/append),
  `vfs_backends.c` (ext2, initramfs, kernelfs).
- Новые файловые системы: `devfs` (`/dev`: null, zero, full, random, urandom, kmsg
  и блочные устройства), `procfs` (`/proc`, общий форматтер `src/lib/format.c`),
  `sysfs` (`/sys`), `tmpfs` в RAM (`/tmp`, `/run`; в live-режиме ещё `/var`, `/home`, `/root`).
- FHS-раскладка: манифест `assets/manifest.txt`, `/etc` из `rootfs/etc`
  (`passwd`, `group`, `hostname`, `fstab`, `motd`, `os-release`, `shells`),
  таблица алиасов старых путей `vfs_alias.c`, самотест `vfs_selftest()` при загрузке.
- Модули и прошивки едут в `/lib/modules` и `/lib/firmware`; `gen_assets.py` и
  `limine.conf.in` подхватывают модули динамически.
- Исправлено: `vfs_append_file` возвращает реально записанные байты; листинг
  initramfs дополняет путь каталога слэшем; `DIR_CAP` рабочего стола 32 → 128
  (раньше часть ярлыков пропадала); `block_device_is_ready()` для `sysfs`.

### ext2 без заглушек (`src/fs/ext2/`)
- `ext2_delete`, `ext2_rename`, `ext2_move` раньше возвращали `-10`. Теперь это
  настоящие операции (`meta.c`): удаление файлов и пустых каталогов с освобождением
  блоков и inode, переименование в каталоге, перенос между каталогами с
  обновлением `..` и счётчиков ссылок, защита от переноса каталога внутрь себя
  и от удаления служебных inode.
- Владелец, группа и режим читаются и пишутся в inode (`ext2_meta_get/set`),
  включая 32-битные uid/gid через `uid_high/gid_high`.
- `ext2_append_file` с тестом на файле больше 12+256+256² блоков.
- Исправлена паника ядра на bare metal при персонализации после установки на ext2.
- Исправлены ошибки inode: смещение 24 — это `i_gid`, а не счётчик ссылок; код
  писал туда 1 и 2, и у всех файлов был gid=1, а `links_count` оставался 0
  (`e2fsck` считал такие inode удалёнными). Теперь счётчик ссылок на смещении 26
  (файл 1, каталог 2, корень 2), gid 0. Перезапись файла больше не сбрасывает
  режим в 0644: владелец, группа и права сохраняются.
- Тесты: `tests/ext2_meta_test.c` (`make test-ext2-meta`) — после удаления всего
  число свободных блоков возвращается к исходному; `tests/ext2_append_test.c`.

### Права доступа и пользователи (`src/fs/vfs_perm.c`, ядро, libc)
- Проверка rwx по владельцу, группе и остальным в open/create/mkdir/write/append/
  delete/rename/move; root обходит проверки. Новая ошибка `FS_ERROR_DENIED`.
  Новый файл получает владельцем создателя; `/tmp` и `/run` создаются с режимом 1777.
- Права хранятся в inode на ext2 (через `get_meta/set_meta` в `vfs_fs_ops`),
  на остальных ФС — в таблице в памяти.
- У процесса появились `uid` и `gid`, наследуются от родителя; сменить их может
  только root.
- Syscalls 309–318: `getuid`, `getgid`, `setuid`, `setgid`, `chmod`, `chown`,
  `file_perm`, `user_lookup`, `getrandom`, `session_begin`. Обёртки в libc:
  `pc_getuid`, `pc_chmod`, `pc_chown`, `pc_file_perm`, `pc_user_lookup`,
  `pc_getrandom`, `pc_session_begin`.
- Разбор `/etc/passwd` (7 полей) в `vfs_perm.c`; тест `tests/vfs_perm_test.c`.

### Драйвер случайных чисел (`src/drivers/random/`, `src/kernel/random/`)
- Модуль `random.so`: Hash_DRBG по SP 800-90A на `pure_sha512` из `libxcrypt`
  (`random_core.c`). Энтропия — RDRAND + джиттер TSC через SHA-512, reseed каждые
  4096 запросов и при записи в `/dev/random`.
- Фасад ядра `krandom.c` грузит модуль после `devman_autoload`, проверяет версию
  ABI; если модуля нет — запасной xorshift (в логе `random: fallback backend`).
- `/dev/random` и `/dev/urandom` читают из DRBG; новый syscall `SYS_GETRANDOM`.
- `devman_load_module()` — загрузка `.so`, не привязанных к устройству.
- Тест `tests/hdrbg_test.c` (`make test-hdrbg`): сверка с независимой реализацией
  на `hashlib`, включая reseed.

### Криптография вне ядра (`libxcrypt`, `mk/main.mk`)
- `libxcrypt` больше не линкуется в образ ядра (там он не использовался);
  собирается как библиотека `bin/lib/libpurecrypt.a` для `login` и установщика.
- Исправлена ошибка `purecrypt_verify`: соль, начинающаяся с `r`, принималась за
  префикс `rounds=` и корректный хеш отвергался (`-1`). Регрессия в `test_crypt.c`.

### Новый установщик (`src/programs/installer/`, `src/fs/install*.c`)
- Шаги: диск → имя компьютера → пароль root (дважды, ввод скрыт) → обычный
  пользователь (имя, полное имя, пароль) → сводка и подтверждение `ERASE`;
  на сводке `E` возвращает к настройкам.
- Пароли хешируются в ring 3 (`$6$`, соль из `getrandom`), в ядро уходят только
  хеши (`struct install_account_request` в `SYS_INSTALL_START_EX`).
- Ядро после копирования системы пишет `/etc/passwd`, `/etc/shadow` (0600),
  `/etc/group`, `/etc/hostname`, `/root` (0700) и `/home/<user>` (0700, владелец —
  пользователь). Запрос валидируется (`install_accounts.c`): имена, хеши, запрет
  `:` и переводов строк, пользователь не может быть `root`.
- Тесты: `tests/install_accounts_test.c`, `tests/accounts_test.c`.

### Экран входа (`src/programs/login/`, `src/programs/accounts/`)
- Поля имени и пароля (Tab, мышь, Enter), проверка по `/etc/shadow` через
  `purecrypt_verify`, растущая задержка после неудач.
- Успешный вход вызывает `SYS_SESSION_BEGIN`: программы, запускаемые от десктопа
  (root-родитель, кроме init), получают uid/gid пользователя.
- Без `/etc/shadow` (live-ISO) остаётся вход одной кнопкой как root.

### Часы и время (`src/kernel/time/`, libc)
- Раньше `time()` считал секунды от загрузки, десктопные часы жили в
  `/purec/datetime.cfg`. Теперь есть настоящие часы ядра: `rtc.c` читает CMOS RTC
  (ожидание конца обновления, двойное чтение до совпадения, BCD/двоичный
  режим, 12/24 часа, век), `wallclock.c` держит календарное время поверх
  аптайма TSC, `civil.c` — перевод даты в unix-время и обратно.
- В лог при загрузке: `time: RTC 2026-10-29 11:39:58 UTC` (сверено с хостом).
- Syscalls `SYS_TIME_GET` (319), `SYS_TIME_SET` (320, только root: ставит время и
  пишет его в RTC, плюс смещение пояса), `SYS_FILE_STAT_EX` (321: размер, режим,
  uid, gid, atime/mtime/ctime). Обёртки `pc_time_get`, `pc_time_set`,
  `pc_file_stat_ex`.
- Hosted libc: `time()`, `gettimeofday()`, новый `clock_gettime()`
  (`CLOCK_REALTIME`/`CLOCK_MONOTONIC`) читают календарное время; `localtime()` и
  `mktime()` учитывают пояс ядра (без летнего времени); `stat()` отдаёт
  `st_mode`, `st_uid`, `st_gid` и времена файлов.
- ext2 записывает atime/ctime/mtime при создании, записи, дозаписи и смене
  прав, время удаления — в `dtime`; tmpfs хранит времена своих узлов.
- Тест `tests/civil_test.c` (`make test-civil`): сверка с `gmtime_r` хоста на
  десятках тысяч дат, високосные годы (2000, 2100), граница 2038, разбор RTC во
  всех режимах и битые значения.

### Железо: ввод, USB, питание (`src/drivers/`, `src/kernel/smp/`)
- Новый драйвер I2C-HID тачпада (ELAN, Intel LPSS) с прерываниями в IDT.
- `cpufreq.c`: управление частотой процессора, HWP и EIST.
- xHCI: глобальная блокировка контроллера, таймауты по TSC вместо счётчика
  (частота из CPUID 0x15), восстановление зависшей конечной точки USB-мыши
  (Reset Endpoint + Set TR Dequeue). Исправляет лаги и пропадание мыши на
  bare metal (i5-1235U).
- Клавиатура: буферы обычных и специальных клавиш под IRQ-safe спинлоком.

### Wi-Fi AR9285 (порт ath9k, `src/drivers/net/ath9k/`)
- Аппаратный слой: регистры, initvals (`mk/gen_ath9k.py`), чтение EEPROM,
  `ath9k_hw.c`, spur mitigation, тестовые хуки чтения/записи.
- 802.11: конвертация data-кадров (`src/net/802.11/data.c`), RSSI в `wifi.c`.
- Тест `tests/ath9k_test.c`.

### Графика (`src/userspace/window_manager.c`)
- Оконный менеджер использует композитор: создание, поднятие, обновление и
  уничтожение окон проходят через `compositor_window_*`; окна, которыми уже
  занимается композитор, не получают лишних перерисовок.

### Сборка и инструменты
- `purec.py`: живой вывод по стадиям сборки; `db/builds.db` пополняется при каждой
  линковке ядра.
- `make test` дополнен: `test-vfs-perm`, `test-install-accounts`, `test-accounts`,
  `test-hdrbg`, `test-ext2-meta`, `test-devfs`, `test-procfs`, `test-sysfs`,
  `test-tmpfs`, `test-vfs-mount`, `test-vfs-alias`, `test-format`.

### Известные ограничения
- Права на не-ext2 файловых системах живут только в памяти; бит `x` и обход
  каталогов пока не проверяются.
- Сквозная установка с аккаунтами и вход под обычным пользователем проверены
  по частям (хост-тесты, загрузка в QEMU), полный прогон на VirtualBox и bare
  metal ещё впереди.
- Десктопные часы (Rust, `desk_datetime`) пока живут в `/purec/datetime.cfg` и не
  читают время ядра: нужен переход на `SYS_TIME_GET`/`SYS_TIME_SET`.
- Открыто: z-order окон «Мониторинг» и Device Manager, пропадающая рамка в
  установщике, паника Limine на первом пункте установленной системы,
  дублирование символа клавиатуры в консоли.

## V1.2.27 — Lua 5.4 REPL: порт, hosted libc, эхо ввода, Портирували MicroPython

### kernel.map и Build UUID (`src/kernel/Makefile`, `linker-limine.ld`, `mk/toolchain.mk`)
- Ядро генерирует `bin/kernel/kernel.map` (`nm -n`, шапка `# build <uuid> <time>`)
  и `kernel-limine.ld.map` (`-Map`); `kernel.map` едет в ISO как `/boot/kernel.map`.
- Каждая сборка получает уникальный UUID (`/proc/sys/kernel/random/uuid` + UTC-штамп),
  вшитый в ядро через генерируемый `build_stamp.c`.
- `KERNEL_CFLAGS`: `-fno-omit-frame-pointer` (достоверный unwinding) и
  `-ffile-prefix-map/-fdebug-prefix-map` (`__FILE__` вида `src/...`, без хостовых путей).

### Паника: символы, origin, стек (`src/kernel/diagnostics/`)
- Новый panic-safe резолвер `ksym.c`: адрес → `имя+смещение` по встроенной `.ksyms`.
- Таблица расширена до глобалов + статиков + локалок (`nm -n`, CAP 4096, `.ksyms`
  в линкере `4096*64+64`); константы синхронны в `ksyms.py`, `kmod_so.c`, `ksym.c`.
- `Call Trace` печатает `#N [<addr>] символ+0x...`, `RIP sym`, дамп 32 qword от RSP,
  статус `ksyms: ready/MISSING`.
- `kernel_panic`/`kernel_panic_exception` — макросы с `__FILE__/__LINE__/__func__`:
  паника печатает `at src/.../console.c:329 exec_line()`; хост-тесты не тронуты
  (`PUREC_HOST_TEST`-гард).
- Композит отключается в панике (`compositor_panic_disable`): окна и курсор
  больше не перекрывают текст.

### QR-код в панике (`qr.c`, `panic_qr.c`)
- Свой freestanding QR-кодер (byte mode, EC-L, версии 1–25, все 8 масок с пенальти,
  без аллокаций); по пути пойман и починен баг порядка коэффициентов в Риде-Соломоне.
- Под паникой рисуется QR на `issues/new?title=Kernel panic <UUID>&body=<дамп>`:
  Build, причина, origin, RIP, до 8 кадров, стадия загрузки. Скан с телефона
  открывает предзаполненный ишью на GitHub. Бюджет URL 1200 символов с урезанием
  кадров; без GOP или на мелком экране — пропуск с пометкой в логе.

### База сборок (`mk/build_db.py`, `db/builds.db`)
- Хостовый SQLite: `uuid ↔ built_at + kernel_map + git_rev`. Пишется при каждой
  линковке ядра, в ядро и ISO не попадает. По UUID с экрана паники:
  `python3 mk/build_db.py lookup --db db/builds.db <uuid>`.

### Тесты и CI
- `tests/ksym_resolve_test.c` (`make test-ksym`): exact/nearest/miss, битая magic,
  `count 0/>cap` — PASS.
- `tests/qr_encode_test.c` (`make test-qr-encode`): границы версий (17/18 байт →
  V1/V2, 300 → V11, 1273 → V25, 1300 — отказ), finder/timing/dark, BCH format,
  детерминизм — PASS.
- `tests/qr_decode_test.py` + `tests/qr_dump.c` (`make test-qr-decode`,
  `QR_DECODE_ARGS=--strict`): round-trip через zbarimg (текст, URL 923 байта,
  байты 1–255) — PASS.
- CI: убран `pull_request`-триггер — тяжёлый пайплайн больше не дублируется
  при мерже веток, запуск один, на пуш в `main`. В `host-tests` добавлены
  QR-decode (`zbar-tools` + `pillow`) и `py_compile` новых скриптов; в артефакты
  релиза добавлены `kernel.map` и `builds.db` (+ SHA256).

### Планировщик: Apple-style QoS + кванты (`src/kernel/process/scheduler.[ch]`, `src/kernel/core/init.c`)
- Новый `enum sched_qos`: `IDLE/BACKGROUND/UTILITY/DEFAULT/USER_INITIATED/USER_INTERACTIVE`
  + поле `qos` в `struct thread`, API `scheduler_set/get_qos()`, `scheduler_qos_quantum()`.
- Кванты по классу: interactive 2мс, initiated 3мс, default 5мс (= `SCHEDULER_TIME_SLICE_MS`),
  utility 8мс, background 10мс; `schedule_locked()` выставляет `ticks_remaining` по QoS потока.
- `pick_next()`: сначала максимальный QoS среди READY/eligible, внутри класса — старый
  round-robin по курсору. Когда все потоки DEFAULT — поведение 1-в-1 как было.
- `init-input`/`desktop-keyboard` → `USER_INTERACTIVE`, `net-rx` → `UTILITY`,
  `kernel-log`/`qemu-net-test` → `BACKGROUND`.
- Снят жёсткий пин `affinity 0` (BSP) у `init-input`, `desktop-keyboard`, `kernel-log`,
  `net-rx`, `qemu-net-test` → `affinity -1` (любой CPU). Было: драг окна дёргал только CPU0;
  стало: нагрузка паралеллится. Syscall-путь через BSP (`scheduler_enter_kernel`) оставлен
  до аудита общих структур.
- Проверено: `test-scheduler-cpu`, `test-scheduler-balance`, весь `make test` зелёный.

### Окна: коалесцинг драга до 60fps (`src/userspace/userspace.c`)
- Было: драг окна/иконки делал полный `redraw_managed_scene` на каждое движение мыши —
  до 1000 полных перерисовок в секунду в одном потоке, CPU0 до 60%.
- Стало: позиция обновляется сразу на каждый пакет, тяжёлая перерисовка сцены при зажатой
  кнопке — не чаще раза в 16мс; курсор живой через `mouse_flush_pending` каждый проход;
  на отпускании кнопки — принудительный финальный кадр (`last_drag_ms = 0` + redraw).

### Порт MicroPython (`src/programs/micropython/`, `/bin/program/micropython`)
- Завендорено ядро `py/` + `shared/runtime/pyexec,stdout_helpers` + `shared/readline`
  + `extmod/{virtpin,modplatform,vfs}.h` + сборочные скрипты `py/*.mk,*.py` (qstr-пайплайн
  апстрима), конфиг минимального уровня без флоатов (как lua v1 без math).
- Свои `mpconfigport.h` (куча 256КБ статикой, `alloca` через builtin), `mphalport.[ch]`
  (stdin/stdout через libc, тики через `clock()`), `main.c` (REPL + `argv[1]` файл,
  `mp_import_stat`/`mp_lexer_new_from_file` через `fopen`, GC по стеку).
- Свой Makefile: кросс `x86_64-elf-gcc`, кодген `mpversion/qstrdefs/moduledefs/root_pointers`,
  линковка `crt0.o + libpurec.a` через `linker-userspace.ld` (как lua); static ELF64 1.1МБ.
- Хук в `mk/main.mk` (`programs`), optional-записи `/bin/program/micropython` в обоих манифестах.
- Проверено: те же исходники хостом — REPL считает, файлы исполняет, `fact(10)` = 3628800;
  `make test` зелёный, `make kernel` чисто.

### `rm` с защитой корня (`src/programs/rm/`, `/bin/rm`)
- Новый бинарь `rm`: файлы, пустые каталоги (`-d`), рекурсия (`-r/-R`), несколько операндов,
  `-f` (молча пропускает отсутствующие), `-v`, `--`, `--help`.
- Защита как в GNU: `/` удалять нельзя — `rm /` отвечает `Is a directory`, а `rm -r /`
  упирается в failsafe (`it is dangerous to operate recursively on '/'`, нужен явный
  `--no-preserve-root`); `'.'`/`'..'` как операнды всегда отклоняются.
- Рекурсия идёт через `pf_list_long` батчами с перелистыванием до пустоты; линковка
  `crt0.o + path.o + libpurefs.a + libpurec.a` (как `touch`/`mkdir`, 0 новых undefined).
- Манифесты (`/bin/program/system/rm`, `/bin/rm`, required) + строка в `help`.
- Проверено: сборка чисто, undefined-символы 1-в-1 как у `touch`, `make test` зелёный.

### IPC для процессной модели сети (`src/kernel/ipc/`, `SYS_IPC_SEND/RECV`)
- Новый транспорт процесс↔процесс: мейлбоксы в ядре, адресация по PID, копия
  отправитель→ядро→получатель; пейлоад до 2048 байт (влезает полный ethernet-кадр),
  глубина 64 сообщения, аллокация ленивая через `oom_slab`.
- Сисколы `SYS_IPC_SEND 291` (неблокирующий) и `SYS_IPC_RECV 292` (poll или ожидание
  до `wait_ms` через `scheduler_sleep`); валидация буферов через readable/writable.
- Доступ только с `PROCESS_CAP_NET_ADMIN` (ядро мимо капа); кап выдаётся по имени
  процесса (`netd`, `net-rx`, `dhcp`, `dns`) как раньше `STORAGE_ADMIN` для installer.
- Очередь дренируется при освобождении процесса (`process_node_free`); поиск цели через
  новый `process_lookup_by_pid`, локи — `process_mutex` + отдельный IPC-спинлок.
- libc: `pc_ipc_send/pc_ipc_recv` в `runtime.c` + `purec.h`.
- Хост-тест `tests/ipc_mailbox_test.c` (`make test-ipc`): roundtrip, FIFO, empty/full,
  лимиты, cap-gate, drain при cleanup, кадр 1514 байт — PASS; весь `make test` зелёный.
- Ядро при этом работает как было: сетевой стек пока в `net_service_thread`, dual-run,
  поведения не меняли — следующий шаг `netd`-каркас и вынос RX-цикла.

### Системные процессы с PPID 1 (`process_spawn_service`, защита PID 1)
- Новый `process_spawn_service()`: настоящий `struct process` (PID из общего счётчика,
  `parent_pid = 1`, состояние RUNNING, имя, капы, окружение от init) + тред ядра через
  новый `scheduler_create_service_thread()` (kernel AS, `user_mode = false`, без BSP-пина).
- Переведены: `init-input` + `desktop-keyboard` (QoS INTERACTIVE, без капов) и `net-rx`
  (QoS UTILITY, `PROCESS_CAP_NET_ADMIN`); `kernel-log` и `qemu-net-test` пока тредами.
  QoS и `affinity -1` сохранены 1-в-1.
- В htop теперь видны PID с PPID 1 и живым CPU/TIME (runtime идёт с треда сервиса).
- PID 1 под защитой со всех сторон: выход паникует (`PID 1 exited`), OOM-селект скипает
  (`pid == 1`), рип зомби отказывает; другого пути убить процесс (kill-сискола нет).
- Проверено: `make kernel` чисто без варнингов, `make test` зелёный; валидация буферов
  и `enter/leave_kernel` идут по `user_mode`, для сервисов поведение как у тредов.


### Порт Lua 5.4.8 (`src/programs/lua/`, `/bin/program/lua`)
- Завендорен Lua 5.4.8 (ядро + библиотеки + REPL `lua.c`), сборка
  `-DLUA_ANSI` без `LUA_USE_POSIX`/`LUA_USE_DLOPEN` (ISO C фолбэки Lua:
  `popen` unsupported, `fseek/ftell`, `tmpnam`, `system`, no-op inspectstat).
- v1 открывает `base/package/coroutine/table/io/os/string/utf8/debug`
  через свой `linit-purec.c`; библиотека `math` исключена (нужен полный
  libm — soft-fp, v2). `lmathlib.c` и оригинальный `linit.c` завендорены
  для будущего.
- Линковка `crt0.o` + `libpurec.a` через `linker-userspace.ld` (обычный
  hosted `main()`); static ELF64, 0 undefined symbols.
- Манифесты (`assets/manifest.txt`, `manifest-new.txt`): optional-запись
  `/bin/program/lua`; ISO собирается, boot smoke PASS.

### Hosted libc ради порта
- Новый `hosted/locale.h` + `src/libc/locale.c`: только локаль C
  (`decimal_point "."`, `setlocale` всегда `"C"`); `locale.o` в `libpurec.a/.so`.
- `signal.h`: недостающий ISO C `sig_atomic_t` (требует `lstate.h`).
- `stdio.h`: `SEEK_*`, `_IONBF/_IOLBF/_IOFBF`, декларации
  `freopen/tmpfile/tmpnam`; `stdio.c`: `freopen` (close+open,
  text==binary), `tmpfile/tmpnam` — стабы `ENOSYS/NULL`.
- `stdlib.h/c`: стаб `system()` (NULL → 0, иначе −1, шелла нет).
- `string.h/c`: `strcoll()` как `strcmp` (C-локаль).
- `time.h/c`: `difftime()` как разность; убран устаревший коммент про
  «нет SSE в юзерспейсе» (юзерспейс SSE может, FPU-стейт ядро держит).
- `math.h/c`: `trunc/floor/ceil/fmod/modf/frexp/sqrt/pow` без вызовов libm
  (только `CVTTSD2SI/CVTSI2SD/DIVSD/SQRTSD` + битовые трюки, линкуется под
  `-ffreestanding`); `pow` точен на целых степенях `[-64,64]` + `x^0.5`
  через `sqrt`, общий дробный `pow` — NaN (лимит v1); гарды `INFINITY/NAN`
  от `float.h`.

### Эхо ввода в REPL (`purec_lua_readline.[ch]`)
- Было: терминал эхует ввод шелла сам, а ребёнок на сыром `SYS_GETCHAR`
  эха не получает — ввод вслепую, backspace падал в буфер буквально и
  ломал строку (`syntax error`).
- Стало: свой `lua_readline` через официальный хук (форс-инклуд `-include`
  в `LUA_OBJECTS`), зеркалит шелл: посимвольное эхо с `fflush` (stdout
  line-buffered), стирание backspace через `'\b'` (консоль `gop.c` его
  стирает), CR/LF, фильтр контролов, кап `LUA_MAXINPUT` без переполнений,
  все выходы NUL-терминируют.
- `SYS_GETCHAR` EOF не возвращает никогда — выход из REPL через `os.exit()`
  или закрытие окна (Ctrl-D нет), зафиксировано в `lua/README.md`.
- Проверено живьём в PureC Terminal: ввод виден, опечатка правится,
  `print("Hi")` печатает `Hi`.

### Известные лимиты v1
- Нет библиотеки `math`; дробный `pow` (кроме `0.5`) — NaN.
- `io.popen/io.tmpfile/os.execute/os.tmpname` — graceful error; локаль C.


### TCC наконецто работаеть 
- Было: TCC не работал в юзерспейсе, а теперь работает.
  Жаловался на библиотеке и отказывался компилировать файлы
  Приходилось запускать с флагами
  
  ```bash
  tcc -static /test.c -o /hi -L/lib/tcc
  /hi
  ```

- Стало: TCC работает в юзерспейсе, и теперь компилирует файлы без ошибок.


### GPU: VMware SVGA II (`src/drivers/display/svga.[ch]`, `src/drivers/pci/pci.[ch]`)
- Новый драйвер SVGA II (PCI `15AD:0405`): `BAR0=IO` (index/value), `BAR1=FB`,
  `BAR2=FIFO`; негоциация `ID 0x90000002` с фолбэком, modeset `1024x768x32`
  с проверкой фактической геометрии карты (`WIDTH/HEIGHT/BPP/BYTES_PER_LINE/FB_OFFSET`,
  несовпадение — фолбэк в GOP вместо мусора).
- FIFO: `MIN/MAX/NEXT/STOP`, `CONFIG_DONE`, команды `UPDATE=1`, `RECT_FILL=2`,
  `RECT_COPY=3` с проверкой `CAPS`; `SYNC/BUSY`, перенос `BAR0` на свободный
  адрес при молчании BIOS-назначения.
- Порты строго по спеке/референсу: index `base+0`, value `base+1` (dword).
- `pci_bar_size()`: проба размера BAR через `0xFFFFFFFF`.
- Интеграция: `gop` рисует во VRAM через `mmio_map`, показ кадра и композитор
  (`compositor_present`, курсор, окна) — через `svga_update`; `RECT_FILL/COPY`
  ускорение при `CAPS`.
- Загрузчик: пункты `GPU SVGA (video=svga)` / `GOP safe (video=gop)` / fallback;
  любой отказ GPU — тихий фолбэк в Limine GOP, паники нет.
- Дебаг-меню (`purec.py`): пункты `5 GPU graphics` / `6 GPU log → file`
  (QEMU `-vga vmware`, serial в `bin/qemu-gpu-debug.log`).
- Проверено: QEMU `-vga vmware` — `ID 0x90000002`, `1024x768x32`, `caps=0xe3`,
  `video: VMware SVGA II active (FIFO UPDATE)`; `make test` зелёный.

### Заморозка
- Фичи заморожены. Дальше только фиксы багов и стабилизация текущего состава:
  GPU SVGA II, GOP фолбэк, юзерспейс, сеть, fs. Нового функционала не добавляем
  до отдельного решения.


## V1.2.24 V1.2.26 — .so формат (ET_DYN): юзерспейс, kmod, tcc-рантайм

### Адресное пространство и стандарты линковки
- Linux-like расклад юзера: legacy ET_EXEC `0x400000`, PIE `0x555555554000`,
  регион `.so` `0x600000000000+` (шаг 256МиБ); стек, куча, `USER_TOP` без изменений.
- Ядро остаётся static (`-fno-pic`), юзерспейс и модули — PIC.
- `PROVIDE` без референса символ не эмитит (поэтому пропадал `_kernel_end`) —
  якоря `.ksyms` теперь обычными присваиваниями.

### Лоадер юзерспейса (`elf.c`, `process.c`)
- `ET_DYN` + слайд базы, `PT_DYNAMIC`, `RELATIVE/GLOB_DAT/JUMP_SLOT/64`,
  резолв по `DT_HASH` (GLOBAL в приоритете, weak-undef в `0`).
- `DT_NEEDED`-очередь из `/lib` (initramfs/VFS, дедуп, лимит 8); куча стартует
  после верхней границы всех объектов. `ET_EXEC` идёт старым путём 1-в-1.

### Тулчейн
- `mk/toolchain.mk`: `USER_SO_CFLAGS`, `SO/PIE_LINK_FLAGS`, `KMOD_SO_CFLAGS`
  (`-fPIC -mcmodel=small`: `kernel`-модель с PIC несовместима в GCC),
  `KMOD_SO_LINK`, `mk/kmod-so.ld`, `src/libc/userspace-so.ld`,
  `linker-userspace-pie.ld` (`PHDRS` + `SIZEOF_HEADERS` против
  `PHDR not covered by LOAD`).
- `libpurec.so` (`SONAME`, SYSV `HASH`, без `TEXTREL`), пилот `echo-dyn`
  (`PUREC_DYNAMIC=1`, `NEEDED libpurec.so`); проверено реальным прогоном
  обоих файлов через лоадер (GOT сошёлся).
- Починен `ROOT_DIR` у `82543gc` (5 уровней вместо 4 — модуль не собирался).

### kmod `.so` (замена `LD -r`)
- `.ksyms`-секция в ядре + `mk/ksyms.py` (1308 символов, сортировка + бинарный
  поиск, проверка `magic`); патч в рецепте линка ядра.
- `src/kernel/module/kmod_so.c`: маппинг ET_DYN в higher-half (бамп от
  `__ksyms_end`), резолв kernel-first + self, `vmm_protect_page` для W^X,
  доступ к образу только через трансляцию, `kmod_get`.
- `.so`-таргеты 6 ин-три модулей (e1000, 82543gc, pcnet, ar9285, ext2,
  i2c_hid_touchpad); все undef-символы покрыты таблицей ядра; `devman`
  soft-probe `.so` (только лог, поведение не меняет).

### tcc
- Собран `libtcc1.a` (7 объектов из `tcc/lib`) в `/lib/tcc` + манифест:
  фиксит `tcc: error: file 'libtcc1.a' not found`. ISO собирается,
  `libtcc1.a` есть в ISO и initramfs.

## V1.2.22 v1.2.26 — OOM, планировщик, SMP, консоли, тесты

### OOM-подсистема (`src/mm/oom/`, только ядро решает)
- Новый модуль: `oom` (резерв ядра 8–64МиБ + гейты), `oom_account` (учёт страниц по pid),
  `oom_victim` (killer только EXITED-зомби, PID 1 и живых не трогает), `oom_pressure`
  (OK/LOW/CRITICAL), `oom_reclaim` (реестр shrinker'ов), `oom_slab` (slab 32–4096 поверх PMM
  с возвратом пустых страниц), `oom_gate` (единая точка), `oom_info` (только статистика),
  `oom_all.h` (зонтик).
- Юзерспейс OOM вызвать не может: отказ — это `0`/`false`/`-1` наверх, без паник.
- Врезка: `kernel.c` (`oom_gate_init`), `vmm.c` (гейт USER-мэппингов), `elf.c` (ELF-страницы
  через `oom_alloc_user_page`), `process.c` (чарджи heap/spawn, per-process лимит,
  WARN `oom: heap grow denied` / `spawn denied`, `process_parent_pid()`).
- `Makefile` ядра: все `mm/oom/*.c` в сборке.

### Процессы и планировщик
- Ноды тредов и процессов через `oom_slab` с фолбэком на страницы.
- TID-хэш (unblock, stopped, runtime, affinity, free, clash) и PID-хэш (`find_by_pid`, clash
  спавна) вместо линейных сканов.
- `finish_switch` освобождает стек+ноду TERMINATED-потока сразу после переключения.
- Спилловер после сисколов: `leave_kernel` возвращает поток без явного пина на
  простаивающий AP (`affinity_auto`); тело сискола по-прежнему всегда на CPU0
  (BSP-ограничение сохранено); явный пин юзера не трогается.
- `enter_kernel` по-прежнему тянет поток на CPU0 на время сискола.

### Листинг процессов и юзерспейс на heap
- `raminfo`, `monitor`: чанки через `pc_process_list_page` в переиспользуемый heap-буфер.
- `init`: постраничный reap зомби без лимита 64.
- `bottom_panel`: поиск имени по pid чанками по 8, без массива на стеке.

### Консоли терминалов (`gop.c`)
- Было: один глобальный `user_console` — второй терминал зеркалил первый.
- Стало: 8 слотов по pid (геометрия, курсор, история) под спинлоком; наследование
  через цепочку родителей (ребёнок пишет в консоль терминала); слот освобождается
  при выходе процесса; `disable` больше не сносит чужой слот.

### Сеть
- Задушен шторм `wifi: scan requested but no device` (581/сек): без адаптера повтор
  не чаще раза в 5 секунд.

### Тестовые программы
- `burn`: кранч 45 секунд для проверки распределения по ядрам.
- `eat [МБ]`: жрёт чанками по 1МиБ до цели (дефолт 1024), потом холд вечно:
  перетирает страницы, доедает освободившееся, выходит сам при закрытии терминала
  (смена родителя на init).

### Host-тесты (`make test` зелёный)
- `test-oom`: резерв, гейты, per-process кап, аккаунтинг, отказы аллокаций.
- `test-scheduler-balance`: спилловер на idle AP, уважение явного пина, поведение
  без idle-ядер, пиннинг входа на BSP.
- `test-scheduler-cpu`: старый сьют поверх хэшей и slab (500 тредов, OOM,
  конкурентный клейм 200 тредов на 16 CPU).
- `mk/main.mk`: чинена линковка `test-scheduler-cpu` (`oom_slab.c`), добавлены
  `test-oom`, `test-scheduler-balance`.
