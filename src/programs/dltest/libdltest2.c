extern int dltest_add(int a, int b);

int dltest2_twice(int value)
{
    return dltest_add(value, value);
}
