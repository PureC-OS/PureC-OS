#include "tty.h"
#include "../../drivers/display/gop.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/interrupts/timer.h"
#include "../diagnostics/klog.h"
#include "../process/scheduler.h"
#include "../process/signal.h"
#include "../sync/spinlock.h"
#include "../syscall/syscall.h"

#define TTY_BUFFER 512
#define TTY_DEFAULT_COLUMNS 80
#define TTY_DEFAULT_ROWS 24
#define TTY_DECISECOND_MS 100

_Static_assert(sizeof(struct tty_termios) == 56, "tty_termios ABI size");

#define TTY_DEFAULTS                                                                                                                                                                           \
  {                                                                                                                                                                                            \
      .iflag = TTY_IFLAG_ICRNL | TTY_IFLAG_IXON,                                                                                                                                               \
      .oflag = TTY_OFLAG_OPOST | TTY_OFLAG_ONLCR,                                                                                                                                              \
      .cflag = TTY_CFLAG_CS8 | TTY_CFLAG_CREAD | TTY_CFLAG_B38400,                                                                                                                             \
      .lflag = TTY_LFLAG_ISIG | TTY_LFLAG_ICANON | TTY_LFLAG_ECHO |                                                                                                                            \
               TTY_LFLAG_ECHOE | TTY_LFLAG_ECHOK | TTY_LFLAG_ECHOCTL |                                                                                                                         \
               TTY_LFLAG_ECHOKE | TTY_LFLAG_IEXTEN,                                                                                                                                            \
      .cc = {[TTY_VINTR] = 3, [TTY_VQUIT] = 28, [TTY_VERASE] = 127, [TTY_VKILL] = 21, [TTY_VEOF] = 4, [TTY_VTIME] = 0, [TTY_VMIN] = 1, [TTY_VSTART] = 17, [TTY_VSTOP] = 19, [TTY_VSUSP] = 26}, \
      .ispeed = TTY_CFLAG_B38400,                                                                                                                                                              \
      .ospeed = TTY_CFLAG_B38400,                                                                                                                                                              \
  }

static const struct tty_termios default_termios = TTY_DEFAULTS;
static struct tty_termios current = TTY_DEFAULTS;

static spinlock_t tty_lock = SPINLOCK_INIT;
static uint8_t input[TTY_BUFFER];
static uint32_t input_head;
static uint32_t input_count;
static bool application_cursor;
static uint32_t owner_pid;
static uint64_t first_feed_tick;
static uint32_t latency_reports;

#define TTY_LATENCY_REPORT_MS 20
#define TTY_LATENCY_REPORT_LIMIT 64

static bool raw_locked(void) { return !(current.lflag & TTY_LFLAG_ICANON); }

static void push_locked(uint8_t byte)
{
  if (input_count >= TTY_BUFFER)
    return;
  if (!input_count)
    first_feed_tick = timer_ticks();
  input[(input_head + input_count) % TTY_BUFFER] = byte;
  input_count++;
}

static void push_text_locked(const char *text)
{
  for (; *text; text++)
    push_locked((uint8_t)*text);
}

static bool pop_locked(uint8_t *out)
{
  if (!input_count)
    return false;
  *out = input[input_head];
  input_head = (input_head + 1) % TTY_BUFFER;
  input_count--;
  return true;
}

static bool is_default_locked(void)
{
  const uint8_t *a = (const uint8_t *)&current;
  const uint8_t *b = (const uint8_t *)&default_termios;
  for (uint32_t i = 0; i < sizeof(current); i++)
  {
    if (a[i] != b[i])
      return false;
  }
  return true;
}

bool tty_raw_mode(void)
{
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  bool raw = raw_locked();
  spin_unlock_irqrestore(&tty_lock, flags);
  return raw;
}

bool tty_output_nl_cr(void)
{
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  bool translate = (current.oflag & TTY_OFLAG_OPOST) &&
                   (current.oflag & TTY_OFLAG_ONLCR);
  spin_unlock_irqrestore(&tty_lock, flags);
  return translate;
}

void tty_get_termios(struct tty_termios *out)
{
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  *out = current;
  spin_unlock_irqrestore(&tty_lock, flags);
}

void tty_set_termios(const struct tty_termios *in, bool flush, uint32_t pid)
{
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  bool was_raw = raw_locked();
  current = *in;
  bool now_raw = raw_locked();
  owner_pid = is_default_locked() ? 0 : pid;
  if (flush || was_raw != now_raw)
  {
    input_head = 0;
    input_count = 0;
  }
  if (!now_raw)
    application_cursor = false;
  spin_unlock_irqrestore(&tty_lock, flags);
  if (was_raw && !now_raw)
    keyboard_flush_legacy();
}

void tty_get_winsize(struct tty_winsize *out)
{
  uint32_t columns = 0, rows = 0, xpixel = 0, ypixel = 0;
  if (!gop_console_get_size(&columns, &rows, &xpixel, &ypixel) || !columns ||
      !rows)
  {
    columns = TTY_DEFAULT_COLUMNS;
    rows = TTY_DEFAULT_ROWS;
    xpixel = 0;
    ypixel = 0;
  }
  out->columns = (uint16_t)columns;
  out->rows = (uint16_t)rows;
  out->xpixel = (uint16_t)xpixel;
  out->ypixel = (uint16_t)ypixel;
}

void tty_set_application_cursor(bool enable)
{
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  application_cursor = enable;
  spin_unlock_irqrestore(&tty_lock, flags);
}

void tty_feed_char(char character)
{
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  if (raw_locked())
    push_locked(character == '\n' ? '\r' : (uint8_t)character);
  spin_unlock_irqrestore(&tty_lock, flags);
}

void tty_feed_sequence(const char *text)
{
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  if (raw_locked())
    push_text_locked(text);
  spin_unlock_irqrestore(&tty_lock, flags);
}

void tty_inject(const char *text) { tty_feed_sequence(text); }

void tty_feed_special(uint8_t key)
{
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  if (raw_locked())
  {
    const char *sequence = 0;
    switch (key)
    {
    case KEYBOARD_SPECIAL_F1:
      sequence = "\033OP";
      break;
    case KEYBOARD_SPECIAL_F2:
      sequence = "\033OQ";
      break;
    case KEYBOARD_SPECIAL_F3:
      sequence = "\033OR";
      break;
    case KEYBOARD_SPECIAL_HOME:
      sequence = application_cursor ? "\033OH" : "\033[H";
      break;
    case KEYBOARD_SPECIAL_END:
      sequence = application_cursor ? "\033OF" : "\033[F";
      break;
    case KEYBOARD_SPECIAL_PAGE_UP:
      sequence = "\033[5~";
      break;
    case KEYBOARD_SPECIAL_PAGE_DOWN:
      sequence = "\033[6~";
      break;
    case KEYBOARD_SPECIAL_LEFT:
      sequence = application_cursor ? "\033OD" : "\033[D";
      break;
    case KEYBOARD_SPECIAL_RIGHT:
      sequence = application_cursor ? "\033OC" : "\033[C";
      break;
    case KEYBOARD_SPECIAL_UP:
      sequence = application_cursor ? "\033OA" : "\033[A";
      break;
    case KEYBOARD_SPECIAL_DOWN:
      sequence = application_cursor ? "\033OB" : "\033[B";
      break;
    case KEYBOARD_SPECIAL_DELETE:
      sequence = "\033[3~";
      break;
    default:
      break;
    }
    if (sequence)
      push_text_locked(sequence);
  }
  spin_unlock_irqrestore(&tty_lock, flags);
}

void tty_flush_input(void)
{
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  input_head = 0;
  input_count = 0;
  spin_unlock_irqrestore(&tty_lock, flags);
}

uint32_t tty_pending(void)
{
  keyboard_poll();
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  uint32_t pending = input_count;
  spin_unlock_irqrestore(&tty_lock, flags);
  return pending;
}

bool tty_input_ready(void)
{
  if (!tty_raw_mode())
    return keyboard_has_key();
  return tty_pending() != 0;
}

static bool translate_input(uint32_t iflag, uint8_t *byte)
{
  if (iflag & TTY_IFLAG_ISTRIP)
    *byte &= 0x7F;
  if (*byte == '\r')
  {
    if (iflag & TTY_IFLAG_IGNCR)
      return false;
    if (iflag & TTY_IFLAG_ICRNL)
      *byte = '\n';
  }
  else if (*byte == '\n' && (iflag & TTY_IFLAG_INLCR))
  {
    *byte = '\r';
  }
  return true;
}

int32_t tty_read(void *buffer, uint32_t count)
{
  if (!count)
    return 0;
  uint8_t *out = (uint8_t *)buffer;
  uint32_t got = 0;
  uint64_t started = timer_ticks();
  uint64_t last = started;
  for (;;)
  {
    keyboard_poll();
    struct tty_termios mode;
    tty_get_termios(&mode);
    uint32_t minimum = mode.cc[TTY_VMIN];
    uint32_t timeout_ms = (uint32_t)mode.cc[TTY_VTIME] * TTY_DECISECOND_MS;
    uint64_t flags = spin_lock_irqsave(&tty_lock);
    uint8_t byte;
    uint64_t fed_at = first_feed_tick;
    bool had_input = input_count != 0;
    while (got < count && pop_locked(&byte))
    {
      if (!translate_input(mode.iflag, &byte))
        continue;
      out[got++] = byte;
      last = timer_ticks();
    }
    spin_unlock_irqrestore(&tty_lock, flags);
    uint64_t now = timer_ticks();
    if (had_input && now - fed_at >= TTY_LATENCY_REPORT_MS &&
        latency_reports < TTY_LATENCY_REPORT_LIMIT)
    {
      latency_reports++;
      klogf(KLOG_INFO, "tty: key waited %u ms in queue before read",
            (uint32_t)(now - fed_at));
    }
    if (got >= count)
      return (int32_t)got;
    if (!minimum && !timeout_ms)
      return (int32_t)got;
    if (minimum && !timeout_ms)
    {
      if (got >= minimum)
        return (int32_t)got;
    }
    else if (!minimum)
    {
      if (got)
        return (int32_t)got;
      if (now - started >= timeout_ms)
        return 0;
    }
    else
    {
      if (got >= minimum)
        return (int32_t)got;
      if (got && now - last >= timeout_ms)
        return (int32_t)got;
    }
    if (signal_pending_current())
      return got ? (int32_t)got : -KERR_INTR;
    scheduler_sleep(1);
  }
}

void tty_process_exit(uint32_t pid)
{
  bool restored = false;
  uint64_t flags = spin_lock_irqsave(&tty_lock);
  if (owner_pid && owner_pid == pid)
  {
    bool was_raw = raw_locked();
    current = default_termios;
    owner_pid = 0;
    application_cursor = false;
    input_head = 0;
    input_count = 0;
    restored = was_raw;
  }
  spin_unlock_irqrestore(&tty_lock, flags);
  if (restored)
    keyboard_flush_legacy();
}
