# Драйвер GOP (Graphics Output Protocol) и графической консоли

Основной графический драйвер ядра. Работает поверх линейного framebuffer, который передаёт загрузчик: Limine (путь по умолчанию), Multiboot2 (GRUB) или видеорежим, переключённый через [SVGA II](svga.md) или [VBE](vbe.md). Драйвер решает четыре задачи:

1. Хранит описание framebuffer (`struct gop_state`): адрес, размеры, pitch, глубину цвета.
2. Рисует примитивы (пиксель, прямоугольник, линия, растр, прокрутка) в **backbuffer** и синхронизирует его с видимым экраном.
3. Выводит текстовую консоль ядра и консоли процессов (до 8 одновременно) шрифтом из `gfx/text`.
4. Передаёт готовые области оконному композитору (`src/gfx/compositor.c`), если он активен.

## Расположение

| Файл | Назначение |
|------|------------|
| `src/drivers/display/gop.h` | Публичный API и структура `gop_state` |
| `src/drivers/display/gop.c` | Реализация (около 1050 строк) |

## Инициализация

| Функция | Источник | Заметки |
|---------|----------|---------|
| `gop_init_from_limine(fb, firmware_type)` | Limine, вызов из `src/boot/boot.c:274` | Pitch делится на байты пикселя. Имя протокола зависит от типа прошивки (UEFI, BIOS, иначе) |
| `gop_init_from_multiboot(mbi)` | Multiboot2, тег типа 8, вызов из `grub_main.c:37` | Pitch всегда делится на 4, даже если bpp другой |
| `gop_apply_live(addr, w, h, pitch_px, bpp)` | После переключения режима SVGA или VBE, вызов из `kernel.c:94` | Освобождает backbuffer, сбрасывает все консоли процессов |

При смене framebuffer backbuffer освобождается через `pmm_free_contiguous` и пересоздаётся при следующем рисовании.

## Публичный API (основное)

**Состояние:** `gop_is_available`, `gop_get_address`, `gop_get_width`, `gop_get_height`, `gop_get_pitch`, `gop_get_bpp`, `gop_get_framebuffer_size_bytes`, `gop_get_protocol_name`.

**Шрифт:** `gop_set_font_face(GOP_FONT_CLASSIC | GOP_FONT_CLEAN | GOP_FONT_BOLD)`, по умолчанию `GOP_FONT_CLEAN`. Для консоли ядра шрифт выбирается через `console_gfx_face`.

**Текст ядра:** `gop_clear`, `gop_set_color`, `gop_putc`, `gop_write`, `gop_write_hex`. При отсутствии GOP символы уходят в [VGA](vga.md).

**Примитивы:** `gop_get_pixel`, `gop_put_pixel`, `gop_draw_rect`, `gop_draw_line` (Брезенхэм), `gop_scroll_rect_up`, `gop_blit_cover` (картинка того же размера, что экран), `gop_draw_image_stretch` (масштабирование ближайшим соседом).

**Пакетная отрисовка:** `gop_begin_batch` / `gop_end_batch` и `gop_begin_compose` / `gop_end_compose` увеличивают счётчики вложенности. Пока счётчик не ноль, `maybe_present` ничего не выводит. По выходе из последней области вызывается `gop_present_forced`. Варианты `*_keep` уменьшают счётчик без вывода, это нужно композитору.

**Вывод на экран:** `gop_present`, `gop_present_forced`, `gop_defer_present`, `gop_flush_needed`, `gop_dirty_pending`, `gop_cancel_compose`.

**Работа с передним и задним буферами:** `gop_has_backbuffer`, `gop_copy_back_to_front`, `gop_put_pixel_front`, `gop_backbuffer_row`, `gop_front_write_row`.

**Консоли процессов:** `gop_console_configure(_owner)`, `gop_console_is_active`, `gop_console_putc`, `gop_console_clear`, `gop_console_disable`, `gop_console_release`.

## Двойная буферизация и синхронизация

- Backbuffer выделяется из PMM как непрерывный блок, по 4 байта на пиксель. Заполняется текущим содержимым экрана при первом обращении (`ensure_backbuffer`). Ограничения: не больше 64 млн пикселей и 1 048 576 страниц.
- Все операции рисования пишут в backbuffer и расширяют область грязных пикселей `dirty_expand`.
- `gop_present` копирует грязную область на экран. Если композитор отвечает (`compositor_present` вернул `true`), копирование делает он.
- Ограничение частоты: вывод не чаще раза в `GOP_PRESENT_MIN_INTERVAL_TICKS` (16 мс), отложенный вывод не позже 50 мс (`GOP_DEFERRED_FLUSH_TICKS`). Единица — миллисекунды из `timer_ticks()`.
- Если на экран пишется 32-битный framebuffer и активен SVGA II, после копирования вызывается `svga_update` для той же области.
- Для 16 и 24 бит пикселя есть отдельные пути конвертации (RGB565 и 24-битный порядок байтов).

## Консоли процессов

- Всего 8 слотов `user_consoles`. Слот связывается с PID владельца (`owner_pid`).
- Для вывода выбирается активный слот по цепочке родителей процесса (до 8 уровней). Если активного слота нет, вывод идёт в общую консоль ядра.
- Консоль хранит сетку символов 128 × 64 и позволяет перерисовать содержимое при смене окна.
- Если у владельца есть окно композитора, рисование идёт через `compositor_draw_rect`, а не в backbuffer.
- Доступ защищён `spin_lock_irqsave` на `console_lock`.

## Ограничения

- Pitch для Multiboot2 всегда считается как `pitch / 4`. Для 16- и 24-битного режима через GRUB это даст неверные адреса строк.
- Основной режим — 32 бита. Пути 16 и 24 бита написаны, но проверены только чтением кода.
- `gop_present` считает время в миллисекундах, а константы названы «тиками». Если частота `timer_ticks` изменится, интервалы сдвинутся.
- При разрушении backbuffer (например, `gop_apply_live`) содержимое не переносится, а консоли процессов сбрасываются.
- Глобальные переменные рисования (`cur_x`, `cur_y`, `fg`, `bg`) общие для всех вызовов ядра. Параллельный вывод с разных ядер может смешать текст.
