# PureC-OS todo

## UserSpace

- [x] Доделать нормально мишку | priority:normal
- [x] Обновить Window Manager Что бы при перемещении окна не было мирцаный | priority:normal
- [x] Доделать нормальные драйвера Видео Карты Что бы можно было менять разрешение экрана {Работаеть частично на bare metal и в VirtualBox не работаеть} | priority:normal
- [x] Добавить возможность Менять розрешение экрана без перезагрузки ядра {Работаеть частично на bare metal и в VirtualBox не работаеть} | priority:normal
- [ ] А так же будет сделана миграция UserSpace в Ring 3. | priority:normal
- [ ] И миграция остальных програм которые живут в Ring 0 они переходят в Ring 3. | priority:normal
- [x] Мой notepad++ при новом рендеренге текста в notepad++ исчез весь текст букви и цифры | priority:normal
- [x] на taskbar батарея перекрывает время | priority:normal

## Userspace programs

- [x] добавыть програму для проверки ram | priority:normal
- [x] добавыть програму для проверки hdd | priority:normal
- [x] добавыть програму для проверки процесора | priority:normal
- [x] расширить функционал htop с полным мониторингом каждого ядра процессора снимать данные с ядра каждего отдельно и делать это в риал тайме | priority:normal

## Network

- [ ] Добавить поддержку 802.11 ассоциации | priority:normal
- [ ] Поднять Atheros AR928X Wirless Network Adapter | priority:normal
- [ ] Поднять network stack и обединить с 802.11 асоцицией | priority:normal
- [ ] Сделать норм wifi backend | priority:normal

## ACPI

- [x] Добавить AML парсер и интерпретатор {Частично реализован} | priority:normal
- [x] Добавить поддержку ACPI MADT (MADT Table){Частично реализован} | priority:normal
- [ ] Добавить чтения устройств из ACPI Table | priority:normal

## Kernel

- [x] Добавить менеджер устройств через ACPI чтения AML namespace | priority:normal
- [x] Сделать возможность чтения темпиратуры cpu на intel через ACPI | priority:normal
- [x] Сделать возможность чтения оборотов кулира через ACPI | priority:normal
- [ ] На Pentium Dual-Core T4500 @ 2.30 GHz температура не читается через ACPI нужен другой метод чтения температуры на пеньке | priority:normal
- [ ] На новом и старом железе показатели с батареи и зарядки не читаются через ACPI | priority:normal
- [x] Сделать initramfs | priority:normal
- [x] Переделать модель загрузки модулей что бы ядро при старте монтировало initramfs и загружало с него все нужные модули | priority:normal

## touchpad

- [x] Добавить поддержку touchpad (через ACPI) aspi только чтение устройства | priority:normal
- [ ] Написать драйвер touchpad Через i2c | priority:normal
- [ ] Провести тестирование драйвера touchpad на BareMetal | priority:normal

## Scheduler

- [x] scheduler на bare metal наченаеть лагать и появляется задержка | priority:normal
- [x] При перемещении окна происходит в userspace скачит нагрузка на cpu и scheduler начинаеть лагать | priority:normal

## SMP и Много процесорность

- [x] CF8/CFC: общая IRQ-safe блокировка транзакций и узкие записи PCI. | priority:normal
- [ ] Базовые вещи | priority:normal
  - [x] 1. Добавить поддержку обнаружения логических CPU через Limine: таблица CPU/APIC ID, BSP, отдельные detected/registered/online (до 16 записей) | priority:normal
  - [x] 2.1 Поднять второй CPU через Limine goto_address на отдельном стеке и оставить в изолированном idle | priority:normal
  - [x] 2.2 Поднять остальные Application Processors после подготовки общей SMP-синхронизации | priority:normal
  - [x] 3. Сделать per-CPU структуры (current, idle, флаги и счётчики планировщика; планирование на AP включено) | priority:normal
- [ ] Планировщик | priority:normal
  - [x] 1.1 Убрать глобальный current → сделать per-CPU | priority:normal
  - [x] 1.2 Переделать pick_next() под работу с несколькими ядрами | priority:normal
  - [x] 1.3 Нормально задействовать affinity | priority:normal
  - [x] 1.4 Добавить возможность миграции потоков между ядрами | priority:normal
  - [x] 1.5 Сделать отдельные runqueue (или хотя бы защиту глобальной) | priority:normal
- [ ] Синхронизация | priority:normal
  - [x] Защита общих структур: | priority:normal
    scheduler — IRQ-safe spinlock, передача блокировки через переключение стека process table — mutex с IRQ-safe защитой метаданных VMM / page tables — IRQ-safe spinlock и подтверждаемый TLB shootdown PMM — IRQ-safe spinlock VFS — рекурсивный mutex, допускающий сон при I/O
  - [x] Добавить cli/sti аккуратно + memory barriers где нужно | priority:normal
- [ ] Архитектурные штуки | priority:normal
  - [x] Отдельная GDT / IDT / TSS на каждое ядро | priority:normal
  - [x] Per-CPU kernel stack | priority:normal
  - [x] IPI (Inter-Processor Interrupts) — хотя бы базовые (reschedule, TLB shootdown, stop) | priority:normal
  - [x] TLB shootdown при смене page tables | priority:normal
- [ ] Процессы | priority:normal
  - [x] Зафиксировать модель процесса: в текущем ABI один поток на процесс; разные процессы выполняются параллельно | priority:normal
  - [x] Сохранить process->thread_id для текущего ABI; освобождать адресное пространство только после завершения переключения с потока | priority:normal
- [ ] Отладка | priority:normal
  - [x] Логирование с какого ядра пришло сообщение | priority:normal
  - [x] Возможность остановить все ядра при панике | priority:normal
  - [x] Простые тесты: поднять 2-е ядро и крутить на нём idle | priority:normal
- [ ] Оставшиеся ограничения и проверки | priority:normal
  - [x] Проверить загрузку CPU в htop именно с VirtualBox: исправлены активные ожидания, но замера на VirtualBox ещё нет. | priority:normal
  - [x] Проверить остановку остальных CPU принудительной паникой и SMP на bare metal. | priority:normal
  - [ ] Убрать ограничение старых драйверов/GUI и системных вызовов на BSP после аудита их общих структур. Ring-3 код уже выполняется на AP; при системном вызове поток временно переносится на BSP. | priority:normal
  - [ ] Добавить API нескольких потоков одного процесса, если он потребуется; текущая модель этого не поддерживает. | priority:normal

## Динамические процессы

Контекст: сейчас таблица процессов статическая (`processes[PROCESS_MAX_COUNT]`,
`PROCESS_MAX_COUNT=64`), треды статические (`threads[SCHEDULER_MAX_THREADS=64]`,
стек 16KB зашит в структуру), ядерной кучи (`kmalloc`/slab) нет — только
постраничный `pmm_allocate_page`. «Бесконечно» физически невозможно: каждый
процесс стоит struct ~3KB + 16KB kstack + 64KB user-stack + таблицы страниц.
Потолок следующей итерации — свободная RAM + OOM, а не константа.

- [x] Поднять PROCESS_MAX_COUNT 16 -> 64 до потолка планировщика; SYS_PROCESS_LIST | priority:normal
  больше не отвечает -1 на запрос больше таблицы (clamp до 1024, `a2=0` вернёт count).
- [x] Динамические треды и процессы: ноды тредов и процессов живут в PMM-страницах | priority:normal
  (страница на ноду треда + 4 страницы на kstack, страница на ноду процесса), очередь — связный список. Лимит = свободная RAM, OOM -> spawn -1. Проверено: host-тест (500 тредов, OOM, конкурентный claim 200 тредов на 16 CPU), QEMU boot smoke (2 CPU), SMP selftest + Ring-3 reap PASS.
- [x] Окна без лимита 8: реестр window_manager на PMM-чанках, detached-запуски | priority:normal
  десктопа на PMM-чанках, taskbar показывает первые 64.
- [x] Файловые хендлы без лимитов 32/16/16: VFS/FAT32/ext2 таблицы растут | priority:normal
  удвоением на PMM, дескрипторы стабильны, OOM -> NO_SPACE.
- [x] Спавн с VFS: точный размер файла вместо буфера 8MB contiguous + чтение циклом. | priority:normal
- [x] Зомби-ликвидация (утечка слотов и адресных пространств): EXITED-процесс, | priority:normal
  которого родитель так и не подождал, висит вечно. Сделать reparent сирот на PID 1 при выходе + цикл reap в init (wait nohang по детям в EXITED). Сбросить stale waiter_thread_id при reparent (иначе wait init упрётся в -1).
- [ ] Ядерная куча поверх PMM (slab под struct process / struct thread). | priority:normal
- [ ] Таблица процессов на куче: рост чанками по мере spawn, OOM -> -1. | priority:normal
- [ ] Стеки тредов аллоцировать из PMM поштучно (4 страницы), а не держать | priority:normal
  массив 64x16KB в BSS; освобождать стек при TERMINATED после переключения.
- [ ] Планировщик: уйти от O(n)-скана всей таблицы при каждом переключении | priority:normal
  (runqueue), PID-хэш вместо линейного поиска.
- [ ] Листинг процессов постранично (count + offset), юзерспейс на heap. | priority:normal

## Динамические процессы (лимит = свободная RAM, а не константа)

Контекст: сейчас таблица процессов статическая (`processes[PROCESS_MAX_COUNT]`,
`PROCESS_MAX_COUNT=64`), треды статические (`threads[SCHEDULER_MAX_THREADS=64]`,
стек 16KB зашит в структуру), ядерной кучи (`kmalloc`/slab) нет — только
постраничный `pmm_allocate_page`. «Бесконечно» физически невозможно: каждый
процесс стоит struct ~3KB + 16KB kstack + 64KB user-stack + таблицы страниц.
Потолок следующей итерации — свободная RAM + OOM, а не константа.

- [x] Поднять PROCESS_MAX_COUNT 16 -> 64 до потолка планировщика; SYS_PROCESS_LIST | priority:normal
- [x] Динамические треды и процессы: ноды тредов и процессов живут в PMM-страницах | priority:normal
- [x] Окна без лимита 8: реестр window_manager на PMM-чанках, detached-запуски | priority:normal
- [x] Файловые хендлы без лимитов 32/16/16: VFS/FAT32/ext2 таблицы растут | priority:normal
- [x] Спавн с VFS: точный размер файла вместо буфера 8MB contiguous + чтение циклом. | priority:normal
- [x] Зомби-ликвидация (утечка слотов и адресных пространств): EXITED-процесс, | priority:normal
- [ ] Ядерная куча поверх PMM (slab под struct process / struct thread). | priority:normal
- [ ] Таблица процессов на куче: рост чанками по мере spawn, OOM -> -1. | priority:normal
- [ ] Стеки тредов аллоцировать из PMM поштучно (4 страницы), а не держать | priority:normal
- [ ] Планировщик: уйти от O(n)-скана всей таблицы при каждом переключении | priority:normal
- [ ] Листинг процессов постранично (count + offset), юзерспейс на heap. | priority:normal

## Process

- [x] Тест | priority:normal
- [x] test | priority:normal
