extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

int __real_main(int argc, char **argv);

int __wrap_main(int argc, char **argv)
{
    for (void (**entry)(void) = __init_array_start; entry < __init_array_end; entry++)
        (*entry)();
    return __real_main(argc, argv);
}
