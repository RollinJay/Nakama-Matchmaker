#!/bin/sh
# Copies the server modules of the IKEMEN GO and melonDS forks into server/modules
# (server/modules/README.md), from checkouts of the two forks.
#
# usage: tools/sync-server-modules.sh <IkemenGo-Nakama checkout> <melonDS fork checkout>
set -eu
if [ $# -ne 2 ]; then
	echo "usage: $0 <IkemenGo-Nakama checkout> <melonDS fork checkout>" >&2
	exit 2
fi
here=$(cd "$(dirname "$0")/.." && pwd)
ikemen=$1/nakama/modules
melonds=$2/src/net/rollback/server/melonds_channel.lua
[ -d "$ikemen" ] || { echo "no $ikemen" >&2; exit 1; }
[ -f "$melonds" ] || { echo "no $melonds" >&2; exit 1; }

rm -f "$here"/server/modules/ikemen/*.lua
cp "$ikemen"/*.lua "$here/server/modules/ikemen/"
cp "$1/LICENCE.txt" "$here/server/modules/ikemen/LICENSE"
# the channel module is MIT, like this repository; the rest of melonDS is GPL
grep -q 'SPDX-License-Identifier: MIT' "$melonds" || { echo "$melonds is not marked MIT" >&2; exit 1; }
cp "$melonds" "$here/server/modules/melonds/"
cp "$here/LICENSE" "$here/server/modules/melonds/LICENSE"

echo "IKEMEN GO modules from $(git -C "$1" rev-parse --short HEAD 2>/dev/null || echo "$1")"
echo "melonDS module from $(git -C "$2" rev-parse --short HEAD 2>/dev/null || echo "$2")"
echo "Update the commits in server/modules/README.md."
