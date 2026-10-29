# Драйверы PureC OS

Документация по каждому драйверу в `src/drivers/`. Для каждого драйвера описаны назначение, файлы, публичный API, порядок инициализации, места вызова в ядре, внутреннее устройство и ограничения.

Драйверы сгруппированы по подсистемам. Ссылка в таблице ведёт на описание, путь — на исходники.

## Прерывания и время

| Драйвер | Описание | Исходники |
|---------|----------|-----------|
| PIC 8259 | [pic.md](pic.md) | `src/drivers/interrupts/pic.c` |
| Таймер PIT и TSC | [timer.md](timer.md) | `src/drivers/interrupts/timer.c` |

## Вывод на экран

| Драйвер | Описание | Исходники |
|---------|----------|-----------|
| VGA текстовый режим | [vga.md](vga.md) | `src/drivers/display/vga.c` |
| Framebuffer (устаревший) | [fb.md](fb.md) | `src/drivers/display/fb.c` |
| GOP и графическая консоль | [gop.md](gop.md) | `src/drivers/display/gop.c` |
| VBE (Bochs DISPI) | [vbe.md](vbe.md) | `src/drivers/display/vbe.c` |
| VMware SVGA II | [svga.md](svga.md) | `src/drivers/display/svga.c` |

## Ввод

| Драйвер | Описание | Исходники |
|---------|----------|-----------|
| Клавиатура PS/2 | [keyboard.md](keyboard.md) | `src/drivers/input/keyboard.c`, `hid_keymap.c`, `key_event.h` |
| Тачпад I2C-HID | [i2c-hid-touchpad.md](i2c-hid-touchpad.md) | `src/drivers/input/i2c_hid_touchpad/` |
| Мышь PS/2 и слой указателя | [ps2-mouse.md](ps2-mouse.md) | `src/drivers/mouse/ps2_mouse.c` |
| Мышь USB | [usb-mouse.md](usb-mouse.md) | `src/drivers/mouse/usb_mouse.c` |

## Звук

| Драйвер | Описание | Исходники |
|---------|----------|-----------|
| Аудиосистема (PC-динамик, PCM) | [audio.md](audio.md) | `src/drivers/audio/audio.c` |
| Intel HDA | [audio-hda.md](audio-hda.md) | `src/drivers/audio/audio_hda.c` |

## Сеть

| Драйвер | Описание | Исходники |
|---------|----------|-----------|
| Intel 82540EM | [e1000-82540em.md](e1000-82540em.md) | `src/drivers/net/e1000/` |
| Intel 82543GC / 82544GC | [e1000-82543gc.md](e1000-82543gc.md) | `src/drivers/net/82543gc/` |
| AMD PCnet-PCI | [pcnet-am79c970a.md](pcnet-am79c970a.md) | `src/drivers/net/pcnet/` |
| Atheros AR9285 (Wi-Fi, порт ath9k) | [ath9k-ar9285.md](ath9k-ar9285.md) | `src/drivers/net/ath9k/` |

## Хранение

| Драйвер | Описание | Исходники |
|---------|----------|-----------|
| Блочный слой | [block-device.md](block-device.md) | `src/drivers/storage/block_device.c` |
| Перечисление контроллеров | [storage-probe.md](storage-probe.md) | `src/drivers/storage/storage_probe.c` |
| AHCI (SATA) | [ahci.md](ahci.md) | `src/drivers/storage/ahci.c` |
| ATA PIO (legacy IDE) | [ata-pio.md](ata-pio.md) | `src/drivers/storage/ata_pio.c` |
| Загрузочные образы Limine | [limine-blobs.md](limine-blobs.md) | `src/drivers/storage/limine_*.h` |

## USB

| Драйвер | Описание | Исходники |
|---------|----------|-----------|
| xHCI (USB 3.x) | [xhci.md](xhci.md) | `src/drivers/usb/xhci.c` |
| EHCI (USB 2.0) | [ehci.md](ehci.md) | `src/drivers/usb/ehci.c` |

## Шина, питание, случайные числа, отладка

| Драйвер | Описание | Исходники |
|---------|----------|-----------|
| PCI | [pci.md](pci.md) | `src/drivers/pci/pci.c` |
| Питание (обёртка ACPI) | [power.md](power.md) | `src/drivers/power/power.c` |
| Генератор случайных чисел (Hash-DRBG) | [random.md](random.md) | `src/drivers/random/` |
| Последовательный порт COM1 | [serial.md](serial.md) | `src/drivers/serial/serial.c` |

## Общие замечания

- **Проверка на железе.** Если в проекте есть данные о проверке драйвера, они указаны в его документе. Если таких данных нет, драйвер считается непроверенным.
- **Опрос вместо прерываний.** Большинство драйверов работают опросом, а не по IRQ. Это упрощает отладку, но влияет на отзывчивость. Исключения — PIC/таймер и LAPIC.
- **Имена устройств.** Диски получают имена `/dev/sdX`, сетевые интерфейсы — `ethN` и `wlan0`. Правила назначения описаны в каждом драйвере.
- **Модули.** Часть драйверов собирается отдельными модулями (`.so`, `.ko`, `.elf`) и подгружается ядром через диспетчер устройств. Список модулей, которые копируются в образ, — в `mk/main.mk`.
