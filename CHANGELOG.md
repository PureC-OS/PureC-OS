# Changelog

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
