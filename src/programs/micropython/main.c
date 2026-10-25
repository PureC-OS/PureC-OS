#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/stackctrl.h"
#include "shared/runtime/pyexec.h"

#if MICROPY_ENABLE_COMPILER
void do_str(const char *src, mp_parse_input_kind_t input_kind) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

static int do_file(const char *path) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_file(qstr_from_str(path));
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
        return 0;
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        return 1;
    }
}
#endif

static char *stack_top;
#if MICROPY_ENABLE_GC
static char heap[MICROPY_HEAP_SIZE];
#endif

int main(int argc, char **argv) {
    int stack_dummy;
    stack_top = (char *)&stack_dummy;
    mp_stack_ctrl_init();
    mp_stack_set_limit(32768);

    #if MICROPY_ENABLE_GC
    gc_init(heap, heap + sizeof(heap));
    #endif
    mp_init();
    #if MICROPY_ENABLE_COMPILER
    int rc = 0;
    if (argc > 1) {
        rc = do_file(argv[1]);
    } else {
        #if MICROPY_REPL_EVENT_DRIVEN
        pyexec_event_repl_init();
        for (;;) {
            int c = mp_hal_stdin_rx_chr();
            if (pyexec_event_repl_process_char(c)) {
                break;
            }
        }
        #else
        pyexec_friendly_repl();
        #endif
    }
    #else
    (void)argc;
    (void)argv;
    pyexec_frozen_module("frozentest.py", false);
    #endif
    mp_deinit();
    #if MICROPY_ENABLE_COMPILER
    return rc;
    #else
    return 0;
    #endif
}

#if MICROPY_ENABLE_GC
void gc_collect(void) {
    void *dummy;
    gc_collect_start();
    gc_collect_root(&dummy, ((mp_uint_t)stack_top - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
    gc_collect_end();
}
#endif

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    const char *path = qstr_str(filename);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        mp_raise_OSError(MP_ENOENT);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        size = 0;
    }
    size_t n = (size_t)size;
    char *buf = (char *)m_malloc(n + 1);
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    buf[got] = '\0';
    return mp_lexer_new_from_str_len(filename, buf, got, 0);
}

mp_import_stat_t mp_import_stat(const char *path) {
    char probe[MICROPY_ALLOC_PATH_MAX + 16];
    size_t n = strlen(path);
    if (n + 4 >= sizeof(probe)) {
        return MP_IMPORT_STAT_NO_EXIST;
    }
    memcpy(probe, path, n);
    memcpy(probe + n, ".py", 4);
    FILE *f = fopen(probe, "rb");
    if (f != NULL) {
        fclose(f);
        return MP_IMPORT_STAT_FILE;
    }
    memcpy(probe + n, ".mpy", 5);
    f = fopen(probe, "rb");
    if (f != NULL) {
        fclose(f);
        return MP_IMPORT_STAT_FILE;
    }
    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return MP_IMPORT_STAT_DIR;
    }
    return MP_IMPORT_STAT_NO_EXIST;
}

void nlr_jump_fail(void *val) {
    (void)val;
    printf("nlr_jump_fail\n");
    exit(1);
}

void MP_NORETURN __fatal_error(const char *msg) {
    printf("__fatal_error: %s\n", msg ? msg : "?");
    exit(1);
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    (void)func;
    exit(1);
}
#endif
