#!/bin/sh
# Host test for src/programs/box: builds box and checks every applet in a scratch directory.
set -u
BOX_BIN=${1:?usage: box_test.sh /path/to/box}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
failures=0

for applet in cp mv mkdir rmdir chmod install basename dirname head tail wc tee cmp env uname true false sleep; do
    ln -s "$BOX_BIN" "$WORK/$applet"
done
cd "$WORK" || exit 1
mkdir data && cd data || exit 1
export PATH="$WORK:$PATH"

check() {
    if [ "$2" != "$3" ]; then
        echo "FAIL $1: expected [$2] got [$3]"
        failures=$((failures + 1))
    fi
}

printf 'a\nb\nc\nd\n' > f1

../mkdir -p x/y/z; check "mkdir -p" 0 $?
[ -d x/y/z ]; check "mkdir -p creates tree" 0 $?
../mkdir -p x/y/z; check "mkdir -p existing" 0 $?
../mkdir x/y/z 2>/dev/null; check "mkdir existing fails" 1 $?

../cp f1 f2; check "cp" 0 $?
../cmp f1 f2; check "cmp equal" 0 $?
printf 'zzz' > f3
../cmp -s f1 f3; check "cmp differ" 1 $?
../cp -r x x2; check "cp -r" 0 $?
[ -d x2/y/z ]; check "cp -r copies tree" 0 $?
../cp nope zz 2>/dev/null; check "cp missing fails" 1 $?
../cp x zz 2>/dev/null; check "cp dir without -r fails" 1 $?

../mv f2 x/; check "mv into dir" 0 $?
[ -f x/f2 ] && [ ! -e f2 ]; check "mv moved" 0 $?
../mv x2 x3; check "mv dir" 0 $?
[ -d x3/y/z ]; check "mv dir contents" 0 $?
../mv nope zz 2>/dev/null; check "mv missing fails" 1 $?

check "head -n" "a,b," "$(../head -n 2 f1 | tr '\n' ,)"
check "head -N" "a," "$(../head -1 f1 | tr '\n' ,)"
check "tail -n" "c,d," "$(../tail -n 2 f1 | tr '\n' ,)"
check "tail -N" "d" "$(../tail -1 f1)"
check "wc -l" "4" "$(../wc -l < f1 | tr -d ' ')"
check "wc" "4 4 8" "$(../wc f1 | awk '{print $1, $2, $3}')"
check "basename" "c" "$(../basename /a/b/c.txt .txt)"
check "basename trailing slash" "b" "$(../basename /a/b/)"
check "dirname" "/a/b" "$(../dirname /a/b/c)"
check "dirname bare" "." "$(../dirname c)"
check "dirname root" "/" "$(../dirname /c)"
check "uname" "PureC x86_64" "$(../uname -sm)"
check "env" "bar" "$(../env FOO=bar sh -c 'echo $FOO')"

../chmod 600 f1; check "chmod octal" 600 "$(stat -c %a f1)"
../chmod u+x f1; check "chmod u+x" 700 "$(stat -c %a f1)"
../chmod a+x,go-w f1; check "chmod comma" 711 "$(stat -c %a f1)"
../chmod u=rw,g=r,o= f1; check "chmod equals" 640 "$(stat -c %a f1)"
../chmod 7z f1 2>/dev/null; check "chmod invalid fails" 1 $?

../install -m 755 f1 ins; check "install" 755 "$(stat -c %a ins)"
../install -d d1/d2; [ -d d1/d2 ]; check "install -d" 0 $?
echo hi | ../tee t1 t2 > /dev/null
check "tee" "hi hi" "$(cat t1 t2 | tr '\n' ' ' | sed 's/ $//')"
../rmdir d1/d2; check "rmdir" 0 $?
../rmdir d1/d2 2>/dev/null; check "rmdir missing fails" 1 $?

../sleep 0.1; check "sleep fraction" 0 $?
../sleep abc 2>/dev/null; check "sleep invalid fails" 1 $?
../true; check "true" 0 $?
../false; check "false" 1 $?

if [ $failures -ne 0 ]; then
    echo "box_test: $failures failure(s)"
    exit 1
fi
echo "box_test: ok"
