//! PureL — простой скриптовый язык для PureC OS.
//!
//! Синтаксис:
//! ```text
//! print "hello", 1 + 2
//! let x = 10
//! x = x - 1
//! if x > 5 then
//!   print x
//! else
//!   print "small"
//! end
//! while x > 0 do
//!   print x
//!   x = x - 1
//! end
//! fun add(a, b)
//!   return a + b
//! end
//! print add(2, 3)
//! ```

#![no_std]
#![no_main]

mod ast;
mod interp;
mod lexer;
mod parser;

use ast::{Arena, StringPool};
use interp::Vm;
use parser::Parser;

const DEMO: &str = r#"
print "PureL on PureC OS"
let n = 5
while n > 0 do
  print "tick", n
  n = n - 1
end
fun square(x)
  return x * x
end
print "7^2 =", square(7)
if true then
  print "ok"
end
"#;

#[no_mangle]
#[link_section = ".text.start"]
pub extern "C" fn _start() -> ! {
    purec::write("PureL 0.1 — type code, empty line to run, 'quit' to exit\n");

    let mut path_buf = [0u8; 128];
    let cmdline_len = purec::command_line(&mut path_buf);
    if cmdline_len > 0 {
        let path = cstr_to_str(&path_buf);
        if !path.is_empty() && path != "pl" {
            // argv0 may be included — take last token as path
            let path = last_token(path);
            if path != "pl" && !path.is_empty() {
                run_file(path);
                purec::exit(0);
            }
        }
    }

    purec::write("--- demo ---\n");
    run_source(DEMO);
    purec::write("--- repl ---\n");
    repl();
    purec::exit(0);
}

fn run_file(path: &str) {
    purec::write("loading ");
    purec::write(path);
    purec::write("\n");
    let fd = purec::file_open(path);
    if fd < 0 {
        purec::write("error: cannot open file\n");
        return;
    }
    let mut buf = [0u8; 4096];
    let n = purec::file_read(fd, &mut buf);
    let _ = purec::file_close(fd);
    if n < 0 {
        purec::write("error: read failed\n");
        return;
    }
    let text = core::str::from_utf8(&buf[..n as usize]).unwrap_or("");
    run_source(text);
}

fn run_source(src: &str) {
    let mut arena = Arena::new();
    let mut strings = StringPool::new();
    let mut parser = Parser::new(src, &mut arena, &mut strings);
    let root = parser.parse_program();
    if let Some(err) = parser.error {
        purec::write("parse error: ");
        purec::write(err);
        purec::write("\n");
        return;
    }
    let mut vm = Vm::new(&arena, &mut strings);
    let _ = vm.exec(root);
    if let Some(err) = vm.error {
        purec::write("runtime error: ");
        purec::write(err);
        purec::write("\n");
    }
}

fn repl() {
    let mut line = [0u8; 256];
    let mut chunk = [0u8; 2048];
    let mut chunk_len = 0usize;

    loop {
        purec::write(if chunk_len == 0 { "> " } else { ". " });
        let n = read_line(&mut line);
        if n < 0 {
            break;
        }
        if n == 0 {
            if chunk_len == 0 {
                continue;
            }
            let src = core::str::from_utf8(&chunk[..chunk_len]).unwrap_or("");
            run_source(src);
            chunk_len = 0;
            continue;
        }
        let text = core::str::from_utf8(&line[..n as usize]).unwrap_or("");
        if text == "quit" || text == "exit" {
            break;
        }
        if text == "help" {
            purec::write("PureL: let, if/then/else/end, while/do/end, fun/end, print, return\n");
            purec::write("Empty line runs buffered statements. quit — exit.\n");
            continue;
        }
        // accumulate
        if chunk_len + n as usize + 1 >= chunk.len() {
            purec::write("buffer full — running\n");
            let src = core::str::from_utf8(&chunk[..chunk_len]).unwrap_or("");
            run_source(src);
            chunk_len = 0;
        }
        chunk[chunk_len..chunk_len + n as usize].copy_from_slice(&line[..n as usize]);
        chunk_len += n as usize;
        chunk[chunk_len] = b'\n';
        chunk_len += 1;

        // single-line auto-run if looks complete (no block open)
        if is_complete(text) {
            let src = core::str::from_utf8(&chunk[..chunk_len]).unwrap_or("");
            run_source(src);
            chunk_len = 0;
        }
    }
}

fn is_complete(line: &str) -> bool {
    // incomplete if starts a block
    let t = line.trim();
    if t.starts_with("if ") || t.starts_with("while ") || t.starts_with("fun ") {
        return false;
    }
    if t == "else" || t.ends_with(" then") || t.ends_with(" do") {
        return false;
    }
    true
}

fn read_line(buf: &mut [u8]) -> i32 {
    let mut i = 0usize;
    loop {
        let c = purec::try_getchar();
        if c < 0 {
            purec::sleep_ms(10);
            continue;
        }
        if c == 10 || c == 13 {
            // echo newline
            purec::write("\n");
            return i as i32;
        }
        if c == 8 || c == 127 {
            if i > 0 {
                i -= 1;
                purec::write("\x08 \x08");
            }
            continue;
        }
        if c >= 32 && c < 127 && i + 1 < buf.len() {
            buf[i] = c as u8;
            i += 1;
            let s = [c as u8, 0];
            // echo char
            let ch = core::str::from_utf8(&s[..1]).unwrap_or("");
            purec::write(ch);
        }
    }
}

fn cstr_to_str(buf: &[u8]) -> &str {
    let mut n = 0;
    while n < buf.len() && buf[n] != 0 {
        n += 1;
    }
    core::str::from_utf8(&buf[..n]).unwrap_or("")
}

fn last_token(s: &str) -> &str {
    let mut last = s;
    for part in s.split(|c: char| c.is_whitespace()) {
        if !part.is_empty() {
            last = part;
        }
    }
    last
}
