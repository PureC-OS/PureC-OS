#!/bin/sh
set -eu
root=$1
repo=$2
stage=$3
pkg=$4
key=$root/rootfs/etc/pkg/keys/purec.pub

rm -rf "$stage"
mkdir -p "$stage/etc/pkg/keys"
conf=$stage.conf
printf '[options]\nArchitecture = x86_64\n' > "$conf"
if ls "$repo"/*.sig >/dev/null 2>&1 && [ -f "$key" ]; then
	printf 'SigLevel = Required\n' >> "$conf"
	cp "$key" "$stage/etc/pkg/keys/purec.pub"
else
	printf 'SigLevel = Never\n' >> "$conf"
fi

"$pkg" --root "$stage" --config "$conf" --noconfirm --nodeps -U "$repo"/*.pkg.tar.gz
"$pkg" --root "$stage" --config "$conf" --noconfirm -D --asdeps init sh coreutils net-tools disk-utils accounts system pkg >/dev/null
rm -rf "$stage/etc" "$conf"
find "$stage" -type f | wc -l
