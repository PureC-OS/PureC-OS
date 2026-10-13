# Changelog

## V1.2.27 — Lua 5.4 REPL: порт, hosted libc, эхо ввода, Портирували MicroPython 

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
