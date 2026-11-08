#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

static void check(long result)
{
    if (result < 0)
    {
        mp_raise_OSError(errno);
    }
}

static mp_obj_t posix_open(size_t n_args, const mp_obj_t *args)
{
    int mode = n_args > 2 ? (int)mp_obj_get_int(args[2]) : 0644;
    int fd = open(mp_obj_str_get_str(args[0]), (int)mp_obj_get_int(args[1]), mode);
    check(fd);
    return mp_obj_new_int(fd);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(posix_open_obj, 2, 3, posix_open);

static mp_obj_t posix_read(mp_obj_t fd_in, mp_obj_t count_in)
{
    mp_int_t count = mp_obj_get_int(count_in);
    if (count < 0)
    {
        mp_raise_ValueError(MP_ERROR_TEXT("negative count"));
    }
    byte *buffer = m_new(byte, count ? count : 1);
    ssize_t got = read((int)mp_obj_get_int(fd_in), buffer, (size_t)count);
    if (got < 0)
    {
        int saved = errno;
        m_del(byte, buffer, count ? count : 1);
        mp_raise_OSError(saved);
    }
    mp_obj_t result = mp_obj_new_bytes(buffer, (size_t)got);
    m_del(byte, buffer, count ? count : 1);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_2(posix_read_obj, posix_read);

static mp_obj_t posix_write(mp_obj_t fd_in, mp_obj_t data_in)
{
    mp_buffer_info_t info;
    mp_get_buffer_raise(data_in, &info, MP_BUFFER_READ);
    ssize_t put = write((int)mp_obj_get_int(fd_in), info.buf, info.len);
    check(put);
    return mp_obj_new_int(put);
}
static MP_DEFINE_CONST_FUN_OBJ_2(posix_write_obj, posix_write);

static mp_obj_t posix_close(mp_obj_t fd_in)
{
    check(close((int)mp_obj_get_int(fd_in)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(posix_close_obj, posix_close);

static mp_obj_t posix_lseek(mp_obj_t fd_in, mp_obj_t offset_in, mp_obj_t whence_in)
{
    long long position = lseek((int)mp_obj_get_int(fd_in), (long)mp_obj_get_int(offset_in),
                               (int)mp_obj_get_int(whence_in));
    check((long)position);
    return mp_obj_new_int_from_ll(position);
}
static MP_DEFINE_CONST_FUN_OBJ_3(posix_lseek_obj, posix_lseek);

static mp_obj_t posix_unlink(mp_obj_t path)
{
    check(unlink(mp_obj_str_get_str(path)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(posix_unlink_obj, posix_unlink);

static mp_obj_t posix_mkdir(mp_obj_t path)
{
    check(mkdir(mp_obj_str_get_str(path), 0755));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(posix_mkdir_obj, posix_mkdir);

static mp_obj_t posix_rmdir(mp_obj_t path)
{
    check(rmdir(mp_obj_str_get_str(path)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(posix_rmdir_obj, posix_rmdir);

static mp_obj_t posix_rename(mp_obj_t from, mp_obj_t to)
{
    check(rename(mp_obj_str_get_str(from), mp_obj_str_get_str(to)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(posix_rename_obj, posix_rename);

static mp_obj_t posix_stat(mp_obj_t path)
{
    struct stat info;
    check(stat(mp_obj_str_get_str(path), &info));
    mp_obj_t items[2] = {
        mp_obj_new_int_from_uint(info.st_mode),
        mp_obj_new_int_from_ll((long long)info.st_size),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(posix_stat_obj, posix_stat);

static mp_obj_t posix_access(mp_obj_t path)
{
    return mp_obj_new_bool(access(mp_obj_str_get_str(path), 0) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(posix_access_obj, posix_access);

static mp_obj_t posix_listdir(mp_obj_t path)
{
    DIR *directory = opendir(mp_obj_str_get_str(path));
    if (!directory)
    {
        mp_raise_OSError(errno);
    }
    mp_obj_t list = mp_obj_new_list(0, NULL);
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL)
    {
        mp_obj_list_append(list, mp_obj_new_str(entry->d_name, strlen(entry->d_name)));
    }
    closedir(directory);
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(posix_listdir_obj, posix_listdir);

static mp_obj_t posix_getpid(void)
{
    return mp_obj_new_int(getpid());
}
static MP_DEFINE_CONST_FUN_OBJ_0(posix_getpid_obj, posix_getpid);

static mp_obj_t posix_getcwd(void)
{
    char buffer[256];
    if (!getcwd(buffer, sizeof(buffer)))
    {
        mp_raise_OSError(errno);
    }
    return mp_obj_new_str(buffer, strlen(buffer));
}
static MP_DEFINE_CONST_FUN_OBJ_0(posix_getcwd_obj, posix_getcwd);

static mp_obj_t posix_pipe(void)
{
    int fds[2];
    check(pipe(fds));
    mp_obj_t items[2] = {mp_obj_new_int(fds[0]), mp_obj_new_int(fds[1])};
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(posix_pipe_obj, posix_pipe);

static mp_obj_t posix_dup2(mp_obj_t from, mp_obj_t to)
{
    check(dup2((int)mp_obj_get_int(from), (int)mp_obj_get_int(to)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(posix_dup2_obj, posix_dup2);

static mp_obj_t posix_fork(void)
{
    int pid = fork();
    check(pid);
    return mp_obj_new_int(pid);
}
static MP_DEFINE_CONST_FUN_OBJ_0(posix_fork_obj, posix_fork);

static mp_obj_t posix_waitpid(mp_obj_t pid_in)
{
    int status = 0;
    int pid = waitpid((int)mp_obj_get_int(pid_in), &status, 0);
    check(pid);
    mp_obj_t items[2] = {mp_obj_new_int(pid), mp_obj_new_int(WEXITSTATUS(status))};
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(posix_waitpid_obj, posix_waitpid);

static mp_obj_t posix_exit(mp_obj_t code)
{
    _exit((int)mp_obj_get_int(code));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(posix_exit_obj, posix_exit);

static const mp_rom_map_elem_t posix_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_posix)},
    {MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&posix_open_obj)},
    {MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&posix_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&posix_write_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&posix_close_obj)},
    {MP_ROM_QSTR(MP_QSTR_lseek), MP_ROM_PTR(&posix_lseek_obj)},
    {MP_ROM_QSTR(MP_QSTR_unlink), MP_ROM_PTR(&posix_unlink_obj)},
    {MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&posix_mkdir_obj)},
    {MP_ROM_QSTR(MP_QSTR_rmdir), MP_ROM_PTR(&posix_rmdir_obj)},
    {MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&posix_rename_obj)},
    {MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&posix_stat_obj)},
    {MP_ROM_QSTR(MP_QSTR_access), MP_ROM_PTR(&posix_access_obj)},
    {MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&posix_listdir_obj)},
    {MP_ROM_QSTR(MP_QSTR_getpid), MP_ROM_PTR(&posix_getpid_obj)},
    {MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&posix_getcwd_obj)},
    {MP_ROM_QSTR(MP_QSTR_pipe), MP_ROM_PTR(&posix_pipe_obj)},
    {MP_ROM_QSTR(MP_QSTR_dup2), MP_ROM_PTR(&posix_dup2_obj)},
    {MP_ROM_QSTR(MP_QSTR_fork), MP_ROM_PTR(&posix_fork_obj)},
    {MP_ROM_QSTR(MP_QSTR_waitpid), MP_ROM_PTR(&posix_waitpid_obj)},
    {MP_ROM_QSTR(MP_QSTR_exit), MP_ROM_PTR(&posix_exit_obj)},
    {MP_ROM_QSTR(MP_QSTR_O_RDONLY), MP_ROM_INT(O_RDONLY)},
    {MP_ROM_QSTR(MP_QSTR_O_WRONLY), MP_ROM_INT(O_WRONLY)},
    {MP_ROM_QSTR(MP_QSTR_O_RDWR), MP_ROM_INT(O_RDWR)},
    {MP_ROM_QSTR(MP_QSTR_O_CREAT), MP_ROM_INT(O_CREAT)},
    {MP_ROM_QSTR(MP_QSTR_O_TRUNC), MP_ROM_INT(O_TRUNC)},
    {MP_ROM_QSTR(MP_QSTR_O_APPEND), MP_ROM_INT(O_APPEND)},
    {MP_ROM_QSTR(MP_QSTR_SEEK_SET), MP_ROM_INT(0)},
    {MP_ROM_QSTR(MP_QSTR_SEEK_CUR), MP_ROM_INT(1)},
    {MP_ROM_QSTR(MP_QSTR_SEEK_END), MP_ROM_INT(2)},
};
static MP_DEFINE_CONST_DICT(posix_globals, posix_globals_table);

const mp_obj_module_t mp_module_posix = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&posix_globals,
};

MP_REGISTER_MODULE(MP_QSTR_posix, mp_module_posix);
