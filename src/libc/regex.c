#include "include/hosted/regex.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/ctype.h"
#include <stdint.h>
#include <stdbool.h>

enum {
    N_EMPTY, N_CHAR, N_ANY, N_CLASS, N_BOL, N_EOL, N_WORDB, N_NWORDB,
    N_WBEG, N_WEND, N_BREF, N_GROUP, N_CAT, N_ALT, N_REP
};

enum {
    I_CHAR, I_ANY, I_CLASS, I_BOL, I_EOL, I_WORDB, I_NWORDB, I_WBEG, I_WEND,
    I_BREF, I_SPLIT, I_JMP, I_SAVE, I_MARK, I_CHECK, I_REPS, I_MATCH
};

#define RX_INF (-1)
#define RX_MAX_GROUPS 32
#define RX_STEP_LIMIT 20000000L

struct rx_node {
    int type;
    int a, b;
    int left, right;
};

struct rx_inst {
    int op;
    int a, b;
};

struct rx_prog {
    struct rx_inst *code;
    int count, cap;
    uint8_t (*classes)[32];
    int nclasses, class_cap;
    int nmarks;
    int ngroups;
    int cflags;
    bool anchored;
};

struct rx_parser {
    const char *p;
    bool ere;
    bool icase;
    int error;
    struct rx_node *nodes;
    int count, cap;
    struct rx_prog *prog;
    int ngroups;
};

static int rx_node_new(struct rx_parser *ps, int type, int a, int b, int left, int right)
{
    if (ps->count >= ps->cap) {
        int ncap = ps->cap ? ps->cap * 2 : 64;
        struct rx_node *n = realloc(ps->nodes, (size_t)ncap * sizeof(*n));
        if (!n) { ps->error = REG_ESPACE; return -1; }
        ps->nodes = n;
        ps->cap = ncap;
    }
    struct rx_node *n = &ps->nodes[ps->count];
    n->type = type; n->a = a; n->b = b; n->left = left; n->right = right;
    return ps->count++;
}

static int rx_class_new(struct rx_prog *prog)
{
    if (prog->nclasses >= prog->class_cap) {
        int ncap = prog->class_cap ? prog->class_cap * 2 : 8;
        void *n = realloc(prog->classes, (size_t)ncap * 32);
        if (!n) return -1;
        prog->classes = n;
        prog->class_cap = ncap;
    }
    memset(prog->classes[prog->nclasses], 0, 32);
    return prog->nclasses++;
}

static void rx_bit(uint8_t *set, int c) { set[(c & 255) >> 3] |= (uint8_t)(1u << (c & 7)); }
static bool rx_has(const uint8_t *set, int c) { return (set[(c & 255) >> 3] >> (c & 7)) & 1; }

static bool rx_named_class(const char *name, size_t len, int c)
{
    #define RXN(s) (len == sizeof(s) - 1 && memcmp(name, s, len) == 0)
    if (RXN("alpha")) return isalpha(c);
    if (RXN("digit")) return isdigit(c);
    if (RXN("alnum")) return isalnum(c);
    if (RXN("upper")) return isupper(c);
    if (RXN("lower")) return islower(c);
    if (RXN("space")) return isspace(c);
    if (RXN("blank")) return c == ' ' || c == '\t';
    if (RXN("punct")) return ispunct(c);
    if (RXN("print")) return isprint(c);
    if (RXN("graph")) return isgraph(c);
    if (RXN("cntrl")) return iscntrl(c);
    if (RXN("xdigit")) return isxdigit(c);
    if (RXN("word")) return isalnum(c) || c == '_';
    #undef RXN
    return false;
}

static bool rx_known_class(const char *name, size_t len)
{
    static const char *names[] = {"alpha", "digit", "alnum", "upper", "lower", "space", "blank",
                                  "punct", "print", "graph", "cntrl", "xdigit", "word"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strlen(names[i]) == len && memcmp(names[i], name, len) == 0) return true;
    return false;
}

static void rx_class_fill(uint8_t *set, int kind)
{
    for (int c = 0; c < 256; c++) {
        bool in = false;
        if (kind == 'w' || kind == 'W') in = isalnum(c) || c == '_';
        else if (kind == 's' || kind == 'S') in = isspace(c);
        else if (kind == 'd' || kind == 'D') in = isdigit(c);
        if (kind == 'W' || kind == 'S' || kind == 'D') in = !in;
        if (in) rx_bit(set, c);
    }
}

static int rx_parse_bracket(struct rx_parser *ps)
{
    int idx = rx_class_new(ps->prog);
    if (idx < 0) { ps->error = REG_ESPACE; return -1; }
    uint8_t set[32];
    memset(set, 0, sizeof(set));
    bool negate = false;
    if (*ps->p == '^') { negate = true; ps->p++; }
    bool first = true;
    for (;;) {
        unsigned char c = (unsigned char)*ps->p;
        if (!c) { ps->error = 7; return -1; }
        if (c == ']' && !first) { ps->p++; break; }
        first = false;
        if (c == '[' && ps->p[1] == ':') {
            const char *start = ps->p + 2;
            const char *end = strstr(start, ":]");
            if (!end || !rx_known_class(start, (size_t)(end - start))) { ps->error = 4; return -1; }
            for (int k = 0; k < 256; k++)
                if (rx_named_class(start, (size_t)(end - start), k)) rx_bit(set, k);
            ps->p = end + 2;
            continue;
        }
        if ((c == '[' && (ps->p[1] == '.' || ps->p[1] == '='))) {
            char term = ps->p[1];
            const char *start = ps->p + 2;
            if (start[0] && start[1] == term && start[2] == ']') {
                rx_bit(set, (unsigned char)start[0]);
                ps->p = start + 3;
                continue;
            }
            ps->error = 3;
            return -1;
        }
        int lo = c;
        ps->p++;
        if (lo == '\\' && !ps->ere && 0) lo = (unsigned char)*ps->p++;
        if (*ps->p == '-' && ps->p[1] && ps->p[1] != ']') {
            int hi = (unsigned char)ps->p[1];
            ps->p += 2;
            if (hi < lo) { ps->error = 11; return -1; }
            for (int k = lo; k <= hi; k++) rx_bit(set, k);
        } else {
            rx_bit(set, lo);
        }
    }
    if (ps->icase) {
        for (int k = 0; k < 256; k++)
            if (rx_has(set, k)) { rx_bit(set, tolower(k)); rx_bit(set, toupper(k)); }
    }
    if (negate) {
        for (int k = 0; k < 32; k++) set[k] = (uint8_t)~set[k];
        if (ps->prog->cflags & REG_NEWLINE) set['\n' >> 3] &= (uint8_t)~(1u << ('\n' & 7));
        set[0] &= (uint8_t)~1u;
    }
    memcpy(ps->prog->classes[idx], set, 32);
    return rx_node_new(ps, N_CLASS, idx, 0, -1, -1);
}

static int rx_parse_alt(struct rx_parser *ps, int depth);

static bool rx_at_alt_end(struct rx_parser *ps)
{
    const char *p = ps->p;
    if (!*p) return true;
    if (ps->ere) return *p == '|' || *p == ')';
    return p[0] == '\\' && (p[1] == '|' || p[1] == ')');
}

static int rx_escape_class(struct rx_parser *ps, int kind)
{
    int idx = rx_class_new(ps->prog);
    if (idx < 0) { ps->error = REG_ESPACE; return -1; }
    rx_class_fill(ps->prog->classes[idx], kind);
    return rx_node_new(ps, N_CLASS, idx, 0, -1, -1);
}

static int rx_parse_atom(struct rx_parser *ps, int depth, bool at_start)
{
    unsigned char c = (unsigned char)*ps->p;
    if (c == '.') { ps->p++; return rx_node_new(ps, N_ANY, 0, 0, -1, -1); }
    if (c == '[') { ps->p++; return rx_parse_bracket(ps); }
    if (c == '^') {
        if (ps->ere || at_start) { ps->p++; return rx_node_new(ps, N_BOL, 0, 0, -1, -1); }
        ps->p++;
        return rx_node_new(ps, N_CHAR, '^', 0, -1, -1);
    }
    if (c == '$') {
        ps->p++;
        if (ps->ere || rx_at_alt_end(ps)) return rx_node_new(ps, N_EOL, 0, 0, -1, -1);
        return rx_node_new(ps, N_CHAR, '$', 0, -1, -1);
    }
    if (ps->ere && c == '(') {
        ps->p++;
        int group = ++ps->ngroups;
        int inner = (*ps->p == ')') ? rx_node_new(ps, N_EMPTY, 0, 0, -1, -1) : rx_parse_alt(ps, depth + 1);
        if (inner < 0) return -1;
        if (*ps->p != ')') { ps->error = 8; return -1; }
        ps->p++;
        return rx_node_new(ps, N_GROUP, group, 0, inner, -1);
    }
    if (c == '\\') {
        unsigned char e = (unsigned char)ps->p[1];
        if (!e) { ps->error = 5; return -1; }
        ps->p += 2;
        if (!ps->ere && e == '(') {
            int group = ++ps->ngroups;
            int inner = (ps->p[0] == '\\' && ps->p[1] == ')') ? rx_node_new(ps, N_EMPTY, 0, 0, -1, -1) : rx_parse_alt(ps, depth + 1);
            if (inner < 0) return -1;
            if (!(ps->p[0] == '\\' && ps->p[1] == ')')) { ps->error = 8; return -1; }
            ps->p += 2;
            return rx_node_new(ps, N_GROUP, group, 0, inner, -1);
        }
        if (e >= '1' && e <= '9') {
            if (e - '0' > ps->ngroups) { ps->error = 6; return -1; }
            return rx_node_new(ps, N_BREF, e - '0', 0, -1, -1);
        }
        if (e == 'w' || e == 'W' || e == 's' || e == 'S' || e == 'd' || e == 'D') return rx_escape_class(ps, e);
        if (e == 'b') return rx_node_new(ps, N_WORDB, 0, 0, -1, -1);
        if (e == 'B') return rx_node_new(ps, N_NWORDB, 0, 0, -1, -1);
        if (e == '<') return rx_node_new(ps, N_WBEG, 0, 0, -1, -1);
        if (e == '>') return rx_node_new(ps, N_WEND, 0, 0, -1, -1);
        if (e == 'n') return rx_node_new(ps, N_CHAR, '\n', 0, -1, -1);
        if (e == 't') return rx_node_new(ps, N_CHAR, '\t', 0, -1, -1);
        return rx_node_new(ps, N_CHAR, e, 0, -1, -1);
    }
    if (ps->ere && (c == '*' || c == '+' || c == '?') && at_start) {
        ps->p++;
        return rx_node_new(ps, N_CHAR, c, 0, -1, -1);
    }
    if (!ps->ere && c == '*' && at_start) {
        ps->p++;
        return rx_node_new(ps, N_CHAR, '*', 0, -1, -1);
    }
    if (ps->ere && (c == '*' || c == '+' || c == '?' || c == '{')) { ps->error = 13; return -1; }
    ps->p++;
    return rx_node_new(ps, N_CHAR, c, 0, -1, -1);
}

static bool rx_read_int(const char **pp, int *out)
{
    const char *p = *pp;
    if (!isdigit((unsigned char)*p)) return false;
    int v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        if (v > 1000) v = 1000;
        p++;
    }
    *out = v;
    *pp = p;
    return true;
}

static int rx_parse_piece(struct rx_parser *ps, int depth, bool at_start)
{
    int atom = rx_parse_atom(ps, depth, at_start);
    if (atom < 0) return -1;
    for (;;) {
        int min, max;
        const char *p = ps->p;
        if (*p == '*') { min = 0; max = RX_INF; ps->p++; }
        else if (ps->ere && *p == '+') { min = 1; max = RX_INF; ps->p++; }
        else if (ps->ere && *p == '?') { min = 0; max = 1; ps->p++; }
        else if (!ps->ere && p[0] == '\\' && p[1] == '+') { min = 1; max = RX_INF; ps->p += 2; }
        else if (!ps->ere && p[0] == '\\' && p[1] == '?') { min = 0; max = 1; ps->p += 2; }
        else if ((ps->ere && *p == '{') || (!ps->ere && p[0] == '\\' && p[1] == '{')) {
            const char *q = p + (ps->ere ? 1 : 2);
            if (!rx_read_int(&q, &min)) {
                if (ps->ere) { ps->p++; return rx_node_new(ps, N_CAT, 0, 0, atom, rx_node_new(ps, N_CHAR, '{', 0, -1, -1)); }
                ps->error = 10; return -1;
            }
            max = min;
            if (*q == ',') {
                q++;
                if (!rx_read_int(&q, &max)) max = RX_INF;
            }
            if (ps->ere) { if (*q != '}') { ps->error = 9; return -1; } q++; }
            else { if (!(q[0] == '\\' && q[1] == '}')) { ps->error = 9; return -1; } q += 2; }
            if (max != RX_INF && max < min) { ps->error = 10; return -1; }
            ps->p = q;
        } else break;
        int type = ps->nodes[atom].type;
        if (type == N_BOL || type == N_EOL) continue;
        atom = rx_node_new(ps, N_REP, min, max, atom, -1);
        if (atom < 0) return -1;
    }
    return atom;
}

static int rx_parse_cat(struct rx_parser *ps, int depth)
{
    int result = -1;
    bool at_start = true;
    while (!rx_at_alt_end(ps)) {
        int piece = rx_parse_piece(ps, depth, at_start);
        if (piece < 0) return -1;
        at_start = ps->nodes[piece].type == N_BOL;
        result = result < 0 ? piece : rx_node_new(ps, N_CAT, 0, 0, result, piece);
        if (result < 0) return -1;
    }
    if (result < 0) result = rx_node_new(ps, N_EMPTY, 0, 0, -1, -1);
    return result;
}

static int rx_parse_alt(struct rx_parser *ps, int depth)
{
    if (depth > 64) { ps->error = REG_ESPACE; return -1; }
    int left = rx_parse_cat(ps, depth);
    if (left < 0) return -1;
    for (;;) {
        if (ps->ere && *ps->p == '|') ps->p++;
        else if (!ps->ere && ps->p[0] == '\\' && ps->p[1] == '|') ps->p += 2;
        else break;
        int right = rx_parse_cat(ps, depth);
        if (right < 0) return -1;
        left = rx_node_new(ps, N_ALT, 0, 0, left, right);
        if (left < 0) return -1;
    }
    return left;
}

static int rx_emit(struct rx_prog *prog, int op, int a, int b)
{
    if (prog->count >= prog->cap) {
        int ncap = prog->cap ? prog->cap * 2 : 64;
        struct rx_inst *n = realloc(prog->code, (size_t)ncap * sizeof(*n));
        if (!n) return -1;
        prog->code = n;
        prog->cap = ncap;
    }
    prog->code[prog->count].op = op;
    prog->code[prog->count].a = a;
    prog->code[prog->count].b = b;
    return prog->count++;
}

static bool rx_single(const struct rx_node *n)
{
    return n->type == N_CHAR || n->type == N_ANY || n->type == N_CLASS;
}

static bool rx_gen(struct rx_parser *ps, int index)
{
    struct rx_prog *prog = ps->prog;
    const struct rx_node *n = &ps->nodes[index];
    int at;
    switch (n->type) {
    case N_EMPTY: return true;
    case N_CHAR: return rx_emit(prog, I_CHAR, ps->icase ? tolower(n->a) : n->a, 0) >= 0;
    case N_ANY: return rx_emit(prog, I_ANY, 0, 0) >= 0;
    case N_CLASS: return rx_emit(prog, I_CLASS, n->a, 0) >= 0;
    case N_BOL: return rx_emit(prog, I_BOL, 0, 0) >= 0;
    case N_EOL: return rx_emit(prog, I_EOL, 0, 0) >= 0;
    case N_WORDB: return rx_emit(prog, I_WORDB, 0, 0) >= 0;
    case N_NWORDB: return rx_emit(prog, I_NWORDB, 0, 0) >= 0;
    case N_WBEG: return rx_emit(prog, I_WBEG, 0, 0) >= 0;
    case N_WEND: return rx_emit(prog, I_WEND, 0, 0) >= 0;
    case N_BREF: return rx_emit(prog, I_BREF, n->a, 0) >= 0;
    case N_GROUP:
        if (rx_emit(prog, I_SAVE, n->a * 2, 0) < 0) return false;
        if (!rx_gen(ps, n->left)) return false;
        return rx_emit(prog, I_SAVE, n->a * 2 + 1, 0) >= 0;
    case N_CAT:
        return rx_gen(ps, n->left) && rx_gen(ps, n->right);
    case N_ALT: {
        int split = rx_emit(prog, I_SPLIT, 0, 0);
        if (split < 0) return false;
        prog->code[split].a = prog->count;
        if (!rx_gen(ps, n->left)) return false;
        int jmp = rx_emit(prog, I_JMP, 0, 0);
        if (jmp < 0) return false;
        prog->code[split].b = prog->count;
        if (!rx_gen(ps, n->right)) return false;
        prog->code[jmp].a = prog->count;
        return true;
    }
    case N_REP: {
        int min = n->a, max = n->b;
        const struct rx_node *child = &ps->nodes[n->left];
        if (rx_single(child)) {
            if (rx_emit(prog, I_REPS, min, max) < 0) return false;
            return rx_gen(ps, n->left);
        }
        if (prog->count > 200000) return false;
        for (int i = 0; i < min; i++)
            if (!rx_gen(ps, n->left)) return false;
        if (max == RX_INF) {
            int mark = prog->nmarks++;
            int loop = rx_emit(prog, I_SPLIT, 0, 0);
            if (loop < 0) return false;
            prog->code[loop].a = prog->count;
            if (rx_emit(prog, I_MARK, mark, 0) < 0) return false;
            if (!rx_gen(ps, n->left)) return false;
            if (rx_emit(prog, I_CHECK, mark, 0) < 0) return false;
            if (rx_emit(prog, I_JMP, loop, 0) < 0) return false;
            prog->code[loop].b = prog->count;
            return true;
        }
        int pending[1001];
        int npending = 0;
        for (int i = min; i < max && npending < 1000; i++) {
            at = rx_emit(prog, I_SPLIT, 0, 0);
            if (at < 0) return false;
            prog->code[at].a = prog->count;
            pending[npending++] = at;
            if (!rx_gen(ps, n->left)) return false;
        }
        for (int i = 0; i < npending; i++) prog->code[pending[i]].b = prog->count;
        return true;
    }
    }
    return false;
}

int regcomp(regex_t *preg, const char *pattern, int cflags)
{
    if (!preg || !pattern) return REG_BADPAT;
    preg->re_nsub = 0;
    preg->re_impl = NULL;
    struct rx_prog *prog = calloc(1, sizeof(*prog));
    if (!prog) return REG_ESPACE;
    prog->cflags = cflags;
    struct rx_parser ps;
    memset(&ps, 0, sizeof(ps));
    ps.p = pattern;
    ps.ere = (cflags & REG_EXTENDED) != 0;
    ps.icase = (cflags & REG_ICASE) != 0;
    ps.prog = prog;
    int root = rx_parse_alt(&ps, 0);
    if (root >= 0 && *ps.p) ps.error = ps.ere ? 8 : 8;
    if (root < 0 || ps.error) {
        int err = ps.error ? ps.error : REG_BADPAT;
        free(ps.nodes);
        free(prog->classes);
        free(prog);
        return err;
    }
    prog->ngroups = ps.ngroups;
    bool ok = rx_emit(prog, I_SAVE, 0, 0) >= 0 && rx_gen(&ps, root) &&
              rx_emit(prog, I_SAVE, 1, 0) >= 0 && rx_emit(prog, I_MATCH, 0, 0) >= 0;
    prog->anchored = ps.nodes[root].type == N_BOL ||
                     (ps.nodes[root].type == N_CAT && 0);
    int first = root;
    while (ps.nodes[first].type == N_CAT) first = ps.nodes[first].left;
    if (ps.nodes[first].type == N_BOL && !(cflags & REG_NEWLINE)) prog->anchored = true;
    free(ps.nodes);
    if (!ok) {
        free(prog->code);
        free(prog->classes);
        free(prog);
        return REG_ESPACE;
    }
    preg->re_nsub = (size_t)ps.ngroups;
    preg->re_impl = prog;
    preg->re_cflags = cflags;
    return 0;
}

struct rx_exec {
    const struct rx_prog *prog;
    const unsigned char *s;
    long len;
    int eflags;
    long caps[RX_MAX_GROUPS * 2 + 2];
    long marks[64];
    long steps;
    bool icase;
};

static bool rx_isword(int c) { return isalnum(c) || c == '_'; }

static bool rx_atom_match(const struct rx_exec *ex, const struct rx_inst *in, long sp)
{
    if (sp >= ex->len) return false;
    int c = ex->s[sp];
    switch (in->op) {
    case I_CHAR: return (ex->icase ? tolower(c) : c) == in->a;
    case I_ANY: return c != 0 && !((ex->prog->cflags & REG_NEWLINE) && c == '\n');
    case I_CLASS: return rx_has(ex->prog->classes[in->a], c);
    }
    return false;
}

static int rx_run(struct rx_exec *ex, int pc, long sp)
{
    const struct rx_inst *code = ex->prog->code;
    for (;;) {
        if (++ex->steps > RX_STEP_LIMIT) return -1;
        const struct rx_inst *in = &code[pc];
        switch (in->op) {
        case I_CHAR:
        case I_ANY:
        case I_CLASS:
            if (!rx_atom_match(ex, in, sp)) return 0;
            sp++;
            pc++;
            break;
        case I_BOL:
            if (sp == 0 ? (ex->eflags & REG_NOTBOL) == 0
                        : ((ex->prog->cflags & REG_NEWLINE) && ex->s[sp - 1] == '\n')) { pc++; break; }
            return 0;
        case I_EOL:
            if (sp == ex->len ? (ex->eflags & REG_NOTEOL) == 0
                              : ((ex->prog->cflags & REG_NEWLINE) && ex->s[sp] == '\n')) { pc++; break; }
            return 0;
        case I_WORDB:
        case I_NWORDB:
        case I_WBEG:
        case I_WEND: {
            bool before = sp > 0 && rx_isword(ex->s[sp - 1]);
            bool after = sp < ex->len && rx_isword(ex->s[sp]);
            bool ok;
            if (in->op == I_WORDB) ok = before != after;
            else if (in->op == I_NWORDB) ok = before == after;
            else if (in->op == I_WBEG) ok = !before && after;
            else ok = before && !after;
            if (!ok) return 0;
            pc++;
            break;
        }
        case I_BREF: {
            long so = ex->caps[in->a * 2], eo = ex->caps[in->a * 2 + 1];
            if (so < 0 || eo < so) return 0;
            long n = eo - so;
            if (sp + n > ex->len) return 0;
            for (long i = 0; i < n; i++) {
                int x = ex->s[so + i], y = ex->s[sp + i];
                if (ex->icase ? tolower(x) != tolower(y) : x != y) return 0;
            }
            sp += n;
            pc++;
            break;
        }
        case I_SPLIT: {
            int r = rx_run(ex, in->a, sp);
            if (r) return r;
            pc = in->b;
            break;
        }
        case I_JMP:
            pc = in->a;
            break;
        case I_SAVE: {
            long old = ex->caps[in->a];
            ex->caps[in->a] = sp;
            int r = rx_run(ex, pc + 1, sp);
            if (r) return r;
            ex->caps[in->a] = old;
            return 0;
        }
        case I_MARK: {
            if (in->a >= 64) { pc++; break; }
            long old = ex->marks[in->a];
            ex->marks[in->a] = sp;
            int r = rx_run(ex, pc + 1, sp);
            if (r) return r;
            ex->marks[in->a] = old;
            return 0;
        }
        case I_CHECK:
            if (in->a < 64 && ex->marks[in->a] == sp) return 0;
            pc++;
            break;
        case I_REPS: {
            const struct rx_inst *atom = &code[pc + 1];
            long n = 0;
            long limit = in->b == RX_INF ? ex->len - sp : in->b;
            while (n < limit && rx_atom_match(ex, atom, sp + n)) n++;
            if (n < in->a) return 0;
            for (long k = n; k >= in->a; k--) {
                int r = rx_run(ex, pc + 2, sp + k);
                if (r) return r;
            }
            return 0;
        }
        case I_MATCH:
            return 1;
        }
    }
}

int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags)
{
    if (!preg || !preg->re_impl || !string) return REG_BADPAT;
    const struct rx_prog *prog = preg->re_impl;
    struct rx_exec *ex = malloc(sizeof(*ex));
    if (!ex) return REG_ESPACE;
    ex->prog = prog;
    ex->s = (const unsigned char *)string;
    ex->len = (long)strlen(string);
    ex->eflags = eflags;
    ex->icase = (prog->cflags & REG_ICASE) != 0;
    int result = REG_NOMATCH;
    for (long start = 0; start <= ex->len; start++) {
        for (int i = 0; i < RX_MAX_GROUPS * 2 + 2; i++) ex->caps[i] = -1;
        for (int i = 0; i < 64; i++) ex->marks[i] = -1;
        ex->steps = 0;
        int r = rx_run(ex, 0, start);
        if (r > 0) {
            result = 0;
            break;
        }
        if (r < 0) { result = REG_ESPACE; break; }
        if (prog->anchored) break;
    }
    if (result == 0 && !(prog->cflags & REG_NOSUB) && pmatch) {
        for (size_t i = 0; i < nmatch; i++) {
            if (i <= (size_t)prog->ngroups && i < RX_MAX_GROUPS + 1) {
                pmatch[i].rm_so = ex->caps[i * 2];
                pmatch[i].rm_eo = ex->caps[i * 2 + 1];
                if (pmatch[i].rm_so < 0 || pmatch[i].rm_eo < 0) pmatch[i].rm_so = pmatch[i].rm_eo = -1;
            } else {
                pmatch[i].rm_so = pmatch[i].rm_eo = -1;
            }
        }
    }
    free(ex);
    return result;
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
    (void)preg;
    static const char *messages[] = {
        "Success", "No match", "Invalid regular expression", "Invalid collation character",
        "Invalid character class name", "Trailing backslash", "Invalid back reference",
        "Unmatched [ or [^", "Unmatched ( or \\(", "Unmatched \\{", "Invalid content of \\{\\}",
        "Invalid range end", "Memory exhausted", "Invalid preceding regular expression"};
    const char *msg = (errcode >= 0 && errcode < (int)(sizeof(messages) / sizeof(messages[0])))
                          ? messages[errcode] : "Unknown error";
    size_t len = strlen(msg) + 1;
    if (errbuf && errbuf_size) {
        size_t n = len < errbuf_size ? len : errbuf_size;
        memcpy(errbuf, msg, n - 1);
        errbuf[n - 1] = 0;
    }
    return len;
}

void regfree(regex_t *preg)
{
    if (!preg || !preg->re_impl) return;
    struct rx_prog *prog = preg->re_impl;
    free(prog->code);
    free(prog->classes);
    free(prog);
    preg->re_impl = NULL;
}
