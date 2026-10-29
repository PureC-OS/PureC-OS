# Драйвер контроллера USB xHCI

Драйвер контроллера xHCI (USB 3.x, класс PCI `0C/03/30`). Выполняет полную последовательность: сброс контроллера, передачу управления от BIOS, настройку колец команд, событий и передачи, перечисление портов, чтение дескрипторов и настройку двух типов устройств:

- **Накопители** (класс `08`, протокол Bulk-Only). Работают как блочные устройства `/dev/sdX` через SCSI-команды `READ(10)`, `WRITE(10)`, `READ CAPACITY(10)` и `SYNC CACHE(10)`.
- **Загрузочные мыши HID** (класс `03`, подкласс `01`, протокол `02`). Переводятся в boot protocol. Отчёты передаются в [usb-mouse.md](usb-mouse.md).

Хабы определяются, но не перечисляются: их счётчик есть в статистике, дочерние порты не опрашиваются.

## Расположение

| Файл | Назначение |
|------|------------|
| `src/drivers/usb/xhci.h` | Публичный API, `struct xhci_probe_stats`, коды ошибок `XHCI_PROBE_*` |
| `src/drivers/usb/xhci.c` | Реализация (около 1840 строк) |

Контроллер ищется через `storage_controller_list` (`src/drivers/storage/storage_probe.c`), который перечисляет PCI-устройства. Драйвер использует только первый xHCI, который удалось запустить.

## Публичный API

| Функция | Что делает |
|---------|------------|
| `xhci_set_address_mapping(hhdm, phys, virt)` | Базы для перевода адресов DMA |
| `xhci_init(linux_name_base)` | Однократная инициализация: контроллер, порты, устройства. Повторный вызов возвращает результат первого |
| `xhci_rescan(linux_name_base)` | Повторное перечисление портов после изменения топологии |
| `xhci_topology_changed()` | Есть ли изменения на портах с прошлого сканирования |
| `xhci_device_count()`, `xhci_get_device_info(index, info)`, `xhci_select_device(index)` | Список накопителей и выбор текущего |
| `xhci_read_sector`, `xhci_read_sectors`, `xhci_write_sector`, `xhci_write_sectors` | Блочный ввод-вывод по 512 байт |
| `xhci_flush_cache()` | `SYNC CACHE(10)` |
| `xhci_device_name()` | Имя текущего накопителя |
| `xhci_get_probe_stats(stats)` | Счётчики и последний код ошибки (для диагностики) |
| `xhci_poll_mouse()` | Забирает события и ставит в очередь новые отчёты мыши |

Блочные операции (`xhci_read_sector`, `xhci_write_sector` и их варианты с несколькими секторами) берут спинлок `xhci_lock`. `xhci_poll_mouse` берёт его через `xhci_try_lock` и при занятости просто возвращается.

## Инициализация (`initialize_controller`)

1. Включаются bus master и доступ к памяти в PCI (`command | 0x06`). BAR отображается через `mmio_map` (размер `XHCI_MMIO_MAP_SIZE`).
2. Читаются регистры возможностей: длина, версия, `HCSPARAMS1` (слоты, прерывания, порты), `HCCPARAMS1` (64-байтные контексты, Extended Capabilities).
3. `take_ownership` ищет расширенную capability `USBLEGSUP` (ID 1). Ставит бит запроса у OS, ждёт, пока BIOS отпустит (до 1 секунды), затем очищает `USBLEGCTLSTS`.
4. Число слотов ограничено 32. Размер контекста — 32 или 64 байта (бит `CSZ`).
5. Контроллер останавливается (`RUN = 0`, ожидание `HALTED`), сбрасывается (`HCRST`), ждётся `CNR = 0`.
6. Проверяется поддержка страниц 4 КБ (`PAGESIZE`). Затем выделяются кольца, таблица контекстов устройств, DCBAA и scratchpad-буферы.
7. Запускается контроллер и сканируются порты.

Ошибки записываются в `probe_stats.last_error` с кодом `XHCI_PROBE_*`. Например, `XHCI_PROBE_BIOS_HANDOFF` означает, что BIOS не отдал управление, `XHCI_PROBE_HALT_TIMEOUT` — что контроллер не остановился.

## Перечисление порта (`enumerate_port`)

1. `address_port` — Enable Slot, Address Device. Стартовый размер пакета выбирается по скорости (`initial_packet_size`). Если дескриптор устройства сообщает другой, он исправляется (`update_control_packet_size`).
2. `get_descriptor` — дескриптор устройства (18 байт), затем дескриптор конфигурации (до 512 байт, `descriptor_buffer`).
3. `find_mass_storage_interface` ищет интерфейс класса `08`, подкласса `06` (SCSI transparent), протокола `50` (Bulk-Only) с парой bulk-endpoint'ов. Если найден, вызывается `configure_mass_storage` и `identify_mass_storage` (READ CAPACITY, INQUIRY).
4. Иначе `find_boot_mouse_interface` ищет HID-интерфейс, который поддерживает boot-мышь. `configure_boot_mouse` включает interrupt-endpoint, переводит устройство в boot protocol (`SET_PROTOCOL 0`) и ставит интервал опроса.
5. Остальные устройства получают слот, но без функции. Они видны только в таблице слотов.

Имена накопителей: `/dev/sdX`, где буква вычисляется как `'a' + linux_name_base + число уже найденных устройств` (`build_device_name`, вызов из `identify_mass_storage`).

## Кольца и события

- Кольцо команд и кольца передачи по 64 TRB (`XHCI_RING_ENTRIES`). Кольцо событий 128 записей (`XHCI_EVENT_ENTRIES`).
- Ожидание ответа на команду или передачу ограничено `XHCI_TIMEOUT_MS` = 5000 мс по TSC (`tsc_mhz` определяет частоту).
- Событие `XHCI_EVENT_TRANSFER` (32) с endpoint мыши обрабатывается в `dispatch_mouse_event`. Остальные события ждут своей команды (`next_event`).
- `poll_async_event` разбирает асинхронные события: изменения портов (отключение накопителя или мыши), ошибки передачи.

## Накопители (`bulk_only_command`)

- Протокол Bulk-Only: CBW (31 байт) на bulk-OUT, данные на bulk-IN или OUT, CSW (13 байт) на bulk-IN.
- Команды из `scsi_sector_command` и `scsi_blocks_command`. Размер сектора 512 байт.
- Проверка границ: `lba < sector_count` до отправки команды.
- Ответ CSW проверяется: сигнатура `USBS`, совпадение тега и `status == 0`. Повторов нет: при любой ошибке функция возвращает `false`, и вызывающий код получает ошибку чтения или записи.

## Мыши (`queue_mouse_report`, `recover_mouse_endpoint`)

- `queue_mouse_report` ставит в очередь transfer на interrupt-IN endpoint. Буфер — `mouse_reports[index]`, 8 байт на устройство, выровнен по 64 байта.
- Если endpoint остановлен из-за ошибок, `recover_mouse_endpoint` отправляет команды Reset Endpoint и Set TR Dequeue Pointer, после чего отчёты снова ставятся в очередь.
- Ошибки передачи считаются в `probe_stats.mouse_transfer_errors`.

## Где используется

- `src/drivers/storage/block_device.c` — блочный слой вызывает `xhci_read_sector(s)` и `xhci_write_sector(s)` для накопителей USB.
- `src/drivers/storage/storage_probe.c` — `storage_controller_list` находит контроллер и вызывает `xhci_init`.
- `src/kernel/devices/device_manager.c`, `src/fs/` — информация о накопителях и повторное сканирование.
- `src/userspace/userspace.c`, `src/kernel/syscall/syscall.c` — `xhci_poll_mouse` через `usb_mouse_poll`.

## Ограничения

- Хабы не перечисляются. Устройства за хабом недоступны, при этом хаб считается в статистике.
- Поддерживается один контроллер xHCI (первый, который запустился). Второй контроллер игнорируется.
- Только Bulk-Only Transport. Протокол UAS (USB Attached SCSI) не реализован.
- Передача синхронная с ожиданием. Большие операции блокируют вызывающий поток до 5 секунд на команду.
- Нет MSI/MSI-X: события получаются опросом кольца событий (`poll_async_event`), а не по прерыванию.
- Scratchpad-буферы выделяются из PMM. При нехватке памяти инициализация завершится с кодом `XHCI_PROBE_SCRATCHPADS`.
- В проекте нет зафиксированных результатов проверки на конкретном железе. При проблеме на машине начинайте с `xhci_get_probe_stats`: в нём видно последний этап и код ошибки.
