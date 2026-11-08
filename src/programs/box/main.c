#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COPY_BUFFER 16384
#define PATH_LIMIT 1024

static const char *applet_name;

static int fail(const char *what, const char *path)
{
    fprintf(stderr, "%s: %s%s%s: %s\n", applet_name, what, path ? " " : "",
            path ? path : "", strerror(errno));
    return 1;
}

static const char *base_name(const char *path)
{
    const char *base = path;
    for (const char *cursor = path; *cursor; cursor++)
        if (*cursor == '/' && cursor[1])
            base = cursor + 1;
    return base;
}

static bool is_directory(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

static void join_path(char *out, size_t capacity, const char *dir,
                      const char *name)
{
    size_t length = strlen(dir);
    while (length > 1 && dir[length - 1] == '/')
        length--;
    if (length == 1 && dir[0] == '/')
        snprintf(out, capacity, "/%s", name);
    else
        snprintf(out, capacity, "%.*s/%s", (int)length, dir, name);
}

static bool parse_clause(const char *text, size_t length, unsigned int *mode)
{
    char clause[64];
    if (!length || length >= sizeof(clause))
        return false;
    memcpy(clause, text, length);
    clause[length] = '\0';
    unsigned int who = 0;
    const char *cursor = clause;
    while (*cursor == 'u' || *cursor == 'g' || *cursor == 'o' || *cursor == 'a')
    {
        if (*cursor == 'u')
            who |= 0700;
        else if (*cursor == 'g')
            who |= 0070;
        else if (*cursor == 'o')
            who |= 0007;
        else
            who |= 0777;
        cursor++;
    }
    if (!who)
        who = 0777;
    if (*cursor != '+' && *cursor != '-' && *cursor != '=')
        return false;
    char operation = *cursor++;
    unsigned int bits = 0;
    for (; *cursor; cursor++)
    {
        if (*cursor == 'r')
            bits |= 0444;
        else if (*cursor == 'w')
            bits |= 0222;
        else if (*cursor == 'x' || *cursor == 'X')
            bits |= 0111;
        else
            return false;
    }
    bits &= who;
    if (operation == '+')
        *mode |= bits;
    else if (operation == '-')
        *mode &= ~bits;
    else
        *mode = (*mode & ~who) | bits;
    return true;
}

static bool parse_mode(const char *text, unsigned int *mode)
{
    if (!*text)
        return false;
    char *end;
    unsigned long value = strtoul(text, &end, 8);
    if (!*end && text[0] >= '0' && text[0] <= '7')
    {
        *mode = (unsigned int)(value & 07777);
        return true;
    }
    const char *cursor = text;
    for (;;)
    {
        const char *comma = strchr(cursor, ',');
        size_t length = comma ? (size_t)(comma - cursor) : strlen(cursor);
        if (!parse_clause(cursor, length, mode))
            return false;
        if (!comma)
            return true;
        cursor = comma + 1;
    }
}

static int copy_file(const char *from, const char *to, bool preserve)
{
    int input = open(from, O_RDONLY);
    if (input < 0)
        return fail("cannot open", from);
    struct stat info;
    memset(&info, 0, sizeof(info));
    fstat(input, &info);
    int output = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0)
    {
        int saved = errno;
        close(input);
        errno = saved;
        return fail("cannot create", to);
    }
    char *buffer = malloc(COPY_BUFFER);
    if (!buffer)
    {
        close(input);
        close(output);
        errno = ENOMEM;
        return fail("out of memory", 0);
    }
    int status = 0;
    for (;;)
    {
        ssize_t count = read(input, buffer, COPY_BUFFER);
        if (count < 0)
        {
            status = fail("read error", from);
            break;
        }
        if (count == 0)
            break;
        ssize_t written = 0;
        while (written < count)
        {
            ssize_t result = write(output, buffer + written, (size_t)(count - written));
            if (result <= 0)
            {
                status = fail("write error", to);
                break;
            }
            written += result;
        }
        if (status)
            break;
    }
    free(buffer);
    close(input);
    close(output);
    if (!status && preserve && info.st_mode)
        chmod(to, info.st_mode & 07777);
    return status;
}

static int copy_tree(const char *from, const char *to, bool recursive,
                     bool preserve)
{
    struct stat info;
    if (stat(from, &info) != 0)
        return fail("cannot stat", from);
    if (!S_ISDIR(info.st_mode))
        return copy_file(from, to, preserve);
    if (!recursive)
    {
        fprintf(stderr, "%s: -r not specified; omitting directory '%s'\n",
                applet_name, from);
        return 1;
    }
    if (!is_directory(to) && mkdir(to, 0755) != 0)
        return fail("cannot create directory", to);
    DIR *directory = opendir(from);
    if (!directory)
        return fail("cannot open directory", from);
    int status = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != 0)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char source[PATH_LIMIT];
        char target[PATH_LIMIT];
        join_path(source, sizeof(source), from, entry->d_name);
        join_path(target, sizeof(target), to, entry->d_name);
        if (copy_tree(source, target, recursive, preserve))
            status = 1;
    }
    closedir(directory);
    return status;
}

static int applet_cp(int argc, char **argv)
{
    bool recursive = false, preserve = false;
    int index = 1;
    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++)
    {
        if (strcmp(argv[index], "--") == 0)
        {
            index++;
            break;
        }
        for (const char *flag = argv[index] + 1; *flag; flag++)
        {
            if (*flag == 'r' || *flag == 'R')
                recursive = true;
            else if (*flag == 'p')
                preserve = true;
            else if (*flag != 'f' && *flag != 'i' && *flag != 'a' && *flag != 'v')
            {
                fprintf(stderr, "cp: invalid option -- '%c'\n", *flag);
                return 1;
            }
            if (*flag == 'a')
                recursive = preserve = true;
        }
    }
    int count = argc - index;
    if (count < 2)
    {
        fprintf(stderr, "usage: cp [-rpf] source... target\n");
        return 1;
    }
    const char *target = argv[argc - 1];
    bool into_directory = is_directory(target);
    if (count > 2 && !into_directory)
    {
        fprintf(stderr, "cp: target '%s' is not a directory\n", target);
        return 1;
    }
    int status = 0;
    for (int i = index; i < argc - 1; i++)
    {
        char destination[PATH_LIMIT];
        if (into_directory)
            join_path(destination, sizeof(destination), target, base_name(argv[i]));
        else
            snprintf(destination, sizeof(destination), "%s", target);
        if (copy_tree(argv[i], destination, recursive, preserve))
            status = 1;
    }
    return status;
}

static int remove_tree(const char *path)
{
    struct stat info;
    if (stat(path, &info) != 0)
        return fail("cannot stat", path);
    if (!S_ISDIR(info.st_mode))
    {
        if (unlink(path) != 0)
            return fail("cannot remove", path);
        return 0;
    }
    DIR *directory = opendir(path);
    if (!directory)
        return fail("cannot open directory", path);
    int status = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != 0)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[PATH_LIMIT];
        join_path(child, sizeof(child), path, entry->d_name);
        if (remove_tree(child))
            status = 1;
    }
    closedir(directory);
    if (rmdir(path) != 0)
        status = fail("cannot remove directory", path);
    return status;
}

static int applet_mv(int argc, char **argv)
{
    int index = 1;
    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++)
    {
        if (strcmp(argv[index], "--") == 0)
        {
            index++;
            break;
        }
        for (const char *flag = argv[index] + 1; *flag; flag++)
            if (*flag != 'f' && *flag != 'i' && *flag != 'v' && *flag != 'n')
            {
                fprintf(stderr, "mv: invalid option -- '%c'\n", *flag);
                return 1;
            }
    }
    int count = argc - index;
    if (count < 2)
    {
        fprintf(stderr, "usage: mv source... target\n");
        return 1;
    }
    const char *target = argv[argc - 1];
    bool into_directory = is_directory(target);
    if (count > 2 && !into_directory)
    {
        fprintf(stderr, "mv: target '%s' is not a directory\n", target);
        return 1;
    }
    int status = 0;
    for (int i = index; i < argc - 1; i++)
    {
        char destination[PATH_LIMIT];
        if (into_directory)
            join_path(destination, sizeof(destination), target, base_name(argv[i]));
        else
            snprintf(destination, sizeof(destination), "%s", target);
        if (!exists(argv[i]))
        {
            status = fail("cannot stat", argv[i]);
            continue;
        }
        if (rename(argv[i], destination) == 0)
            continue;
        if (copy_tree(argv[i], destination, true, true) != 0 ||
            remove_tree(argv[i]) != 0)
            status = 1;
    }
    return status;
}

static int make_directories(const char *path, unsigned int mode, bool parents)
{
    char buffer[PATH_LIMIT];
    if (strlen(path) >= sizeof(buffer))
    {
        errno = ENAMETOOLONG;
        return fail("cannot create directory", path);
    }
    strcpy(buffer, path);
    if (parents)
    {
        for (char *cursor = buffer + 1; *cursor; cursor++)
        {
            if (*cursor != '/')
                continue;
            *cursor = '\0';
            if (!is_directory(buffer) && mkdir(buffer, 0755) != 0 && !is_directory(buffer))
                return fail("cannot create directory", buffer);
            *cursor = '/';
        }
        if (is_directory(buffer))
            return 0;
    }
    if (mkdir(buffer, mode) != 0)
        return fail("cannot create directory", path);
    return 0;
}

static int applet_mkdir(int argc, char **argv)
{
    bool parents = false;
    unsigned int mode = 0755;
    int index = 1;
    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++)
    {
        if (strcmp(argv[index], "--") == 0)
        {
            index++;
            break;
        }
        if (strcmp(argv[index], "-m") == 0 && index + 1 < argc)
        {
            if (!parse_mode(argv[++index], &mode))
            {
                fprintf(stderr, "mkdir: invalid mode '%s'\n", argv[index]);
                return 1;
            }
            continue;
        }
        for (const char *flag = argv[index] + 1; *flag; flag++)
        {
            if (*flag == 'p')
                parents = true;
            else if (*flag != 'v')
            {
                fprintf(stderr, "mkdir: invalid option -- '%c'\n", *flag);
                return 1;
            }
        }
    }
    if (index >= argc)
    {
        fprintf(stderr, "usage: mkdir [-p] [-m mode] directory...\n");
        return 1;
    }
    int status = 0;
    for (; index < argc; index++)
        if (make_directories(argv[index], mode, parents))
            status = 1;
    return status;
}

static int applet_rmdir(int argc, char **argv)
{
    int status = 0;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1])
            continue;
        if (rmdir(argv[i]) != 0)
            status = fail("failed to remove", argv[i]);
    }
    return status;
}

static int applet_chmod(int argc, char **argv)
{
    int index = 1;
    while (index < argc && argv[index][0] == '-' && argv[index][1] &&
           strcmp(argv[index], "-R") != 0 && !strchr("rwxXst", argv[index][1]))
        index++;
    if (argc - index < 2)
    {
        fprintf(stderr, "usage: chmod mode file...\n");
        return 1;
    }
    const char *spec = argv[index++];
    int status = 0;
    for (; index < argc; index++)
    {
        struct stat info;
        if (stat(argv[index], &info) != 0)
        {
            status = fail("cannot access", argv[index]);
            continue;
        }
        unsigned int mode = info.st_mode & 07777;
        if (!parse_mode(spec, &mode))
        {
            fprintf(stderr, "chmod: invalid mode '%s'\n", spec);
            return 1;
        }
        if (chmod(argv[index], mode) != 0)
            status = fail("cannot change mode of", argv[index]);
    }
    return status;
}

static int applet_install(int argc, char **argv)
{
    bool directories = false;
    unsigned int mode = 0755;
    int index = 1;
    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++)
    {
        if (strcmp(argv[index], "--") == 0)
        {
            index++;
            break;
        }
        if (strcmp(argv[index], "-m") == 0 && index + 1 < argc)
        {
            if (!parse_mode(argv[++index], &mode))
            {
                fprintf(stderr, "install: invalid mode '%s'\n", argv[index]);
                return 1;
            }
            continue;
        }
        if (strcmp(argv[index], "-o") == 0 || strcmp(argv[index], "-g") == 0)
        {
            index++;
            continue;
        }
        for (const char *flag = argv[index] + 1; *flag; flag++)
        {
            if (*flag == 'd')
                directories = true;
            else if (*flag != 'c' && *flag != 's' && *flag != 'v' && *flag != 'p')
            {
                fprintf(stderr, "install: invalid option -- '%c'\n", *flag);
                return 1;
            }
        }
    }
    int status = 0;
    if (directories)
    {
        for (; index < argc; index++)
            if (make_directories(argv[index], mode, true))
                status = 1;
        return status;
    }
    if (argc - index < 2)
    {
        fprintf(stderr, "usage: install [-m mode] source... target\n");
        return 1;
    }
    const char *target = argv[argc - 1];
    bool into_directory = is_directory(target);
    if (argc - index > 2 && !into_directory)
    {
        fprintf(stderr, "install: target '%s' is not a directory\n", target);
        return 1;
    }
    for (int i = index; i < argc - 1; i++)
    {
        char destination[PATH_LIMIT];
        if (into_directory)
            join_path(destination, sizeof(destination), target, base_name(argv[i]));
        else
            snprintf(destination, sizeof(destination), "%s", target);
        if (copy_file(argv[i], destination, false) || chmod(destination, mode) != 0)
            status = 1;
    }
    return status;
}

static int applet_basename(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: basename name [suffix]\n");
        return 1;
    }
    char buffer[PATH_LIMIT];
    snprintf(buffer, sizeof(buffer), "%s", argv[1]);
    size_t length = strlen(buffer);
    while (length > 1 && buffer[length - 1] == '/')
        buffer[--length] = '\0';
    const char *base = base_name(buffer);
    size_t base_length = strlen(base);
    if (argc > 2)
    {
        size_t suffix = strlen(argv[2]);
        if (suffix && suffix < base_length &&
            strcmp(base + base_length - suffix, argv[2]) == 0)
            base_length -= suffix;
    }
    printf("%.*s\n", (int)base_length, base);
    return 0;
}

static int applet_dirname(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: dirname name\n");
        return 1;
    }
    char buffer[PATH_LIMIT];
    snprintf(buffer, sizeof(buffer), "%s", argv[1]);
    size_t length = strlen(buffer);
    while (length > 1 && buffer[length - 1] == '/')
        buffer[--length] = '\0';
    char *slash = strrchr(buffer, '/');
    if (!slash)
    {
        puts(".");
        return 0;
    }
    while (slash > buffer && slash[-1] == '/')
        slash--;
    if (slash == buffer)
        puts("/");
    else
    {
        *slash = '\0';
        puts(buffer);
    }
    return 0;
}

static bool parse_count(int argc, char **argv, int *index, long *count)
{
    for (; *index < argc && argv[*index][0] == '-' && argv[*index][1]; (*index)++)
    {
        if (strcmp(argv[*index], "--") == 0)
        {
            (*index)++;
            break;
        }
        if (strcmp(argv[*index], "-n") == 0 && *index + 1 < argc)
        {
            *count = atol(argv[++*index]);
            continue;
        }
        if (argv[*index][1] == 'n')
        {
            *count = atol(argv[*index] + 2);
            continue;
        }
        if (argv[*index][1] >= '0' && argv[*index][1] <= '9')
        {
            *count = atol(argv[*index] + 1);
            continue;
        }
        return false;
    }
    return true;
}

static int applet_head(int argc, char **argv)
{
    long lines = 10;
    int index = 1;
    if (!parse_count(argc, argv, &index, &lines))
    {
        fprintf(stderr, "head: invalid option\n");
        return 1;
    }
    int status = 0;
    int files = argc - index;
    for (int i = index; i < argc || (files == 0 && i == index); i++)
    {
        FILE *stream = files == 0 ? stdin : fopen(argv[i], "r");
        if (!stream)
        {
            status = fail("cannot open", argv[i]);
            continue;
        }
        if (files > 1)
            printf("%s==> %s <==\n", i > index ? "\n" : "", argv[i]);
        long seen = 0;
        int c;
        while (seen < lines && (c = fgetc(stream)) != EOF)
        {
            putchar(c);
            if (c == '\n')
                seen++;
        }
        if (stream != stdin)
            fclose(stream);
        if (files == 0)
            break;
    }
    return status;
}

static int applet_tail(int argc, char **argv)
{
    long lines = 10;
    int index = 1;
    if (!parse_count(argc, argv, &index, &lines))
    {
        fprintf(stderr, "tail: invalid option\n");
        return 1;
    }
    int status = 0;
    int files = argc - index;
    for (int i = index; i < argc || (files == 0 && i == index); i++)
    {
        FILE *stream = files == 0 ? stdin : fopen(argv[i], "r");
        if (!stream)
        {
            status = fail("cannot open", argv[i]);
            continue;
        }
        if (files > 1)
            printf("%s==> %s <==\n", i > index ? "\n" : "", argv[i]);
        size_t capacity = 4096, length = 0;
        char *data = malloc(capacity);
        int c;
        while (data && (c = fgetc(stream)) != EOF)
        {
            if (length + 1 >= capacity)
            {
                capacity *= 2;
                char *grown = realloc(data, capacity);
                if (!grown)
                {
                    free(data);
                    data = 0;
                    break;
                }
                data = grown;
            }
            data[length++] = (char)c;
        }
        if (data)
        {
            size_t start = length;
            long seen = 0;
            if (length && data[length - 1] == '\n')
                seen = -1;
            while (start > 0)
            {
                if (data[start - 1] == '\n' && ++seen >= lines)
                    break;
                start--;
            }
            fwrite(data + start, 1, length - start, stdout);
            free(data);
        }
        if (stream != stdin)
            fclose(stream);
        if (files == 0)
            break;
    }
    return status;
}

static int applet_wc(int argc, char **argv)
{
    bool show_lines = false, show_words = false, show_bytes = false;
    int index = 1;
    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++)
    {
        if (strcmp(argv[index], "--") == 0)
        {
            index++;
            break;
        }
        for (const char *flag = argv[index] + 1; *flag; flag++)
        {
            if (*flag == 'l')
                show_lines = true;
            else if (*flag == 'w')
                show_words = true;
            else if (*flag == 'c' || *flag == 'm')
                show_bytes = true;
            else
            {
                fprintf(stderr, "wc: invalid option -- '%c'\n", *flag);
                return 1;
            }
        }
    }
    if (!show_lines && !show_words && !show_bytes)
        show_lines = show_words = show_bytes = true;
    int files = argc - index;
    unsigned long total_lines = 0, total_words = 0, total_bytes = 0;
    int status = 0;
    for (int i = index; i < argc || (files == 0 && i == index); i++)
    {
        FILE *stream = files == 0 ? stdin : fopen(argv[i], "r");
        if (!stream)
        {
            status = fail("cannot open", argv[i]);
            continue;
        }
        unsigned long lines = 0, words = 0, bytes = 0;
        bool in_word = false;
        int c;
        while ((c = fgetc(stream)) != EOF)
        {
            bytes++;
            if (c == '\n')
                lines++;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                in_word = false;
            else if (!in_word)
            {
                in_word = true;
                words++;
            }
        }
        if (stream != stdin)
            fclose(stream);
        if (show_lines)
            printf("%7lu", lines);
        if (show_words)
            printf("%7lu", words);
        if (show_bytes)
            printf("%7lu", bytes);
        if (files)
            printf(" %s", argv[i]);
        printf("\n");
        total_lines += lines;
        total_words += words;
        total_bytes += bytes;
        if (files == 0)
            break;
    }
    if (files > 1)
    {
        if (show_lines)
            printf("%7lu", total_lines);
        if (show_words)
            printf("%7lu", total_words);
        if (show_bytes)
            printf("%7lu", total_bytes);
        printf(" total\n");
    }
    return status;
}

static int applet_tee(int argc, char **argv)
{
    bool append = false;
    int index = 1;
    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++)
        if (strcmp(argv[index], "-a") == 0)
            append = true;
    FILE *outputs[16];
    int output_count = 0;
    int status = 0;
    for (int i = index; i < argc && output_count < 16; i++)
    {
        FILE *stream = fopen(argv[i], append ? "a" : "w");
        if (!stream)
        {
            status = fail("cannot open", argv[i]);
            continue;
        }
        outputs[output_count++] = stream;
    }
    int c;
    while ((c = fgetc(stdin)) != EOF)
    {
        putchar(c);
        for (int i = 0; i < output_count; i++)
            fputc(c, outputs[i]);
    }
    for (int i = 0; i < output_count; i++)
        fclose(outputs[i]);
    return status;
}

static int applet_cmp(int argc, char **argv)
{
    int index = 1;
    bool silent = false;
    if (index < argc && strcmp(argv[index], "-s") == 0)
    {
        silent = true;
        index++;
    }
    if (argc - index < 2)
    {
        fprintf(stderr, "usage: cmp [-s] file1 file2\n");
        return 2;
    }
    FILE *left = fopen(argv[index], "r");
    if (!left)
        return fail("cannot open", argv[index]) + 1;
    FILE *right = fopen(argv[index + 1], "r");
    if (!right)
    {
        fclose(left);
        return fail("cannot open", argv[index + 1]) + 1;
    }
    unsigned long position = 1, line = 1;
    int status = 0;
    for (;; position++)
    {
        int a = fgetc(left);
        int b = fgetc(right);
        if (a == EOF && b == EOF)
            break;
        if (a != b)
        {
            if (!silent)
            {
                if (a == EOF || b == EOF)
                    printf("cmp: EOF on %s\n", a == EOF ? argv[index] : argv[index + 1]);
                else
                    printf("%s %s differ: char %lu, line %lu\n", argv[index],
                           argv[index + 1], position, line);
            }
            status = 1;
            break;
        }
        if (a == '\n')
            line++;
    }
    fclose(left);
    fclose(right);
    return status;
}

static int applet_env(int argc, char **argv)
{
    int index = 1;
    if (index < argc && strcmp(argv[index], "-i") == 0)
    {
        index++;
        environ[0] = 0;
    }
    for (; index < argc && strchr(argv[index], '='); index++)
    {
        char *copy = strdup(argv[index]);
        if (copy)
            putenv(copy);
    }
    if (index < argc)
    {
        execvp(argv[index], argv + index);
        return fail("cannot run", argv[index]) + 126;
    }
    for (char **entry = environ; entry && *entry; entry++)
        puts(*entry);
    return 0;
}

static int applet_uname(int argc, char **argv)
{
    bool all = false, system = false, node = false, release = false, machine = false;
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
            continue;
        for (const char *flag = argv[i] + 1; *flag; flag++)
        {
            if (*flag == 'a')
                all = true;
            else if (*flag == 's')
                system = true;
            else if (*flag == 'n')
                node = true;
            else if (*flag == 'r')
                release = true;
            else if (*flag == 'm' || *flag == 'p')
                machine = true;
        }
    }
    if (!all && !system && !node && !release && !machine)
        system = true;
    bool first = true;
    if (all || system)
        printf("%sPureC", first ? "" : " "), first = false;
    if (all || node)
        printf("%spurec", first ? "" : " "), first = false;
    if (all || release)
        printf("%s1.0", first ? "" : " "), first = false;
    if (all || machine)
        printf("%sx86_64", first ? "" : " "), first = false;
    printf("\n");
    return 0;
}

struct applet
{
    const char *name;
    int (*run)(int, char **);
};

static const struct applet applets[] = {
    {"cp", applet_cp},         {"mv", applet_mv},
    {"mkdir", applet_mkdir},   {"rmdir", applet_rmdir},
    {"chmod", applet_chmod},   {"install", applet_install},
    {"basename", applet_basename}, {"dirname", applet_dirname},
    {"head", applet_head},     {"tail", applet_tail},
    {"wc", applet_wc},         {"tee", applet_tee},
    {"cmp", applet_cmp},       {"env", applet_env},
    {"uname", applet_uname},
};

int main(int argc, char **argv)
{
    const char *name = base_name(argc > 0 ? argv[0] : "box");
    if (strcmp(name, "box") == 0 && argc > 1)
    {
        argv++;
        argc--;
        name = base_name(argv[0]);
    }
    applet_name = name;
    for (size_t i = 0; i < sizeof(applets) / sizeof(applets[0]); i++)
        if (strcmp(applets[i].name, name) == 0)
            return applets[i].run(argc, argv);
    fprintf(stderr, "box: unknown applet '%s'\n", name);
    fprintf(stderr, "applets:");
    for (size_t i = 0; i < sizeof(applets) / sizeof(applets[0]); i++)
        fprintf(stderr, " %s", applets[i].name);
    fprintf(stderr, "\n");
    return 127;
}
