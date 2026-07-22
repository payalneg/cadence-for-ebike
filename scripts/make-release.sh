#!/usr/bin/env bash
# Prepare a firmware release: pristine west build + copy the hex into release/.
# Usage: ./scripts/make-release.sh [-v VERSION] [-n]   (-n = package existing build/ without rebuilding)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

version=""
nobuild=0
while getopts "v:n" opt; do
	case $opt in
	v) version=$OPTARG ;;
	n) nobuild=1 ;;
	*) exit 2 ;;
	esac
done

if [ "$nobuild" -eq 0 ]; then
	. "$root/scripts/ncs-env.sh"
	west build -b nrf52dk/nrf52810 -p always "$root"
fi

# Hex location differs between sysbuild and classic builds — take the first that exists.
hex=""
for c in build/merged.hex build/zephyr/zephyr.hex build/cadence-for-ebike/zephyr/zephyr.hex; do
	if [ -f "$root/$c" ]; then hex="$root/$c"; break; fi
done
[ -n "$hex" ] || { echo "no built hex found under build/ — run without -n" >&2; exit 1; }

[ -n "$version" ] || version="$(git -C "$root" describe --tags --always --dirty)"

mkdir -p "$root/release"
name="bk6ls-cadence-$version.hex"
cp "$hex" "$root/release/$name"
hash=$(shasum -a 256 "$root/release/$name" | awk '{print $1}')
cat > "$root/release/bk6ls-cadence-$version.txt" <<EOF
BK6LS-Cadence firmware release
version : $version
date    : $(date +"%Y-%m-%d %H:%M")
commit  : $(git -C "$root" rev-parse HEAD)
source  : $hex
sha256  : $hash
flash   : nrfjprog --family NRF52 --program $name --chiperase --verify --reset
EOF
echo "Release ready: release/$name  (sha256 $hash)"
