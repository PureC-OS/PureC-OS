#!/bin/sh
# Guest-side toolchain self-test: sh, make, tcc and the box utilities working together.
# Runs inside PureC OS and reports through /dev/kmsg, which reaches the serial log.

KMSG=${KMSG:-/dev/kmsg}
report() { echo "SELFTEST $1" > "$KMSG"; }
fail() { report "FAIL $1"; exit 1; }

report START
rm -rf /tmp/selftest
mkdir -p /tmp/selftest || fail mkdir
cd /tmp/selftest || fail cd

printf '#include <stdio.h>\nint main(void) { printf("selftest %%d\\n", 6 * 7); return 0; }\n' > hello.c
printf 'CC = tcc\nall: hello\nhello: hello.c\n\t$(CC) hello.c -o hello\nrun: hello\n\t./hello\n' > Makefile

make > build.log 2>&1 || fail build
[ -f ./hello ] || fail no-binary
./hello > out.txt || fail run
printf 'selftest 42\n' > expected.txt
cmp out.txt expected.txt || fail output
make -q || fail up-to-date-after-build

sleep 2
touch hello.c || fail touch
make -q
[ $? -eq 1 ] || fail rebuild-detected
make > rebuild.log 2>&1 || fail rebuild
make -q || fail up-to-date-after-rebuild
make run > run.log 2>&1 || fail make-run

printf 'all: a.out\n\t@echo done > done.txt\n%%.out: %%.in\n\tcp $< $@\n' > pattern.mk
echo data > a.in
make -f pattern.mk > pattern.log 2>&1 || fail pattern-rule
[ -f a.out ] || fail pattern-output
cmp a.in a.out || fail pattern-copy
[ -f done.txt ] || fail recipe-echo

printf 'all:\n\tfalse\n' > bad.mk
make -f bad.mk > bad.log 2>&1
[ $? -eq 2 ] || fail error-code

report PASS
