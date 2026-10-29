
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

struct buf {
    char *data;
    size_t length;
    size_t capacity;
};

static void *xmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) { fputs("sh: out of memory\n", stderr); _exit(2); }
    return p;
}

static void *xrealloc(void *old, size_t size) {
    void *p = realloc(old, size ? size : 1);
    if (!p) { fputs("sh: out of memory\n", stderr); _exit(2); }
    return p;
}

static char *xstrdup(const char *text) {
    size_t n = strlen(text) + 1;
    char *copy = xmalloc(n);
    memcpy(copy, text, n);
    return copy;
}

static void buf_put(struct buf *b, char c) {
    if (b->length + 2 > b->capacity) {
        b->capacity = b->capacity ? b->capacity * 2 : 64;
        b->data = xrealloc(b->data, b->capacity);
    }
    b->data[b->length++] = c;
    b->data[b->length] = '\0';
}

static void buf_puts(struct buf *b, const char *text) {
    while (*text) buf_put(b, *text++);
}

static char *buf_take(struct buf *b) {
    if (!b->data) return xstrdup("");
    char *d = b->data;
    b->data = 0;
    b->length = b->capacity = 0;
    return d;
}

struct variable {
    char *name;
    char *value;
    bool exported;
    bool readonly;
};

struct function {
    char *name;
    struct node *body;
};

static struct variable *variables;
static size_t variable_count;
static struct function *functions;
static size_t function_count;
static int last_status;
static pid_t last_background;
static pid_t shell_pid;
static char *shell_name = "sh";
static char **positional;
static int positional_count;
static bool opt_errexit;
static bool opt_xtrace;
static bool opt_noglob;
static bool opt_nounset;
static bool interactive;
static int loop_depth;
static int break_count;
static int continue_count;
static bool returning;
static int return_status;
static int function_depth;
static char *exit_trap;
static bool exiting;

static struct variable *var_find(const char *name) {
    for (size_t i = 0; i < variable_count; i++)
        if (strcmp(variables[i].name, name) == 0) return &variables[i];
    return 0;
}

static const char *var_get(const char *name) {
    struct variable *v = var_find(name);
    return v ? v->value : 0;
}

static int var_set(const char *name, const char *value, bool export_it) {
    struct variable *v = var_find(name);
    if (v) {
        if (v->readonly) {
            fprintf(stderr, "sh: %s: readonly variable\n", name);
            return 1;
        }
        free(v->value);
        v->value = xstrdup(value);
        if (export_it) v->exported = true;
        return 0;
    }
    variables = xrealloc(variables, (variable_count + 1) * sizeof(*variables));
    v = &variables[variable_count++];
    v->name = xstrdup(name);
    v->value = xstrdup(value);
    v->exported = export_it;
    v->readonly = false;
    return 0;
}

static void var_unset(const char *name) {
    for (size_t i = 0; i < variable_count; i++) {
        if (strcmp(variables[i].name, name) != 0) continue;
        if (variables[i].readonly) return;
        free(variables[i].name);
        free(variables[i].value);
        variables[i] = variables[--variable_count];
        return;
    }
}

static char **build_environment(void) {
    char **env = xmalloc((variable_count + 1) * sizeof(char *));
    size_t n = 0;
    for (size_t i = 0; i < variable_count; i++) {
        if (!variables[i].exported) continue;
        size_t len = strlen(variables[i].name) + strlen(variables[i].value) + 2;
        char *entry = xmalloc(len);
        snprintf(entry, len, "%s=%s", variables[i].name, variables[i].value);
        env[n++] = entry;
    }
    env[n] = 0;
    return env;
}

static void free_environment(char **env) {
    for (size_t i = 0; env[i]; i++) free(env[i]);
    free(env);
}

static void import_environment(void) {
    for (size_t i = 0; environ && environ[i]; i++) {
        const char *eq = strchr(environ[i], '=');
        if (!eq || eq == environ[i]) continue;
        char name[128];
        size_t n = (size_t)(eq - environ[i]);
        if (n >= sizeof(name)) continue;
        memcpy(name, environ[i], n);
        name[n] = '\0';
        var_set(name, eq + 1, true);
    }
}

static struct function *func_find(const char *name) {
    for (size_t i = 0; i < function_count; i++)
        if (strcmp(functions[i].name, name) == 0) return &functions[i];
    return 0;
}

static bool valid_name(const char *s) {
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (size_t i = 1; s[i]; i++)
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return false;
    return true;
}

enum token_type {
    T_EOF, T_WORD, T_NEWLINE, T_PIPE, T_OR, T_AND, T_SEMI, T_DSEMI, T_AMP,
    T_LPAREN, T_RPAREN, T_LESS, T_GREAT, T_DGREAT, T_DLESS, T_LESSAND,
    T_GREATAND, T_LESSGREAT, T_IONUMBER
};

struct token {
    enum token_type type;
    char *text;
};

struct lexer {
    const char *input;
    size_t position;
    struct token pending;
    bool has_pending;
    struct heredoc_wait *heredocs;
    char *error;
};

struct redirect {
    int fd;
    enum token_type kind;
    char *target;
    char *body;
    bool body_expand;
    struct redirect *next;
};

struct heredoc_wait {
    struct redirect *redirect;
    char *delimiter;
    bool strip_tabs;
    struct heredoc_wait *next;
};

static void lex_error(struct lexer *lx, const char *message) {
    if (!lx->error) lx->error = xstrdup(message);
}

static void read_heredoc_bodies(struct lexer *lx) {
    struct heredoc_wait *w = lx->heredocs;
    lx->heredocs = 0;
    struct heredoc_wait *reversed = 0;
    while (w) {
        struct heredoc_wait *next = w->next;
        w->next = reversed;
        reversed = w;
        w = next;
    }
    for (w = reversed; w; w = w->next) {
        struct buf body = {0};
        for (;;) {
            if (!lx->input[lx->position]) break;
            size_t start = lx->position;
            while (lx->input[lx->position] && lx->input[lx->position] != '\n')
                lx->position++;
            size_t end = lx->position;
            if (lx->input[lx->position] == '\n') lx->position++;
            const char *line = lx->input + start;
            size_t length = end - start;
            if (w->strip_tabs) {
                while (length && *line == '\t') { line++; length--; }
            }
            if (length == strlen(w->delimiter) &&
                strncmp(line, w->delimiter, length) == 0)
                break;
            for (size_t i = 0; i < length; i++) buf_put(&body, line[i]);
            buf_put(&body, '\n');
        }
        w->redirect->body = buf_take(&body);
    }
}

static bool is_operator_start(char c) {
    return c == '|' || c == '&' || c == ';' || c == '(' || c == ')' ||
           c == '<' || c == '>';
}

static void lex_copy_dollar_paren(struct lexer *lx, struct buf *word) {
    const char *s = lx->input;
    buf_put(word, s[lx->position++]);
    int depth = 0;
    while (s[lx->position]) {
        char d = s[lx->position];
        if (d == '\'') {
            buf_put(word, d);
            lx->position++;
            while (s[lx->position] && s[lx->position] != '\'')
                buf_put(word, s[lx->position++]);
            if (!s[lx->position]) return;
            buf_put(word, s[lx->position++]);
            continue;
        }
        if (d == '"') {
            buf_put(word, d);
            lx->position++;
            while (s[lx->position] && s[lx->position] != '"') {
                if (s[lx->position] == '\\' && s[lx->position + 1])
                    buf_put(word, s[lx->position++]);
                buf_put(word, s[lx->position++]);
            }
            if (!s[lx->position]) return;
            buf_put(word, s[lx->position++]);
            continue;
        }
        if (d == '\\' && s[lx->position + 1]) {
            buf_put(word, d);
            buf_put(word, s[lx->position + 1]);
            lx->position += 2;
            continue;
        }
        if (d == '(') depth++;
        else if (d == ')') depth--;
        buf_put(word, d);
        lx->position++;
        if (depth == 0) return;
    }
}

static struct token lex_next(struct lexer *lx) {
    struct token t = {T_EOF, 0};
    const char *s = lx->input;
    for (;;) {
        while (s[lx->position] == ' ' || s[lx->position] == '\t') lx->position++;
        if (s[lx->position] == '\\' && s[lx->position + 1] == '\n') {
            lx->position += 2;
            continue;
        }
        if (s[lx->position] == '#') {
            while (s[lx->position] && s[lx->position] != '\n') lx->position++;
            continue;
        }
        break;
    }
    char c = s[lx->position];
    if (!c) return t;
    if (c == '\n') {
        lx->position++;
        t.type = T_NEWLINE;
        if (lx->heredocs) read_heredoc_bodies(lx);
        return t;
    }
    if (is_operator_start(c)) {
        char n = s[lx->position + 1];
        lx->position++;
        switch (c) {
        case '|':
            if (n == '|') { lx->position++; t.type = T_OR; } else t.type = T_PIPE;
            break;
        case '&':
            if (n == '&') { lx->position++; t.type = T_AND; } else t.type = T_AMP;
            break;
        case ';':
            if (n == ';') { lx->position++; t.type = T_DSEMI; } else t.type = T_SEMI;
            break;
        case '(': t.type = T_LPAREN; break;
        case ')': t.type = T_RPAREN; break;
        case '<':
            if (n == '<') { lx->position++; t.type = T_DLESS; }
            else if (n == '&') { lx->position++; t.type = T_LESSAND; }
            else if (n == '>') { lx->position++; t.type = T_LESSGREAT; }
            else t.type = T_LESS;
            break;
        case '>':
            if (n == '>') { lx->position++; t.type = T_DGREAT; }
            else if (n == '&') { lx->position++; t.type = T_GREATAND; }
            else t.type = T_GREAT;
            break;
        }
        return t;
    }
    struct buf word = {0};
    bool all_digits = true;
    while (s[lx->position]) {
        c = s[lx->position];
        if (c == ' ' || c == '\t' || c == '\n' || is_operator_start(c)) break;
        if (!isdigit((unsigned char)c)) all_digits = false;
        if (c == '\\') {
            if (s[lx->position + 1] == '\n') { lx->position += 2; continue; }
            buf_put(&word, c);
            lx->position++;
            if (s[lx->position]) buf_put(&word, s[lx->position++]);
            all_digits = false;
            continue;
        }
        if (c == '\'') {
            buf_put(&word, c);
            lx->position++;
            while (s[lx->position] && s[lx->position] != '\'')
                buf_put(&word, s[lx->position++]);
            if (!s[lx->position]) { lex_error(lx, "unterminated quote"); break; }
            buf_put(&word, s[lx->position++]);
            all_digits = false;
            continue;
        }
        if (c == '"') {
            buf_put(&word, c);
            lx->position++;
            while (s[lx->position] && s[lx->position] != '"') {
                if (s[lx->position] == '\\' && s[lx->position + 1]) {
                    buf_put(&word, s[lx->position++]);
                    buf_put(&word, s[lx->position++]);
                } else if (s[lx->position] == '$' && s[lx->position + 1] == '(') {
                    lex_copy_dollar_paren(lx, &word);
                } else if (s[lx->position] == '`') {
                    buf_put(&word, s[lx->position++]);
                    while (s[lx->position] && s[lx->position] != '`') {
                        if (s[lx->position] == '\\' && s[lx->position + 1])
                            buf_put(&word, s[lx->position++]);
                        buf_put(&word, s[lx->position++]);
                    }
                    if (s[lx->position]) buf_put(&word, s[lx->position++]);
                } else {
                    buf_put(&word, s[lx->position++]);
                }
            }
            if (!s[lx->position]) { lex_error(lx, "unterminated quote"); break; }
            buf_put(&word, s[lx->position++]);
            all_digits = false;
            continue;
        }
        if (c == '`') {
            buf_put(&word, c);
            lx->position++;
            while (s[lx->position] && s[lx->position] != '`') {
                if (s[lx->position] == '\\' && s[lx->position + 1])
                    buf_put(&word, s[lx->position++]);
                buf_put(&word, s[lx->position++]);
            }
            if (!s[lx->position]) { lex_error(lx, "unterminated backquote"); break; }
            buf_put(&word, s[lx->position++]);
            all_digits = false;
            continue;
        }
        if (c == '$' && s[lx->position + 1] == '(') {
            lex_copy_dollar_paren(lx, &word);
            all_digits = false;
            continue;
        }
        if (c == '$' && s[lx->position + 1] == '{') {
            while (s[lx->position] && s[lx->position] != '}')
                buf_put(&word, s[lx->position++]);
            if (s[lx->position]) buf_put(&word, s[lx->position++]);
            all_digits = false;
            continue;
        }
        buf_put(&word, c);
        lx->position++;
    }
    t.text = buf_take(&word);
    t.type = T_WORD;
    if (all_digits && (s[lx->position] == '<' || s[lx->position] == '>'))
        t.type = T_IONUMBER;
    return t;
}

static struct token peek(struct lexer *lx) {
    if (!lx->has_pending) {
        lx->pending = lex_next(lx);
        lx->has_pending = true;
    }
    return lx->pending;
}

static struct token take(struct lexer *lx) {
    struct token t = peek(lx);
    lx->has_pending = false;
    return t;
}

enum node_type {
    N_SIMPLE, N_PIPELINE, N_AND, N_OR, N_LIST, N_BACKGROUND, N_IF, N_WHILE,
    N_UNTIL, N_FOR, N_CASE, N_GROUP, N_SUBSHELL, N_FUNCTION, N_NOT
};

struct case_item {
    char **patterns;
    int pattern_count;
    struct node *body;
    struct case_item *next;
};

struct node {
    enum node_type type;
    char **words;
    int word_count;
    struct redirect *redirects;
    struct node *left;
    struct node *right;
    struct node *third;
    char *name;
    struct case_item *items;
    struct node **stages;
    int stage_count;
};

static struct node *new_node(enum node_type type) {
    struct node *n = xmalloc(sizeof(*n));
    memset(n, 0, sizeof(*n));
    n->type = type;
    return n;
}

struct parser {
    struct lexer lexer;
    char *error;
};

static void parse_error(struct parser *p, const char *message) {
    if (!p->error) p->error = xstrdup(message);
}

static bool is_reserved(const char *w) {
    static const char *list[] = {"if", "then", "else", "elif", "fi", "do", "done",
                                 "case", "esac", "while", "until", "for", "in",
                                 "{", "}", "!"};
    for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); i++)
        if (strcmp(w, list[i]) == 0) return true;
    return false;
}

static bool peek_word_is(struct parser *p, const char *word) {
    struct token t = peek(&p->lexer);
    return t.type == T_WORD && strcmp(t.text, word) == 0;
}

static void skip_newlines(struct parser *p) {
    while (peek(&p->lexer).type == T_NEWLINE) take(&p->lexer);
}

static struct node *parse_list(struct parser *p, bool stop_on_reserved);
static struct node *parse_and_or(struct parser *p);

static bool expect_word(struct parser *p, const char *word) {
    skip_newlines(p);
    if (!peek_word_is(p, word)) {
        char message[96];
        snprintf(message, sizeof(message), "expected '%s'", word);
        parse_error(p, message);
        return false;
    }
    take(&p->lexer);
    return true;
}

static struct redirect *parse_redirect(struct parser *p, int explicit_fd) {
    struct token op = take(&p->lexer);
    struct redirect *r = xmalloc(sizeof(*r));
    memset(r, 0, sizeof(*r));
    r->kind = op.type;
    r->fd = explicit_fd >= 0 ? explicit_fd
                             : ((op.type == T_LESS || op.type == T_LESSAND ||
                                 op.type == T_DLESS || op.type == T_LESSGREAT)
                                    ? 0 : 1);
    struct token target = peek(&p->lexer);
    if (target.type != T_WORD) {
        parse_error(p, "missing redirection target");
        return r;
    }
    take(&p->lexer);
    r->target = target.text;
    if (op.type == T_DLESS) {
        struct heredoc_wait *w = xmalloc(sizeof(*w));
        bool quoted = strchr(target.text, '\'') || strchr(target.text, '"') ||
                      strchr(target.text, '\\');
        struct buf plain = {0};
        for (const char *c = target.text; *c; c++)
            if (*c != '\'' && *c != '"' && *c != '\\') buf_put(&plain, *c);
        w->delimiter = buf_take(&plain);
        w->redirect = r;
        w->strip_tabs = false;
        w->next = p->lexer.heredocs;
        p->lexer.heredocs = w;
        r->body_expand = !quoted;
    }
    return r;
}

static bool is_redirect_token(enum token_type t) {
    return t == T_LESS || t == T_GREAT || t == T_DGREAT || t == T_DLESS ||
           t == T_LESSAND || t == T_GREATAND || t == T_LESSGREAT;
}

static void parse_redirects(struct parser *p, struct redirect **tail_head) {
    struct redirect **tail = tail_head;
    while (*tail) tail = &(*tail)->next;
    for (;;) {
        struct token t = peek(&p->lexer);
        int fd = -1;
        if (t.type == T_IONUMBER) {
            take(&p->lexer);
            fd = atoi(t.text);
            t = peek(&p->lexer);
            if (!is_redirect_token(t.type)) {
                parse_error(p, "bad redirection");
                return;
            }
        }
        if (!is_redirect_token(t.type)) return;
        struct redirect *r = parse_redirect(p, fd);
        *tail = r;
        tail = &r->next;
        if (p->error) return;
    }
}

static struct node *parse_if(struct parser *p) {
    struct node *n = new_node(N_IF);
    n->left = parse_list(p, true);
    if (!expect_word(p, "then")) return n;
    n->right = parse_list(p, true);
    skip_newlines(p);
    if (peek_word_is(p, "elif")) {
        take(&p->lexer);
        n->third = parse_if(p);
        return n;
    }
    if (peek_word_is(p, "else")) {
        take(&p->lexer);
        n->third = parse_list(p, true);
    }
    expect_word(p, "fi");
    return n;
}

static struct node *parse_for(struct parser *p) {
    struct node *n = new_node(N_FOR);
    struct token name = take(&p->lexer);
    if (name.type != T_WORD || !valid_name(name.text)) {
        parse_error(p, "bad for loop variable");
        return n;
    }
    n->name = name.text;
    skip_newlines(p);
    if (peek_word_is(p, "in")) {
        take(&p->lexer);
        while (peek(&p->lexer).type == T_WORD) {
            struct token w = take(&p->lexer);
            n->words = xrealloc(n->words, (size_t)(n->word_count + 1) * sizeof(char *));
            n->words[n->word_count++] = w.text;
        }
    } else {
        n->name = name.text;
        n->word_count = -1;
    }
    struct token sep = peek(&p->lexer);
    if (sep.type == T_SEMI || sep.type == T_NEWLINE) take(&p->lexer);
    if (!expect_word(p, "do")) return n;
    n->left = parse_list(p, true);
    expect_word(p, "done");
    return n;
}

static struct node *parse_case(struct parser *p) {
    struct node *n = new_node(N_CASE);
    struct token subject = take(&p->lexer);
    if (subject.type != T_WORD) {
        parse_error(p, "bad case subject");
        return n;
    }
    n->name = subject.text;
    skip_newlines(p);
    if (!expect_word(p, "in")) return n;
    struct case_item **tail = &n->items;
    for (;;) {
        skip_newlines(p);
        if (peek_word_is(p, "esac")) { take(&p->lexer); break; }
        if (peek(&p->lexer).type == T_EOF) { parse_error(p, "unterminated case"); break; }
        if (peek(&p->lexer).type == T_LPAREN) take(&p->lexer);
        struct case_item *item = xmalloc(sizeof(*item));
        memset(item, 0, sizeof(*item));
        for (;;) {
            struct token pat = take(&p->lexer);
            if (pat.type != T_WORD) { parse_error(p, "bad case pattern"); return n; }
            item->patterns = xrealloc(item->patterns,
                                      (size_t)(item->pattern_count + 1) * sizeof(char *));
            item->patterns[item->pattern_count++] = pat.text;
            if (peek(&p->lexer).type == T_PIPE) { take(&p->lexer); continue; }
            break;
        }
        if (peek(&p->lexer).type != T_RPAREN) { parse_error(p, "expected ')'"); return n; }
        take(&p->lexer);
        item->body = parse_list(p, true);
        *tail = item;
        tail = &item->next;
        skip_newlines(p);
        if (peek(&p->lexer).type == T_DSEMI) take(&p->lexer);
    }
    return n;
}

static struct node *parse_command(struct parser *p) {
    struct token t = peek(&p->lexer);
    struct node *n = 0;
    if (t.type == T_LPAREN) {
        take(&p->lexer);
        n = new_node(N_SUBSHELL);
        n->left = parse_list(p, true);
        if (peek(&p->lexer).type != T_RPAREN) parse_error(p, "expected ')'");
        else take(&p->lexer);
        parse_redirects(p, &n->redirects);
        return n;
    }
    if (t.type == T_WORD) {
        if (strcmp(t.text, "{") == 0) {
            take(&p->lexer);
            n = new_node(N_GROUP);
            n->left = parse_list(p, true);
            expect_word(p, "}");
            parse_redirects(p, &n->redirects);
            return n;
        }
        if (strcmp(t.text, "if") == 0) {
            take(&p->lexer);
            n = parse_if(p);
            parse_redirects(p, &n->redirects);
            return n;
        }
        if (strcmp(t.text, "while") == 0 || strcmp(t.text, "until") == 0) {
            take(&p->lexer);
            n = new_node(strcmp(t.text, "while") == 0 ? N_WHILE : N_UNTIL);
            n->left = parse_list(p, true);
            if (!expect_word(p, "do")) return n;
            n->right = parse_list(p, true);
            expect_word(p, "done");
            parse_redirects(p, &n->redirects);
            return n;
        }
        if (strcmp(t.text, "for") == 0) {
            take(&p->lexer);
            n = parse_for(p);
            parse_redirects(p, &n->redirects);
            return n;
        }
        if (strcmp(t.text, "case") == 0) {
            take(&p->lexer);
            n = parse_case(p);
            parse_redirects(p, &n->redirects);
            return n;
        }
    }
    n = new_node(N_SIMPLE);
    for (;;) {
        t = peek(&p->lexer);
        if (t.type == T_IONUMBER || is_redirect_token(t.type)) {
            parse_redirects(p, &n->redirects);
            if (p->error) return n;
            continue;
        }
        if (t.type != T_WORD) break;
        if (n->word_count == 0 && is_reserved(t.text) && strcmp(t.text, "!") != 0 &&
            strcmp(t.text, "{") != 0)
            break;
        take(&p->lexer);
        n->words = xrealloc(n->words, (size_t)(n->word_count + 1) * sizeof(char *));
        n->words[n->word_count++] = t.text;
        if (n->word_count == 1 && peek(&p->lexer).type == T_LPAREN &&
            valid_name(t.text)) {
            take(&p->lexer);
            if (peek(&p->lexer).type != T_RPAREN) {
                parse_error(p, "bad function definition");
                return n;
            }
            take(&p->lexer);
            skip_newlines(p);
            struct node *fn = new_node(N_FUNCTION);
            fn->name = t.text;
            fn->left = parse_command(p);
            return fn;
        }
    }
    if (n->word_count == 0 && !n->redirects) {
        parse_error(p, "syntax error near unexpected token");
    }
    return n;
}

static struct node *parse_pipeline(struct parser *p) {
    bool negate = false;
    if (peek_word_is(p, "!")) { take(&p->lexer); negate = true; }
    struct node *first = parse_command(p);
    struct node *result = first;
    if (peek(&p->lexer).type == T_PIPE) {
        struct node *pipeline = new_node(N_PIPELINE);
        pipeline->stages = xmalloc(sizeof(struct node *));
        pipeline->stages[0] = first;
        pipeline->stage_count = 1;
        while (peek(&p->lexer).type == T_PIPE) {
            take(&p->lexer);
            skip_newlines(p);
            pipeline->stages = xrealloc(pipeline->stages,
                                        (size_t)(pipeline->stage_count + 1) * sizeof(struct node *));
            pipeline->stages[pipeline->stage_count++] = parse_command(p);
            if (p->error) break;
        }
        result = pipeline;
    }
    if (negate) {
        struct node *n = new_node(N_NOT);
        n->left = result;
        return n;
    }
    return result;
}

static struct node *parse_and_or(struct parser *p) {
    struct node *left = parse_pipeline(p);
    for (;;) {
        enum token_type t = peek(&p->lexer).type;
        if (t != T_AND && t != T_OR) break;
        take(&p->lexer);
        skip_newlines(p);
        struct node *n = new_node(t == T_AND ? N_AND : N_OR);
        n->left = left;
        n->right = parse_pipeline(p);
        left = n;
        if (p->error) break;
    }
    return left;
}

static bool at_list_end(struct parser *p, bool stop_on_reserved) {
    struct token t = peek(&p->lexer);
    if (t.type == T_EOF || t.type == T_RPAREN || t.type == T_DSEMI) return true;
    if (stop_on_reserved && t.type == T_WORD) {
        static const char *stops[] = {"then", "else", "elif", "fi", "do", "done",
                                      "esac", "}"};
        for (size_t i = 0; i < sizeof(stops) / sizeof(stops[0]); i++)
            if (strcmp(t.text, stops[i]) == 0) return true;
    }
    return false;
}

static struct node *parse_list(struct parser *p, bool stop_on_reserved) {
    struct node *list = 0;
    struct node **tail = &list;
    skip_newlines(p);
    while (!at_list_end(p, stop_on_reserved) && !p->error) {
        struct node *item = parse_and_or(p);
        enum token_type sep = peek(&p->lexer).type;
        if (sep == T_AMP) {
            take(&p->lexer);
            struct node *bg = new_node(N_BACKGROUND);
            bg->left = item;
            item = bg;
        } else if (sep == T_SEMI || sep == T_NEWLINE) {
            take(&p->lexer);
        }
        if (!list) {
            list = item;
            tail = &list;
        } else {
            if ((*tail)->type != N_LIST || (*tail) == item) {
                struct node *seq = new_node(N_LIST);
                seq->left = *tail;
                seq->right = item;
                *tail = seq;
            } else {
                struct node *seq = new_node(N_LIST);
                seq->left = *tail;
                seq->right = item;
                *tail = seq;
            }
        }
        skip_newlines(p);
    }
    return list;
}

static bool match_pattern(const char *pattern, const char *text, bool leading_dot_guard);

static bool match_bracket(const char **pattern, char c, bool *ok) {
    const char *p = *pattern + 1;
    bool negate = false;
    bool matched = false;
    if (*p == '!' || *p == '^') { negate = true; p++; }
    bool first = true;
    while (*p && (*p != ']' || first)) {
        first = false;
        unsigned char lo = (unsigned char)*p;
        if (*p == '\\' && p[1]) { p++; lo = (unsigned char)*p; }
        if (p[1] == '-' && p[2] && p[2] != ']') {
            unsigned char hi = (unsigned char)p[2];
            if ((unsigned char)c >= lo && (unsigned char)c <= hi) matched = true;
            p += 3;
        } else {
            if ((unsigned char)c == lo) matched = true;
            p++;
        }
    }
    if (*p != ']') { *ok = false; return false; }
    *pattern = p + 1;
    *ok = true;
    return matched != negate;
}

static bool match_pattern(const char *pattern, const char *text, bool dot_guard) {
    while (*pattern) {
        if (*pattern == '*') {
            while (*pattern == '*') pattern++;
            if (dot_guard && *text == '.') return false;
            if (!*pattern) return true;
            for (const char *t = text;; t++) {
                if (match_pattern(pattern, t, false)) return true;
                if (!*t) return false;
            }
        }
        if (!*text) return false;
        if (*pattern == '?') {
            if (dot_guard && *text == '.') return false;
            pattern++;
            text++;
        } else if (*pattern == '[') {
            bool ok;
            const char *after = pattern;
            bool hit = match_bracket(&after, *text, &ok);
            if (!ok) {
                if (*text != '[') return false;
                pattern++;
                text++;
            } else {
                if (!hit) return false;
                if (dot_guard && *text == '.') return false;
                pattern = after;
                text++;
            }
        } else if (*pattern == '\\' && pattern[1]) {
            if (pattern[1] != *text) return false;
            pattern += 2;
            text++;
        } else {
            if (*pattern != *text) return false;
            pattern++;
            text++;
        }
        dot_guard = false;
    }
    return !*text;
}

struct cell {
    char c;
    unsigned char flags;
};

#define CELL_QUOTED 1
#define CELL_EXPANDED 2
#define CELL_BREAK 4

struct cells {
    struct cell *items;
    size_t count;
    size_t capacity;
};

static void cells_put(struct cells *cs, char c, unsigned char flags) {
    if (cs->count + 1 > cs->capacity) {
        cs->capacity = cs->capacity ? cs->capacity * 2 : 32;
        cs->items = xrealloc(cs->items, cs->capacity * sizeof(struct cell));
    }
    cs->items[cs->count].c = c;
    cs->items[cs->count].flags = flags;
    cs->count++;
}

static void cells_puts(struct cells *cs, const char *text, unsigned char flags) {
    for (; *text; text++) cells_put(cs, *text, flags);
}

struct field_list {
    char **items;
    int count;
};

static void fields_add(struct field_list *fl, char *text) {
    fl->items = xrealloc(fl->items, (size_t)(fl->count + 1) * sizeof(char *));
    fl->items[fl->count++] = text;
}

static int run_string(const char *text);
static int run_node(struct node *n);
static char *command_substitute(const char *command);
static long arith_eval(const char *expr);

static const char *special_param(char name, char *scratch, size_t size) {
    switch (name) {
    case '?': snprintf(scratch, size, "%d", last_status); return scratch;
    case '$': snprintf(scratch, size, "%d", (int)shell_pid); return scratch;
    case '!': snprintf(scratch, size, "%d", (int)last_background); return scratch;
    case '#': snprintf(scratch, size, "%d", positional_count); return scratch;
    case '-': {
        size_t n = 0;
        if (opt_errexit) scratch[n++] = 'e';
        if (opt_xtrace) scratch[n++] = 'x';
        if (opt_noglob) scratch[n++] = 'f';
        if (opt_nounset) scratch[n++] = 'u';
        if (interactive) scratch[n++] = 'i';
        scratch[n] = '\0';
        return scratch;
    }
    case '0': return shell_name;
    default: break;
    }
    return 0;
}

static const char *lookup_param(const char *name, char *scratch, size_t size) {
    if (name[0] && !name[1]) {
        const char *s = special_param(name[0], scratch, size);
        if (s) return s;
    }
    if (isdigit((unsigned char)name[0])) {
        int index = atoi(name);
        if (index == 0) return shell_name;
        if (index <= positional_count) return positional[index - 1];
        return 0;
    }
    return var_get(name);
}

static char *strip_affix(const char *value, const char *pattern, bool suffix,
                         bool longest) {
    size_t length = strlen(value);
    if (!suffix) {
        if (longest) {
            for (size_t i = length + 1; i-- > 0;) {
                char *candidate = xstrdup(value);
                candidate[i] = '\0';
                bool ok = match_pattern(pattern, candidate, false);
                free(candidate);
                if (ok) return xstrdup(value + i);
            }
        } else {
            for (size_t i = 0; i <= length; i++) {
                char *candidate = xstrdup(value);
                candidate[i] = '\0';
                bool ok = match_pattern(pattern, candidate, false);
                free(candidate);
                if (ok) return xstrdup(value + i);
            }
        }
    } else {
        if (longest) {
            for (size_t i = 0; i <= length; i++)
                if (match_pattern(pattern, value + i, false)) {
                    char *result = xstrdup(value);
                    result[i] = '\0';
                    return result;
                }
        } else {
            for (size_t i = length + 1; i-- > 0;)
                if (match_pattern(pattern, value + i, false)) {
                    char *result = xstrdup(value);
                    result[i] = '\0';
                    return result;
                }
        }
    }
    return xstrdup(value);
}

static size_t arith_group_end(const char *raw, size_t start) {
    int depth = 2;
    size_t j = start;
    while (raw[j] && depth > 0) {
        if (raw[j] == '(') depth++;
        else if (raw[j] == ')') depth--;
        j++;
    }
    return j;
}

static char *expand_scalar(const char *raw);

static void expand_braced(const char *body, struct cells *out, unsigned char flags) {
    char scratch[64];
    char name[128];
    size_t n = 0;
    bool length_op = false;
    const char *p = body;
    if (*p == '#' && p[1]) { length_op = true; p++; }
    if (isdigit((unsigned char)*p)) {
        while (isdigit((unsigned char)*p) && n + 1 < sizeof(name)) name[n++] = *p++;
    } else if (strchr("?$!#-@*", *p) && !(isalpha((unsigned char)*p))) {
        name[n++] = *p++;
    } else {
        while ((isalnum((unsigned char)*p) || *p == '_') && n + 1 < sizeof(name))
            name[n++] = *p++;
    }
    name[n] = '\0';
    const char *value = lookup_param(name, scratch, sizeof(scratch));
    if (length_op) {
        char number[32];
        snprintf(number, sizeof(number), "%d", (int)(value ? strlen(value) : 0));
        cells_puts(out, number, flags);
        return;
    }
    if (!*p) {
        if (!value && opt_nounset) {
            fprintf(stderr, "sh: %s: parameter not set\n", name);
            last_status = 1;
        }
        if (value) cells_puts(out, value, flags);
        return;
    }
    bool colon = false;
    if (*p == ':') { colon = true; p++; }
    char op = *p;
    if (op == '-' || op == '=' || op == '+' || op == '?') {
        p++;
        char *word = expand_scalar(p);
        bool unset = !value || (colon && !*value);
        if (op == '-') cells_puts(out, unset ? word : value, flags);
        else if (op == '+') { if (!unset) cells_puts(out, word, flags); }
        else if (op == '=') {
            if (unset) { var_set(name, word, false); cells_puts(out, word, flags); }
            else cells_puts(out, value, flags);
        } else if (unset) {
            fprintf(stderr, "sh: %s: %s\n", name, *word ? word : "parameter null or not set");
            last_status = 1;
            if (!interactive) _exit(1);
        } else {
            cells_puts(out, value, flags);
        }
        free(word);
        return;
    }
    if (op == '%' || op == '#') {
        bool suffix = op == '%';
        bool longest = p[1] == op;
        p += longest ? 2 : 1;
        char *pattern = expand_scalar(p);
        char *result = strip_affix(value ? value : "", pattern, suffix, longest);
        cells_puts(out, result, flags);
        free(pattern);
        free(result);
        return;
    }
    if (value) cells_puts(out, value, flags);
}

static void expand_word_cells(const char *raw, struct cells *out, bool *had_quotes,
                              bool no_split_context);

static char *expand_scalar(const char *raw) {
    struct cells cs = {0};
    bool quotes = false;
    expand_word_cells(raw, &cs, &quotes, true);
    struct buf result = {0};
    for (size_t i = 0; i < cs.count; i++) {
        if (cs.items[i].flags & CELL_BREAK) { buf_put(&result, ' '); continue; }
        buf_put(&result, cs.items[i].c);
    }
    free(cs.items);
    return buf_take(&result);
}

static void expand_word_cells(const char *raw, struct cells *out, bool *had_quotes,
                              bool no_split) {
    char scratch[64];
    size_t i = 0;
    bool at_start = true;
    while (raw[i]) {
        char c = raw[i];
        if (c == '\\' && raw[i + 1]) {
            cells_put(out, raw[i + 1], CELL_QUOTED);
            *had_quotes = true;
            i += 2;
        } else if (c == '\'') {
            *had_quotes = true;
            i++;
            while (raw[i] && raw[i] != '\'') cells_put(out, raw[i++], CELL_QUOTED);
            if (raw[i]) i++;
        } else if (c == '"') {
            *had_quotes = true;
            i++;
            while (raw[i] && raw[i] != '"') {
                if (raw[i] == '\\' && raw[i + 1] &&
                    strchr("$`\"\\", raw[i + 1])) {
                    cells_put(out, raw[i + 1], CELL_QUOTED);
                    i += 2;
                } else if (raw[i] == '$' && raw[i + 1] == '(' &&
                           raw[i + 2] == '(') {
                    size_t start = i + 3;
                    size_t j = arith_group_end(raw, start);
                    size_t length = j >= start + 2 ? j - start - 2 : 0;
                    char *expr = xmalloc(length + 1);
                    memcpy(expr, raw + start, length);
                    expr[length] = '\0';
                    char *expanded = expand_scalar(expr);
                    char number[32];
                    snprintf(number, sizeof(number), "%ld", arith_eval(expanded));
                    cells_puts(out, number, CELL_QUOTED);
                    free(expr);
                    free(expanded);
                    i = j;
                } else if (raw[i] == '$' && raw[i + 1] == '(') {
                    size_t start = i + 2;
                    int depth = 1;
                    size_t j = start;
                    while (raw[j] && depth > 0) {
                        if (raw[j] == '(') depth++;
                        else if (raw[j] == ')') depth--;
                        j++;
                    }
                    size_t length = j - start - (depth == 0 ? 1 : 0);
                    char *command = xmalloc(length + 1);
                    memcpy(command, raw + start, length);
                    command[length] = '\0';
                    char *output = command_substitute(command);
                    cells_puts(out, output, CELL_QUOTED);
                    free(command);
                    free(output);
                    i = j;
                } else if (raw[i] == '`') {
                    size_t start = ++i;
                    while (raw[i] && raw[i] != '`') i++;
                    char *command = xmalloc(i - start + 1);
                    memcpy(command, raw + start, i - start);
                    command[i - start] = '\0';
                    char *output = command_substitute(command);
                    cells_puts(out, output, CELL_QUOTED);
                    free(command);
                    free(output);
                    if (raw[i]) i++;
                } else if (raw[i] == '$' && raw[i + 1] == '{') {
                    size_t start = i + 2;
                    int depth = 1;
                    size_t j = start;
                    while (raw[j] && depth > 0) {
                        if (raw[j] == '{') depth++;
                        else if (raw[j] == '}') depth--;
                        j++;
                    }
                    size_t length = j - start - (depth == 0 ? 1 : 0);
                    char *body = xmalloc(length + 1);
                    memcpy(body, raw + start, length);
                    body[length] = '\0';
                    expand_braced(body, out, CELL_QUOTED);
                    free(body);
                    i = j;
                } else if (raw[i] == '$' && (raw[i + 1] == '@' || raw[i + 1] == '*')) {
                    const char *join_ifs = var_get("IFS");
                    for (int k = 0; k < positional_count; k++) {
                        if (k) {
                            if (raw[i + 1] == '@') cells_put(out, 0, CELL_BREAK);
                            else if (!join_ifs) cells_put(out, ' ', CELL_QUOTED);
                            else if (join_ifs[0]) cells_put(out, join_ifs[0], CELL_QUOTED);
                        }
                        cells_puts(out, positional[k], CELL_QUOTED);
                    }
                    i += 2;
                } else if (raw[i] == '$' && (isalpha((unsigned char)raw[i + 1]) ||
                                             raw[i + 1] == '_')) {
                    char name[128];
                    size_t n = 0;
                    i++;
                    while ((isalnum((unsigned char)raw[i]) || raw[i] == '_') &&
                           n + 1 < sizeof(name))
                        name[n++] = raw[i++];
                    name[n] = '\0';
                    const char *value = lookup_param(name, scratch, sizeof(scratch));
                    if (!value && opt_nounset) {
                        fprintf(stderr, "sh: %s: parameter not set\n", name);
                        last_status = 1;
                    }
                    if (value) cells_puts(out, value, CELL_QUOTED);
                } else if (raw[i] == '$' && (isdigit((unsigned char)raw[i + 1]) ||
                                             strchr("?$!#-", raw[i + 1]))) {
                    char name[2] = {raw[i + 1], 0};
                    i += 2;
                    const char *value = lookup_param(name, scratch, sizeof(scratch));
                    if (value) cells_puts(out, value, CELL_QUOTED);
                } else {
                    cells_put(out, raw[i++], CELL_QUOTED);
                }
            }
            if (raw[i]) i++;
        } else if (c == '$' && raw[i + 1] == '(' && raw[i + 2] == '(') {
            size_t start = i + 3;
            size_t j = arith_group_end(raw, start);
            size_t length = j >= start + 2 ? j - start - 2 : 0;
            char *expr = xmalloc(length + 1);
            memcpy(expr, raw + start, length);
            expr[length] = '\0';
            char *expanded = expand_scalar(expr);
            char number[32];
            snprintf(number, sizeof(number), "%ld", arith_eval(expanded));
            cells_puts(out, number, no_split ? CELL_QUOTED : CELL_EXPANDED);
            free(expr);
            free(expanded);
            i = j;
        } else if (c == '$' && raw[i + 1] == '(') {
            size_t start = i + 2;
            int depth = 1;
            size_t j = start;
            while (raw[j] && depth > 0) {
                if (raw[j] == '(') depth++;
                else if (raw[j] == ')') depth--;
                j++;
            }
            size_t length = j - start - (depth == 0 ? 1 : 0);
            char *command = xmalloc(length + 1);
            memcpy(command, raw + start, length);
            command[length] = '\0';
            char *output = command_substitute(command);
            cells_puts(out, output, no_split ? CELL_QUOTED : CELL_EXPANDED);
            free(command);
            free(output);
            i = j;
        } else if (c == '`') {
            size_t start = ++i;
            while (raw[i] && raw[i] != '`') i++;
            char *command = xmalloc(i - start + 1);
            memcpy(command, raw + start, i - start);
            command[i - start] = '\0';
            char *output = command_substitute(command);
            cells_puts(out, output, no_split ? CELL_QUOTED : CELL_EXPANDED);
            free(command);
            free(output);
            if (raw[i]) i++;
        } else if (c == '$' && raw[i + 1] == '{') {
            size_t start = i + 2;
            int depth = 1;
            size_t j = start;
            while (raw[j] && depth > 0) {
                if (raw[j] == '{') depth++;
                else if (raw[j] == '}') depth--;
                j++;
            }
            size_t length = j - start - (depth == 0 ? 1 : 0);
            char *body = xmalloc(length + 1);
            memcpy(body, raw + start, length);
            body[length] = '\0';
            expand_braced(body, out, no_split ? CELL_QUOTED : CELL_EXPANDED);
            free(body);
            i = j;
        } else if (c == '$' && (raw[i + 1] == '@' || raw[i + 1] == '*')) {
            for (int k = 0; k < positional_count; k++) {
                if (k) cells_put(out, ' ', no_split ? CELL_QUOTED : CELL_EXPANDED);
                cells_puts(out, positional[k], no_split ? CELL_QUOTED : CELL_EXPANDED);
            }
            i += 2;
        } else if (c == '$' && (isalpha((unsigned char)raw[i + 1]) || raw[i + 1] == '_')) {
            char name[128];
            size_t n = 0;
            i++;
            while ((isalnum((unsigned char)raw[i]) || raw[i] == '_') &&
                   n + 1 < sizeof(name))
                name[n++] = raw[i++];
            name[n] = '\0';
            const char *value = lookup_param(name, scratch, sizeof(scratch));
            if (!value && opt_nounset) {
                fprintf(stderr, "sh: %s: parameter not set\n", name);
                last_status = 1;
            }
            if (value) cells_puts(out, value, no_split ? CELL_QUOTED : CELL_EXPANDED);
        } else if (c == '$' && (isdigit((unsigned char)raw[i + 1]) ||
                                strchr("?$!#-", raw[i + 1]))) {
            char name[2] = {raw[i + 1], 0};
            i += 2;
            const char *value = lookup_param(name, scratch, sizeof(scratch));
            if (value) cells_puts(out, value, no_split ? CELL_QUOTED : CELL_EXPANDED);
        } else if (c == '~' && at_start) {
            size_t j = i + 1;
            while (raw[j] && raw[j] != '/' && raw[j] != ':') j++;
            if (j == i + 1) {
                const char *home = var_get("HOME");
                cells_puts(out, home ? home : "/", CELL_QUOTED);
                i = j;
            } else {
                cells_put(out, raw[i++], 0);
            }
        } else {
            cells_put(out, raw[i++], 0);
        }
        at_start = false;
    }
}

static bool has_glob_chars(const struct cells *cs, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        if (cs->items[i].flags & CELL_QUOTED) continue;
        char c = cs->items[i].c;
        if (c == '*' || c == '?' || c == '[') return true;
    }
    return false;
}

static int compare_strings(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void glob_walk(const char *directory, const char *rest, struct buf *prefix,
                      struct field_list *matches) {
    const char *slash = strchr(rest, '/');
    size_t component_length = slash ? (size_t)(slash - rest) : strlen(rest);
    char component[256];
    if (component_length >= sizeof(component)) return;
    memcpy(component, rest, component_length);
    component[component_length] = '\0';
    const char *next = slash ? slash + 1 : 0;
    bool wild = strpbrk(component, "*?[") != 0;
    size_t prefix_length = prefix->length;
    if (!wild) {
        char plain[256];
        size_t n = 0;
        for (size_t i = 0; component[i] && n + 1 < sizeof(plain); i++) {
            if (component[i] == '\\' && component[i + 1]) i++;
            plain[n++] = component[i];
        }
        plain[n] = '\0';
        buf_puts(prefix, plain);
        if (next) {
            buf_put(prefix, '/');
            char *path = xstrdup(prefix->data);
            struct stat info;
            if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
                glob_walk(path, next, prefix, matches);
            }
            free(path);
        } else {
            struct stat info;
            if (stat(prefix->data, &info) == 0) fields_add(matches, xstrdup(prefix->data));
        }
        prefix->length = prefix_length;
        prefix->data[prefix_length] = '\0';
        return;
    }
    DIR *dir = opendir(directory[0] ? directory : ".");
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!match_pattern(component, entry->d_name, true)) continue;
        buf_puts(prefix, entry->d_name);
        if (next) {
            buf_put(prefix, '/');
            struct stat info;
            char *path = xstrdup(prefix->data);
            if (stat(path, &info) == 0 && S_ISDIR(info.st_mode))
                glob_walk(path, next, prefix, matches);
            free(path);
        } else {
            fields_add(matches, xstrdup(prefix->data));
        }
        prefix->length = prefix_length;
        prefix->data[prefix_length] = '\0';
    }
    closedir(dir);
}

static void emit_field(struct field_list *fields, const struct cells *cs, size_t start,
                       size_t end, bool force) {
    if (start == end && !force) return;
    bool glob = !opt_noglob && has_glob_chars(cs, start, end);
    if (glob) {
        struct buf pattern = {0};
        for (size_t i = start; i < end; i++) {
            if ((cs->items[i].flags & CELL_QUOTED) &&
                strchr("*?[]\\", cs->items[i].c))
                buf_put(&pattern, '\\');
            buf_put(&pattern, cs->items[i].c);
        }
        char *text = buf_take(&pattern);
        struct field_list matches = {0};
        struct buf prefix = {0};
        const char *rest = text;
        if (text[0] == '/') {
            buf_put(&prefix, '/');
            while (*rest == '/') rest++;
        }
        glob_walk(text[0] == '/' ? "/" : "", rest, &prefix, &matches);
        free(prefix.data);
        if (matches.count > 0) {
            qsort(matches.items, (size_t)matches.count, sizeof(char *), compare_strings);
            for (int i = 0; i < matches.count; i++) fields_add(fields, matches.items[i]);
            free(matches.items);
            free(text);
            return;
        }
        free(matches.items);
        free(text);
    }
    struct buf plain = {0};
    for (size_t i = start; i < end; i++) buf_put(&plain, cs->items[i].c);
    fields_add(fields, buf_take(&plain));
}

static void expand_word(const char *raw, struct field_list *fields) {
    struct cells cs = {0};
    bool had_quotes = false;
    expand_word_cells(raw, &cs, &had_quotes, false);
    const char *ifs = var_get("IFS");
    if (!ifs) ifs = " \t\n";
    size_t start = 0;
    bool any = false;
    bool in_field = false;
    size_t field_start = 0;
    for (size_t i = 0; i < cs.count; i++) {
        unsigned char f = cs.items[i].flags;
        if (f & CELL_BREAK) {
            if (in_field || had_quotes) emit_field(fields, &cs, field_start, i, true);
            any = true;
            in_field = false;
            field_start = i + 1;
            continue;
        }
        bool split_here = (f & CELL_EXPANDED) && !(f & CELL_QUOTED) &&
                          strchr(ifs, cs.items[i].c) != 0 && cs.items[i].c != 0;
        if (split_here) {
            if (in_field) emit_field(fields, &cs, field_start, i, false);
            any = any || in_field;
            in_field = false;
            field_start = i + 1;
        } else if (!in_field) {
            in_field = true;
            field_start = i;
        }
    }
    (void)start;
    if (in_field) {
        emit_field(fields, &cs, field_start, cs.count, false);
        any = true;
    } else if (had_quotes && !any && cs.count == 0) {
        fields_add(fields, xstrdup(""));
    } else if (had_quotes && !any) {
        fields_add(fields, xstrdup(""));
    }
    free(cs.items);
}

struct arith {
    const char *p;
};

static long arith_expr(struct arith *a, int level);

static void arith_space(struct arith *a) {
    while (*a->p == ' ' || *a->p == '\t') a->p++;
}

static long arith_primary(struct arith *a) {
    arith_space(a);
    if (*a->p == '(') {
        a->p++;
        long v = arith_expr(a, 0);
        arith_space(a);
        if (*a->p == ')') a->p++;
        return v;
    }
    if (*a->p == '-') { a->p++; return -arith_primary(a); }
    if (*a->p == '+') { a->p++; return arith_primary(a); }
    if (*a->p == '!') { a->p++; return !arith_primary(a); }
    if (*a->p == '~') { a->p++; return ~arith_primary(a); }
    if (isdigit((unsigned char)*a->p)) {
        char *end;
        long v = strtol(a->p, &end, 0);
        a->p = end;
        return v;
    }
    if (isalpha((unsigned char)*a->p) || *a->p == '_') {
        char name[128];
        size_t n = 0;
        while ((isalnum((unsigned char)*a->p) || *a->p == '_') && n + 1 < sizeof(name))
            name[n++] = *a->p++;
        name[n] = '\0';
        const char *v = var_get(name);
        return v ? strtol(v, 0, 0) : 0;
    }
    return 0;
}

static long arith_expr(struct arith *a, int level) {
    static const char *const levels[][6] = {
        {"||", 0}, {"&&", 0}, {"|", 0}, {"^", 0}, {"&", 0},
        {"==", "!=", 0}, {"<=", ">=", "<", ">", 0},
        {"<<", ">>", 0}, {"+", "-", 0}, {"*", "/", "%", 0}};
    enum { LEVEL_COUNT = 10 };
    if (level >= LEVEL_COUNT) return arith_primary(a);
    long left = arith_expr(a, level + 1);
    for (;;) {
        arith_space(a);
        const char *matched = 0;
        for (int i = 0; levels[level][i]; i++) {
            size_t length = strlen(levels[level][i]);
            if (strncmp(a->p, levels[level][i], length) != 0) continue;
            if ((levels[level][i][0] == '|' && length == 1 && a->p[1] == '|') ||
                (levels[level][i][0] == '&' && length == 1 && a->p[1] == '&') ||
                (levels[level][i][0] == '<' && length == 1 && (a->p[1] == '<' || a->p[1] == '=')) ||
                (levels[level][i][0] == '>' && length == 1 && (a->p[1] == '>' || a->p[1] == '=')))
                continue;
            matched = levels[level][i];
            break;
        }
        if (!matched) return left;
        a->p += strlen(matched);
        long right = arith_expr(a, level + 1);
        if (!strcmp(matched, "||")) left = left || right;
        else if (!strcmp(matched, "&&")) left = left && right;
        else if (!strcmp(matched, "|")) left |= right;
        else if (!strcmp(matched, "^")) left ^= right;
        else if (!strcmp(matched, "&")) left &= right;
        else if (!strcmp(matched, "==")) left = left == right;
        else if (!strcmp(matched, "!=")) left = left != right;
        else if (!strcmp(matched, "<=")) left = left <= right;
        else if (!strcmp(matched, ">=")) left = left >= right;
        else if (!strcmp(matched, "<")) left = left < right;
        else if (!strcmp(matched, ">")) left = left > right;
        else if (!strcmp(matched, "<<")) left <<= right;
        else if (!strcmp(matched, ">>")) left >>= right;
        else if (!strcmp(matched, "+")) left += right;
        else if (!strcmp(matched, "-")) left -= right;
        else if (!strcmp(matched, "*")) left *= right;
        else if (!strcmp(matched, "/")) {
            if (!right) { fputs("sh: division by zero\n", stderr); return 0; }
            left /= right;
        } else if (!strcmp(matched, "%")) {
            if (!right) { fputs("sh: division by zero\n", stderr); return 0; }
            left %= right;
        }
    }
}

static long arith_eval(const char *expr) {
    struct arith a = {expr};
    return arith_expr(&a, 0);
}

static int status_from_wait(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return status;
}

static int wait_for(pid_t pid) {
    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, 0);
        if (r == pid) return status_from_wait(status);
        if (r < 0 && errno != EINTR) return 127;
    }
}

static char *command_substitute(const char *command) {
    int fds[2];
    if (pipe(fds) < 0) return xstrdup("");
    fflush(0);
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return xstrdup("");
    }
    if (pid == 0) {
        dup2(fds[1], 1);
        close(fds[0]);
        close(fds[1]);
        int status = run_string(command);
        fflush(0);
        _exit(status);
    }
    close(fds[1]);
    struct buf out = {0};
    char chunk[512];
    for (;;) {
        ssize_t got = read(fds[0], chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        for (ssize_t i = 0; i < got; i++) buf_put(&out, chunk[i]);
    }
    close(fds[0]);
    last_status = wait_for(pid);
    char *text = buf_take(&out);
    size_t length = strlen(text);
    while (length && text[length - 1] == '\n') text[--length] = '\0';
    return text;
}

struct saved_fd {
    int fd;
    int saved;
};

static int redirect_open(struct redirect *r, char *path, int *fd_out) {
    int flags = 0;
    switch (r->kind) {
    case T_LESS: flags = O_RDONLY; break;
    case T_GREAT: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
    case T_DGREAT: flags = O_WRONLY | O_CREAT | O_APPEND; break;
    case T_LESSGREAT: flags = O_RDWR | O_CREAT; break;
    default: return -1;
    }
    int fd = open(path, flags, 0666);
    if (fd < 0) {
        fprintf(stderr, "sh: %s: %s\n", path, strerror(errno));
        return -1;
    }
    *fd_out = fd;
    return 0;
}

static int apply_redirects(struct redirect *list, struct saved_fd *saves, int *save_count) {
    for (struct redirect *r = list; r; r = r->next) {
        if (saves) {
            int saved = fcntl(r->fd, F_DUPFD_CLOEXEC, 10);
            saves[*save_count].fd = r->fd;
            saves[*save_count].saved = saved;
            (*save_count)++;
        }
        if (r->kind == T_DLESS) {
            char *body = r->body ? r->body : xstrdup("");
            char *text = body;
            if (r->body_expand) text = expand_scalar(body);
            int fds[2];
            if (pipe(fds) < 0) return -1;
            size_t length = strlen(text);
            if (length <= 4096) {
                write(fds[1], text, length);
                close(fds[1]);
            } else {
                pid_t writer = fork();
                if (writer == 0) {
                    close(fds[0]);
                    write(fds[1], text, length);
                    _exit(0);
                }
                close(fds[1]);
            }
            dup2(fds[0], r->fd);
            close(fds[0]);
            continue;
        }
        if (r->kind == T_LESSAND || r->kind == T_GREATAND) {
            struct field_list target = {0};
            expand_word(r->target, &target);
            if (target.count != 1) {
                fprintf(stderr, "sh: ambiguous redirect\n");
                return -1;
            }
            if (strcmp(target.items[0], "-") == 0) {
                close(r->fd);
            } else if (isdigit((unsigned char)target.items[0][0])) {
                int source = atoi(target.items[0]);
                if (dup2(source, r->fd) < 0) {
                    fprintf(stderr, "sh: %d: bad file descriptor\n", source);
                    return -1;
                }
            } else {
                int fd;
                struct redirect copy = *r;
                copy.kind = T_GREAT;
                if (redirect_open(&copy, target.items[0], &fd) < 0) return -1;
                dup2(fd, r->fd);
                dup2(fd, 2);
                if (fd != r->fd && fd != 2) close(fd);
            }
            continue;
        }
        struct field_list target = {0};
        expand_word(r->target, &target);
        if (target.count != 1) {
            fprintf(stderr, "sh: ambiguous redirect\n");
            return -1;
        }
        int fd;
        if (redirect_open(r, target.items[0], &fd) < 0) return -1;
        if (fd != r->fd) {
            dup2(fd, r->fd);
            close(fd);
        }
    }
    return 0;
}

static void restore_redirects(struct saved_fd *saves, int count) {
    for (int i = count - 1; i >= 0; i--) {
        if (saves[i].saved >= 0) {
            dup2(saves[i].saved, saves[i].fd);
            close(saves[i].saved);
        } else {
            close(saves[i].fd);
        }
    }
}

static int builtin_echo(int argc, char **argv) {
    bool newline = true;
    bool escapes = true;
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) newline = false;
        else if (strcmp(argv[i], "-e") == 0) escapes = true;
        else if (strcmp(argv[i], "-E") == 0) escapes = false;
        else break;
    }
    struct buf out = {0};
    for (; i < argc; i++) {
        if (out.length) buf_put(&out, ' ');
        for (const char *p = argv[i]; *p; p++) {
            if (escapes && *p == '\\' && p[1]) {
                p++;
                switch (*p) {
                case 'n': buf_put(&out, '\n'); break;
                case 't': buf_put(&out, '\t'); break;
                case 'r': buf_put(&out, '\r'); break;
                case 'a': buf_put(&out, '\a'); break;
                case 'b': buf_put(&out, '\b'); break;
                case 'f': buf_put(&out, '\f'); break;
                case 'v': buf_put(&out, '\v'); break;
                case '\\': buf_put(&out, '\\'); break;
                case 'c': newline = false; goto done;
                case '0': {
                    int value = 0, digits = 0;
                    while (digits < 3 && p[1] >= '0' && p[1] <= '7') {
                        value = value * 8 + (*++p - '0');
                        digits++;
                    }
                    buf_put(&out, (char)value);
                    break;
                }
                default: buf_put(&out, '\\'); buf_put(&out, *p); break;
                }
            } else {
                buf_put(&out, *p);
            }
        }
    }
done:
    if (newline) buf_put(&out, '\n');
    if (out.data) write(1, out.data, out.length);
    free(out.data);
    return 0;
}

static void printf_escape(struct buf *out, const char **pp) {
    const char *p = *pp;
    p++;
    switch (*p) {
    case 'n': buf_put(out, '\n'); break;
    case 't': buf_put(out, '\t'); break;
    case 'r': buf_put(out, '\r'); break;
    case 'a': buf_put(out, '\a'); break;
    case 'b': buf_put(out, '\b'); break;
    case 'f': buf_put(out, '\f'); break;
    case 'v': buf_put(out, '\v'); break;
    case '\\': buf_put(out, '\\'); break;
    case '"': buf_put(out, '"'); break;
    case '\'': buf_put(out, '\''); break;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
        int value = 0, digits = 0;
        while (digits < 3 && *p >= '0' && *p <= '7') { value = value * 8 + (*p++ - '0'); digits++; }
        p--;
        buf_put(out, (char)value);
        break;
    }
    case '\0': buf_put(out, '\\'); p--; break;
    default: buf_put(out, '\\'); buf_put(out, *p); break;
    }
    *pp = p;
}

static int builtin_printf(int argc, char **argv) {
    if (argc < 2) { fputs("printf: usage: printf format [arguments]\n", stderr); return 2; }
    const char *format = argv[1];
    int arg = 2;
    struct buf out = {0};
    int status = 0;
    bool consumed;
    do {
        consumed = false;
        for (const char *p = format; *p; p++) {
            if (*p == '\\') { printf_escape(&out, &p); continue; }
            if (*p != '%') { buf_put(&out, *p); continue; }
            p++;
            if (*p == '%') { buf_put(&out, '%'); continue; }
            char spec[32];
            size_t n = 0;
            spec[n++] = '%';
            while (*p && strchr("-+ #0", *p) && n < 20) spec[n++] = *p++;
            while (*p && (isdigit((unsigned char)*p) || *p == '.') && n < 24) spec[n++] = *p++;
            char conv = *p;
            if (!conv) break;
            const char *value = arg < argc ? argv[arg] : 0;
            if (arg < argc) { arg++; consumed = true; }
            char piece[512];
            if (conv == 'b') {
                const char *text = value ? value : "";
                for (; *text; text++) {
                    if (*text == '\\' && text[1]) printf_escape(&out, &text);
                    else buf_put(&out, *text);
                }
                continue;
            }
            if (conv == 's') {
                spec[n++] = 's';
                spec[n] = '\0';
                snprintf(piece, sizeof(piece), spec, value ? value : "");
            } else if (conv == 'c') {
                spec[n++] = 'c';
                spec[n] = '\0';
                snprintf(piece, sizeof(piece), spec, value ? value[0] : 0);
            } else if (strchr("diouxX", conv)) {
                spec[n++] = 'l';
                spec[n++] = conv == 'i' ? 'd' : conv;
                spec[n] = '\0';
                long number = 0;
                if (value) {
                    if (value[0] == '\'' || value[0] == '"') number = (unsigned char)value[1];
                    else {
                        char *end;
                        number = strtol(value, &end, 0);
                        if (*end) { fprintf(stderr, "printf: %s: not a number\n", value); status = 1; }
                    }
                }
                snprintf(piece, sizeof(piece), spec, number);
            } else {
                fprintf(stderr, "printf: %%%c: unsupported conversion\n", conv);
                status = 1;
                continue;
            }
            buf_puts(&out, piece);
        }
    } while (consumed && arg < argc);
    if (out.data) write(1, out.data, out.length);
    free(out.data);
    return status;
}

struct test_state {
    char **argv;
    int argc;
    int pos;
    bool error;
};

static bool test_or(struct test_state *t);

static bool file_test(char op, const char *path) {
    struct stat info;
    if (op == 'h' || op == 'L') return false;
    if (stat(path, &info) != 0) return false;
    switch (op) {
    case 'e': return true;
    case 'f': return S_ISREG(info.st_mode);
    case 'd': return S_ISDIR(info.st_mode);
    case 's': return info.st_size > 0;
    case 'r': return access(path, R_OK) == 0;
    case 'w': return access(path, W_OK) == 0;
    case 'x': return access(path, X_OK) == 0;
    default: return false;
    }
}

static bool test_primary(struct test_state *t) {
    if (t->pos >= t->argc) { t->error = true; return false; }
    char *a = t->argv[t->pos];
    if (strcmp(a, "(") == 0) {
        t->pos++;
        bool v = test_or(t);
        if (t->pos < t->argc && strcmp(t->argv[t->pos], ")") == 0) t->pos++;
        else t->error = true;
        return v;
    }
    if (strcmp(a, "!") == 0) {
        t->pos++;
        return !test_primary(t);
    }
    if (t->pos + 2 < t->argc + 0 && t->pos + 2 <= t->argc - 1) {
        char *op = t->argv[t->pos + 1];
        if (op[0] == '=' || (op[0] == '!' && op[1] == '=') ||
            (op[0] == '-' && op[1] && (strchr("eglnl", op[1])) && op[2] &&
             (op[2] == 'q' || op[2] == 'e' || op[2] == 't' || op[2] == 'l' || op[2] == 'n'))) {
            char *b = t->argv[t->pos + 2];
            if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) { t->pos += 3; return strcmp(a, b) == 0; }
            if (strcmp(op, "!=") == 0) { t->pos += 3; return strcmp(a, b) != 0; }
            long x = strtol(a, 0, 10), y = strtol(b, 0, 10);
            if (strcmp(op, "-eq") == 0) { t->pos += 3; return x == y; }
            if (strcmp(op, "-ne") == 0) { t->pos += 3; return x != y; }
            if (strcmp(op, "-lt") == 0) { t->pos += 3; return x < y; }
            if (strcmp(op, "-le") == 0) { t->pos += 3; return x <= y; }
            if (strcmp(op, "-gt") == 0) { t->pos += 3; return x > y; }
            if (strcmp(op, "-ge") == 0) { t->pos += 3; return x >= y; }
        }
    }
    if (a[0] == '-' && a[1] && !a[2] && t->pos + 1 < t->argc) {
        char *operand = t->argv[t->pos + 1];
        if (a[1] == 'z') { t->pos += 2; return operand[0] == '\0'; }
        if (a[1] == 'n') { t->pos += 2; return operand[0] != '\0'; }
        if (strchr("efdsrwxhL", a[1])) { t->pos += 2; return file_test(a[1], operand); }
        if (a[1] == 't') { t->pos += 2; return isatty(atoi(operand)); }
    }
    t->pos++;
    return a[0] != '\0';
}

static bool test_and(struct test_state *t) {
    bool v = test_primary(t);
    while (t->pos < t->argc && strcmp(t->argv[t->pos], "-a") == 0) {
        t->pos++;
        bool r = test_primary(t);
        v = v && r;
    }
    return v;
}

static bool test_or(struct test_state *t) {
    bool v = test_and(t);
    while (t->pos < t->argc && strcmp(t->argv[t->pos], "-o") == 0) {
        t->pos++;
        bool r = test_and(t);
        v = v || r;
    }
    return v;
}

static int builtin_test(int argc, char **argv, bool bracket) {
    if (bracket) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            fputs("sh: [: missing ]\n", stderr);
            return 2;
        }
        argc--;
    }
    struct test_state t = {argv + 1, argc - 1, 0, false};
    if (t.argc == 0) return 1;
    bool v = test_or(&t);
    if (t.error || t.pos < t.argc) {
        fputs("sh: test: bad expression\n", stderr);
        return 2;
    }
    return v ? 0 : 1;
}

static int builtin_cd(int argc, char **argv) {
    const char *target = argc > 1 ? argv[1] : var_get("HOME");
    if (!target) target = "/";
    if (strcmp(target, "-") == 0) {
        target = var_get("OLDPWD");
        if (!target) { fputs("sh: cd: OLDPWD not set\n", stderr); return 1; }
    }
    char before[512];
    if (!getcwd(before, sizeof(before))) strcpy(before, "/");
    if (chdir(target) < 0) {
        fprintf(stderr, "sh: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }
    char after[512];
    if (getcwd(after, sizeof(after))) {
        var_set("OLDPWD", before, true);
        var_set("PWD", after, true);
    }
    return 0;
}

static int builtin_export(int argc, char **argv, bool readonly) {
    if (argc == 1) {
        for (size_t i = 0; i < variable_count; i++)
            if (readonly ? variables[i].readonly : variables[i].exported)
                printf("%s %s=\"%s\"\n", readonly ? "readonly" : "export",
                       variables[i].name, variables[i].value);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        char name[128];
        size_t n = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
        if (n >= sizeof(name)) continue;
        memcpy(name, argv[i], n);
        name[n] = '\0';
        if (!valid_name(name)) {
            fprintf(stderr, "sh: %s: bad variable name\n", argv[i]);
            return 1;
        }
        if (eq) var_set(name, eq + 1, !readonly);
        else if (!var_find(name)) var_set(name, "", !readonly);
        struct variable *v = var_find(name);
        if (v) {
            if (readonly) v->readonly = true;
            else v->exported = true;
        }
    }
    return 0;
}

static int builtin_read(int argc, char **argv) {
    int i = 1;
    bool raw = false;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-r") == 0) raw = true;
        i++;
    }
    struct buf line = {0};
    bool got_any = false;
    char c;
    for (;;) {
        ssize_t r = read(0, &c, 1);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        got_any = true;
        if (c == '\n') break;
        if (c == '\\' && !raw) {
            if (read(0, &c, 1) <= 0) break;
            if (c == '\n') continue;
        }
        buf_put(&line, c);
    }
    char *text = buf_take(&line);
    if (i >= argc) {
        var_set("REPLY", text, false);
    } else {
        const char *ifs = var_get("IFS");
        if (!ifs) ifs = " \t\n";
        char *cursor = text;
        for (; i < argc; i++) {
            while (*cursor && strchr(ifs, *cursor) && strchr(" \t\n", *cursor)) cursor++;
            if (i == argc - 1) {
                var_set(argv[i], cursor, false);
                break;
            }
            char *end = cursor;
            while (*end && !strchr(ifs, *end)) end++;
            char saved = *end;
            *end = '\0';
            var_set(argv[i], cursor, false);
            if (saved) end++;
            cursor = end;
        }
    }
    free(text);
    return got_any ? 0 : 1;
}

static bool is_builtin(const char *name) {
    static const char *names[] = {
        "cd", "pwd", "echo", "printf", "exit", "export", "readonly", "unset",
        "set", "shift", "read", "test", "[", "true", "false", ":", ".", "source",
        "eval", "exec", "wait", "return", "break", "continue", "trap", "umask",
        "ulimit", "type", "command", "hash", "local", "kill", "times", "getopts"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcmp(name, names[i]) == 0) return true;
    return false;
}

static int run_file(const char *path);

static int find_in_path(const char *name, char *out, size_t size) {
    if (strchr(name, '/')) {
        snprintf(out, size, "%s", name);
        return access(out, F_OK) == 0 ? 0 : -1;
    }
    const char *path = var_get("PATH");
    if (!path) path = "/usr/bin:/bin:/usr/games:/sbin";
    const char *cursor = path;
    for (;;) {
        const char *end = strchr(cursor, ':');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        snprintf(out, size, "%.*s/%s", (int)(length ? length : 1), length ? cursor : ".", name);
        struct stat info;
        if (stat(out, &info) == 0 && !S_ISDIR(info.st_mode)) return 0;
        if (!end) break;
        cursor = end + 1;
    }
    return -1;
}

static int builtin_dispatch(int argc, char **argv, bool *handled);

static int run_function(struct function *fn, int argc, char **argv) {
    char **saved = positional;
    int saved_count = positional_count;
    char **allocated = xmalloc((size_t)(argc > 1 ? argc : 1) * sizeof(char *));
    positional = allocated;
    for (int i = 1; i < argc; i++) positional[i - 1] = argv[i];
    positional_count = argc - 1;
    function_depth++;
    int status = run_node(fn->body);
    if (returning) { status = return_status; returning = false; }
    function_depth--;
    free(allocated);
    positional = saved;
    positional_count = saved_count;
    return status;
}

static int builtin_dispatch(int argc, char **argv, bool *handled) {
    const char *name = argv[0];
    *handled = true;
    if (strcmp(name, "echo") == 0) return builtin_echo(argc, argv);
    if (strcmp(name, "printf") == 0) return builtin_printf(argc, argv);
    if (strcmp(name, "true") == 0 || strcmp(name, ":") == 0) return 0;
    if (strcmp(name, "false") == 0) return 1;
    if (strcmp(name, "test") == 0) return builtin_test(argc, argv, false);
    if (strcmp(name, "[") == 0) return builtin_test(argc, argv, true);
    if (strcmp(name, "cd") == 0) return builtin_cd(argc, argv);
    if (strcmp(name, "pwd") == 0) {
        char cwd[512];
        if (!getcwd(cwd, sizeof(cwd))) { perror("pwd"); return 1; }
        printf("%s\n", cwd);
        return 0;
    }
    if (strcmp(name, "exit") == 0) {
        int code = argc > 1 ? atoi(argv[1]) : last_status;
        if (exit_trap) { char *t = exit_trap; exit_trap = 0; run_string(t); }
        fflush(0);
        _exit(code & 0xFF);
    }
    if (strcmp(name, "export") == 0) return builtin_export(argc, argv, false);
    if (strcmp(name, "readonly") == 0) return builtin_export(argc, argv, true);
    if (strcmp(name, "local") == 0) {
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                var_set(argv[i], eq + 1, false);
            }
        }
        return 0;
    }
    if (strcmp(name, "unset") == 0) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-f") == 0) continue;
            var_unset(argv[i]);
        }
        return 0;
    }
    if (strcmp(name, "shift") == 0) {
        int count = argc > 1 ? atoi(argv[1]) : 1;
        if (count > positional_count || count < 0) return 1;
        positional += count;
        positional_count -= count;
        return 0;
    }
    if (strcmp(name, "set") == 0) {
        int i = 1;
        for (; i < argc; i++) {
            const char *a = argv[i];
            if (strcmp(a, "--") == 0) { i++; break; }
            if ((a[0] == '-' || a[0] == '+') && a[1]) {
                bool on = a[0] == '-';
                for (const char *f = a + 1; *f; f++) {
                    if (*f == 'e') opt_errexit = on;
                    else if (*f == 'x') opt_xtrace = on;
                    else if (*f == 'f') opt_noglob = on;
                    else if (*f == 'u') opt_nounset = on;
                }
                continue;
            }
            break;
        }
        if (i < argc || (argc > 1 && strcmp(argv[argc - 1], "--") == 0)) {
            int count = argc - i;
            char **fresh = xmalloc((size_t)(count > 0 ? count : 1) * sizeof(char *));
            for (int k = 0; k < count; k++) fresh[k] = xstrdup(argv[i + k]);
            positional = fresh;
            positional_count = count;
        }
        return 0;
    }
    if (strcmp(name, "read") == 0) return builtin_read(argc, argv);
    if (strcmp(name, "eval") == 0) {
        struct buf text = {0};
        for (int i = 1; i < argc; i++) {
            if (i > 1) buf_put(&text, ' ');
            buf_puts(&text, argv[i]);
        }
        char *s = buf_take(&text);
        int status = run_string(s);
        free(s);
        return status;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "source") == 0) {
        if (argc < 2) { fputs("sh: .: filename argument required\n", stderr); return 2; }
        char found[512];
        if (find_in_path(argv[1], found, sizeof(found)) < 0) {
            fprintf(stderr, "sh: .: %s: not found\n", argv[1]);
            return 1;
        }
        return run_file(found);
    }
    if (strcmp(name, "return") == 0) {
        returning = true;
        return_status = argc > 1 ? atoi(argv[1]) : last_status;
        return return_status;
    }
    if (strcmp(name, "break") == 0) {
        break_count = argc > 1 ? atoi(argv[1]) : 1;
        if (break_count < 1) break_count = 1;
        return 0;
    }
    if (strcmp(name, "continue") == 0) {
        continue_count = argc > 1 ? atoi(argv[1]) : 1;
        if (continue_count < 1) continue_count = 1;
        return 0;
    }
    if (strcmp(name, "wait") == 0) {
        if (argc > 1) return wait_for((pid_t)atoi(argv[1]));
        int status;
        while (waitpid(-1, &status, 0) > 0) { }
        return 0;
    }
    if (strcmp(name, "trap") == 0) {
        if (argc >= 3 && (strcmp(argv[2], "EXIT") == 0 || strcmp(argv[2], "0") == 0)) {
            free(exit_trap);
            exit_trap = argv[1][0] ? xstrdup(argv[1]) : 0;
        }
        return 0;
    }
    if (strcmp(name, "umask") == 0) {
        if (argc == 1) puts("0022");
        return 0;
    }
    if (strcmp(name, "ulimit") == 0) {
        if (argc == 1 || strcmp(argv[1], "-n") == 0) puts("unlimited");
        return 0;
    }
    if (strcmp(name, "hash") == 0 || strcmp(name, "times") == 0) return 0;
    if (strcmp(name, "getopts") == 0) return 1;
    if (strcmp(name, "kill") == 0) {
        int sig = SIGTERM;
        int i = 1;
        if (i < argc && argv[i][0] == '-') {
            sig = atoi(argv[i] + 1);
            if (!sig) {
                static const struct { const char *n; int s; } names[] = {
                    {"HUP", 1}, {"INT", 2}, {"QUIT", 3}, {"KILL", 9}, {"USR1", 10},
                    {"USR2", 12}, {"PIPE", 13}, {"ALRM", 14}, {"TERM", 15}, {"CHLD", 17}};
                const char *n = argv[i] + 1;
                if (strncmp(n, "SIG", 3) == 0) n += 3;
                for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++)
                    if (strcmp(n, names[k].n) == 0) sig = names[k].s;
            }
            i++;
        }
        int status = 0;
        for (; i < argc; i++)
            if (kill(atoi(argv[i]), sig) < 0) {
                fprintf(stderr, "sh: kill: %s: %s\n", argv[i], strerror(errno));
                status = 1;
            }
        return status;
    }
    if (strcmp(name, "type") == 0 || strcmp(name, "command") == 0) {
        int i = 1;
        bool verbose = strcmp(name, "type") == 0;
        if (i < argc && (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-V") == 0)) { verbose = true; i++; }
        else if (strcmp(name, "command") == 0 && i < argc) {
            *handled = false;
            return 0;
        }
        int status = 0;
        for (; i < argc; i++) {
            char found[512];
            if (func_find(argv[i])) printf("%s is a function\n", argv[i]);
            else if (is_builtin(argv[i])) printf("%s is a shell builtin\n", argv[i]);
            else if (find_in_path(argv[i], found, sizeof(found)) == 0) {
                if (verbose) printf("%s is %s\n", argv[i], found);
                else puts(found);
            } else {
                if (verbose) fprintf(stderr, "sh: %s: not found\n", argv[i]);
                status = 1;
            }
        }
        return status;
    }
    if (strcmp(name, "exec") == 0) {
        *handled = false;
        return 0;
    }
    *handled = false;
    return 0;
}

static void trace_command(char **argv, int argc) {
    if (!opt_xtrace) return;
    fputs("+", stderr);
    for (int i = 0; i < argc; i++) fprintf(stderr, " %s", argv[i]);
    fputc('\n', stderr);
}

static int apply_assignments(struct node *n, int *first_word_index, bool temporary,
                             char ***saved_names, char ***saved_values, int *saved_count) {
    int i = 0;
    for (; i < n->word_count; i++) {
        const char *w = n->words[i];
        const char *eq = strchr(w, '=');
        if (!eq || eq == w) break;
        bool ok = true;
        for (const char *c = w; c < eq; c++)
            if (!(isalnum((unsigned char)*c) || *c == '_')) { ok = false; break; }
        if (!ok || isdigit((unsigned char)w[0])) break;
        char name[128];
        size_t length = (size_t)(eq - w);
        if (length >= sizeof(name)) break;
        memcpy(name, w, length);
        name[length] = '\0';
        char *value = expand_scalar(eq + 1);
        if (temporary) {
            struct variable *old = var_find(name);
            *saved_names = xrealloc(*saved_names, (size_t)(*saved_count + 1) * sizeof(char *));
            *saved_values = xrealloc(*saved_values, (size_t)(*saved_count + 1) * sizeof(char *));
            (*saved_names)[*saved_count] = xstrdup(name);
            (*saved_values)[*saved_count] = old ? xstrdup(old->value) : 0;
            (*saved_count)++;
            var_set(name, value, true);
        } else {
            var_set(name, value, false);
        }
        free(value);
    }
    *first_word_index = i;
    return 0;
}

static void restore_assignments(char **names, char **values, int count) {
    for (int i = count - 1; i >= 0; i--) {
        if (values[i]) var_set(names[i], values[i], false);
        else var_unset(names[i]);
        free(names[i]);
        free(values[i]);
    }
    free(names);
    free(values);
}

static int exec_external(char **argv) {
    char **env = build_environment();
    char path[512];
    if (find_in_path(argv[0], path, sizeof(path)) < 0) {
        fprintf(stderr, "sh: %s: not found\n", argv[0]);
        free_environment(env);
        return 127;
    }
    execve(path, argv, env);
    int code = errno == ENOEXEC ? 126 : 126;
    if (errno == ENOEXEC) {
        char *script_argv[66];
        int n = 0;
        script_argv[n++] = (char *)"sh";
        script_argv[n++] = path;
        for (int i = 1; argv[i] && n < 64; i++) script_argv[n++] = argv[i];
        script_argv[n] = 0;
        execve("/usr/bin/sh", script_argv, env);
        execve("/bin/sh", script_argv, env);
    }
    fprintf(stderr, "sh: %s: %s\n", argv[0], strerror(errno));
    free_environment(env);
    return code;
}

static int run_simple(struct node *n, bool in_forked_child) {
    int first = 0;
    char **saved_names = 0, **saved_values = 0;
    int saved_count = 0;
    bool has_command = false;
    for (int i = 0; i < n->word_count; i++) {
        const char *w = n->words[i];
        const char *eq = strchr(w, '=');
        bool assign = eq && eq != w && !isdigit((unsigned char)w[0]);
        if (assign)
            for (const char *c = w; c < eq; c++)
                if (!(isalnum((unsigned char)*c) || *c == '_')) { assign = false; break; }
        if (!assign) { has_command = true; break; }
    }
    apply_assignments(n, &first, has_command, &saved_names, &saved_values, &saved_count);
    if (!has_command) {
        struct saved_fd saves[16];
        int save_count = 0;
        if (n->redirects) {
            if (apply_redirects(n->redirects, saves, &save_count) < 0) {
                restore_redirects(saves, save_count);
                return 1;
            }
            restore_redirects(saves, save_count);
        }
        return 0;
    }
    struct field_list fields = {0};
    for (int i = first; i < n->word_count; i++) expand_word(n->words[i], &fields);
    if (fields.count == 0) {
        restore_assignments(saved_names, saved_values, saved_count);
        return 0;
    }
    fields_add(&fields, 0);
    fields.count--;
    char **argv = fields.items;
    int argc = fields.count;
    trace_command(argv, argc);

    bool is_exec = strcmp(argv[0], "exec") == 0;
    if (is_exec) {
        if (argc == 1) {
            if (n->redirects) apply_redirects(n->redirects, 0, 0);
            restore_assignments(saved_names, saved_values, saved_count);
            return 0;
        }
        if (n->redirects && apply_redirects(n->redirects, 0, 0) < 0) _exit(1);
        fflush(0);
        int code = exec_external(argv + 1);
        _exit(code);
    }
    struct function *fn = func_find(argv[0]);
    bool builtin = !fn && is_builtin(argv[0]);
    if (!fn && !builtin) {
        fflush(0);
        pid_t pid = 0;
        if (!in_forked_child) {
            pid = fork();
            if (pid < 0) {
                fprintf(stderr, "sh: fork failed\n");
                restore_assignments(saved_names, saved_values, saved_count);
                return 1;
            }
        }
        if (pid == 0) {
            if (n->redirects && apply_redirects(n->redirects, 0, 0) < 0) _exit(1);
            int code = exec_external(argv);
            fflush(0);
            _exit(code);
        }
        int status = wait_for(pid);
        restore_assignments(saved_names, saved_values, saved_count);
        return status;
    }
    struct saved_fd saves[16];
    int save_count = 0;
    int status;
    if (n->redirects && apply_redirects(n->redirects, saves, &save_count) < 0) {
        restore_redirects(saves, save_count);
        restore_assignments(saved_names, saved_values, saved_count);
        return 1;
    }
    if (fn) {
        status = run_function(fn, argc, argv);
    } else {
        bool handled;
        if (strcmp(argv[0], "command") == 0 && argc > 1 &&
            strcmp(argv[1], "-v") != 0 && strcmp(argv[1], "-V") != 0) {
            struct node sub = *n;
            (void)sub;
            char **shifted = argv + 1;
            bool inner_handled;
            if (is_builtin(shifted[0])) status = builtin_dispatch(argc - 1, shifted, &inner_handled);
            else {
                fflush(0);
                pid_t pid = fork();
                if (pid == 0) _exit(exec_external(shifted));
                status = wait_for(pid);
            }
        } else {
            status = builtin_dispatch(argc, argv, &handled);
        }
    }
    fflush(0);
    restore_redirects(saves, save_count);
    restore_assignments(saved_names, saved_values, saved_count);
    for (int i = 0; i < fields.count; i++) free(fields.items[i]);
    free(fields.items);
    return status;
}

static int run_pipeline(struct node *n) {
    int count = n->stage_count;
    pid_t *pids = xmalloc((size_t)count * sizeof(pid_t));
    int previous = -1;
    fflush(0);
    for (int i = 0; i < count; i++) {
        int fds[2] = {-1, -1};
        if (i < count - 1 && pipe(fds) < 0) {
            fprintf(stderr, "sh: pipe failed\n");
            return 1;
        }
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "sh: fork failed\n");
            return 1;
        }
        if (pid == 0) {
            if (previous >= 0) { dup2(previous, 0); close(previous); }
            if (fds[1] >= 0) { dup2(fds[1], 1); close(fds[1]); }
            if (fds[0] >= 0) close(fds[0]);
            int status;
            struct node *stage = n->stages[i];
            if (stage->type == N_SIMPLE) status = run_simple(stage, true);
            else status = run_node(stage);
            fflush(0);
            _exit(status);
        }
        pids[i] = pid;
        if (previous >= 0) close(previous);
        if (fds[1] >= 0) close(fds[1]);
        previous = fds[0];
    }
    if (previous >= 0) close(previous);
    int status = 0;
    for (int i = 0; i < count; i++) {
        int s = wait_for(pids[i]);
        if (i == count - 1) status = s;
    }
    free(pids);
    return status;
}

static bool case_match(const char *pattern_raw, const char *subject) {
    char *pattern = expand_scalar(pattern_raw);
    struct buf escaped = {0};
    const char *raw = pattern_raw;
    bool quoted = strchr(raw, '\'') || strchr(raw, '"') || strchr(raw, '\\');
    bool result;
    if (quoted) {
        result = strcmp(pattern, subject) == 0;
    } else {
        result = match_pattern(pattern, subject, false);
    }
    free(escaped.data);
    free(pattern);
    return result;
}

static int run_node(struct node *n) {
    if (!n) return 0;
    int status = 0;
    switch (n->type) {
    case N_SIMPLE:
        status = run_simple(n, false);
        break;
    case N_PIPELINE:
        status = run_pipeline(n);
        break;
    case N_NOT:
        status = run_node(n->left) == 0 ? 1 : 0;
        break;
    case N_AND:
        status = run_node(n->left);
        if (status == 0 && !returning && !break_count && !continue_count)
            status = run_node(n->right);
        break;
    case N_OR:
        status = run_node(n->left);
        if (status != 0 && !returning && !break_count && !continue_count)
            status = run_node(n->right);
        break;
    case N_LIST:
        status = run_node(n->left);
        last_status = status;
        if (!returning && !break_count && !continue_count) status = run_node(n->right);
        break;
    case N_BACKGROUND: {
        fflush(0);
        pid_t pid = fork();
        if (pid == 0) {
            int s = run_node(n->left);
            fflush(0);
            _exit(s);
        }
        last_background = pid;
        status = 0;
        break;
    }
    case N_SUBSHELL: {
        fflush(0);
        pid_t pid = fork();
        if (pid == 0) {
            if (n->redirects && apply_redirects(n->redirects, 0, 0) < 0) _exit(1);
            int s = run_node(n->left);
            fflush(0);
            _exit(s);
        }
        status = wait_for(pid);
        break;
    }
    case N_GROUP: {
        struct saved_fd saves[16];
        int save_count = 0;
        if (n->redirects && apply_redirects(n->redirects, saves, &save_count) < 0) {
            restore_redirects(saves, save_count);
            return 1;
        }
        status = run_node(n->left);
        fflush(0);
        restore_redirects(saves, save_count);
        break;
    }
    case N_IF: {
        struct saved_fd saves[16];
        int save_count = 0;
        if (n->redirects && apply_redirects(n->redirects, saves, &save_count) < 0) {
            restore_redirects(saves, save_count);
            return 1;
        }
        bool saved_errexit = opt_errexit;
        opt_errexit = false;
        int condition = run_node(n->left);
        opt_errexit = saved_errexit;
        if (condition == 0) status = run_node(n->right);
        else if (n->third) status = run_node(n->third);
        else status = 0;
        fflush(0);
        restore_redirects(saves, save_count);
        break;
    }
    case N_WHILE:
    case N_UNTIL: {
        struct saved_fd saves[16];
        int save_count = 0;
        if (n->redirects && apply_redirects(n->redirects, saves, &save_count) < 0) {
            restore_redirects(saves, save_count);
            return 1;
        }
        loop_depth++;
        status = 0;
        for (;;) {
            bool saved_errexit = opt_errexit;
            opt_errexit = false;
            int condition = run_node(n->left);
            opt_errexit = saved_errexit;
            if ((n->type == N_WHILE) != (condition == 0)) break;
            status = run_node(n->right);
            if (returning) break;
            if (break_count) { break_count--; break; }
            if (continue_count) {
                continue_count--;
                if (continue_count) break;
            }
        }
        loop_depth--;
        fflush(0);
        restore_redirects(saves, save_count);
        break;
    }
    case N_FOR: {
        struct saved_fd saves[16];
        int save_count = 0;
        if (n->redirects && apply_redirects(n->redirects, saves, &save_count) < 0) {
            restore_redirects(saves, save_count);
            return 1;
        }
        struct field_list items = {0};
        if (n->word_count < 0) {
            for (int i = 0; i < positional_count; i++) fields_add(&items, xstrdup(positional[i]));
        } else {
            for (int i = 0; i < n->word_count; i++) expand_word(n->words[i], &items);
        }
        loop_depth++;
        status = 0;
        for (int i = 0; i < items.count; i++) {
            var_set(n->name, items.items[i], false);
            status = run_node(n->left);
            if (returning) break;
            if (break_count) { break_count--; break; }
            if (continue_count) {
                continue_count--;
                if (continue_count) break;
            }
        }
        loop_depth--;
        fflush(0);
        restore_redirects(saves, save_count);
        break;
    }
    case N_CASE: {
        char *subject = expand_scalar(n->name);
        status = 0;
        for (struct case_item *item = n->items; item; item = item->next) {
            bool matched = false;
            for (int i = 0; i < item->pattern_count && !matched; i++)
                matched = case_match(item->patterns[i], subject);
            if (matched) {
                status = run_node(item->body);
                break;
            }
        }
        free(subject);
        break;
    }
    case N_FUNCTION: {
        struct function *existing = func_find(n->name);
        if (existing) existing->body = n->left;
        else {
            functions = xrealloc(functions, (function_count + 1) * sizeof(*functions));
            functions[function_count].name = xstrdup(n->name);
            functions[function_count].body = n->left;
            function_count++;
        }
        status = 0;
        break;
    }
    }
    last_status = status;
    if (opt_errexit && status != 0 && n->type == N_SIMPLE && !exiting) {
        fflush(0);
        _exit(status);
    }
    return status;
}

static int run_string(const char *text) {
    struct parser parser;
    memset(&parser, 0, sizeof(parser));
    parser.lexer.input = text;
    int status = 0;
    for (;;) {
        skip_newlines(&parser);
        if (peek(&parser.lexer).type == T_EOF) break;
        struct node *tree = parse_and_or(&parser);
        enum token_type sep = peek(&parser.lexer).type;
        if (sep == T_AMP) {
            take(&parser.lexer);
            struct node *bg = new_node(N_BACKGROUND);
            bg->left = tree;
            tree = bg;
        } else if (sep == T_SEMI || sep == T_NEWLINE) {
            take(&parser.lexer);
        } else if (sep != T_EOF && !parser.error) {
            parse_error(&parser, "syntax error near unexpected token");
        }
        if (parser.error || parser.lexer.error) {
            fprintf(stderr, "sh: %s\n", parser.error ? parser.error : parser.lexer.error);
            return 2;
        }
        status = run_node(tree);
        if (returning || break_count || continue_count) break;
    }
    return status;
}

static int run_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "sh: %s: %s\n", path, strerror(errno));
        return 127;
    }
    struct buf contents = {0};
    char chunk[1024];
    for (;;) {
        ssize_t got = read(fd, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        for (ssize_t i = 0; i < got; i++) buf_put(&contents, chunk[i]);
    }
    close(fd);
    char *text = buf_take(&contents);
    int status = run_string(text);
    free(text);
    return status;
}

static void interactive_loop(void) {
    char line[1024];
    for (;;) {
        const char *ps1 = var_get("PS1");
        fputs(ps1 ? ps1 : "$ ", stderr);
        fflush(stderr);
        if (!fgets(line, sizeof(line), stdin)) break;
        last_status = run_string(line);
    }
}

int main(int argc, char **argv) {
    shell_pid = getpid();
    import_environment();
    if (!var_get("PATH")) var_set("PATH", "/usr/bin:/bin:/usr/games:/sbin", true);
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd))) var_set("PWD", cwd, true);
    shell_name = argc > 0 ? argv[0] : (char *)"sh";
    int i = 1;
    const char *command = 0;
    bool read_stdin = false;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || !a[1]) break;
        if (strcmp(a, "--") == 0) { i++; break; }
        for (const char *f = a + 1; *f; f++) {
            if (*f == 'c') {
                if (i + 1 >= argc) { fputs("sh: -c requires an argument\n", stderr); return 2; }
                command = argv[++i];
            } else if (*f == 'e') opt_errexit = true;
            else if (*f == 'x') opt_xtrace = true;
            else if (*f == 'f') opt_noglob = true;
            else if (*f == 'u') opt_nounset = true;
            else if (*f == 's') read_stdin = true;
            else if (*f == 'i') interactive = true;
        }
    }
    if (command) {
        if (i < argc) { shell_name = argv[i++]; }
        positional = argv + i;
        positional_count = argc - i;
        int status = run_string(command);
        if (exit_trap) run_string(exit_trap);
        fflush(0);
        return status;
    }
    if (i < argc && !read_stdin) {
        shell_name = argv[i];
        positional = argv + i + 1;
        positional_count = argc - i - 1;
        int status = run_file(argv[i]);
        if (exit_trap) run_string(exit_trap);
        fflush(0);
        return status;
    }
    positional = argv + i;
    positional_count = argc - i;
    if (isatty(0) || interactive) {
        interactive = true;
        interactive_loop();
        fflush(0);
        return last_status;
    }
    struct buf contents = {0};
    char chunk[1024];
    for (;;) {
        ssize_t got = read(0, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        for (ssize_t k = 0; k < got; k++) buf_put(&contents, chunk[k]);
    }
    char *text = buf_take(&contents);
    int status = run_string(text);
    if (exit_trap) run_string(exit_trap);
    fflush(0);
    return status;
}
