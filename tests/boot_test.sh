#!/bin/sh
# Boot the ISO headless in QEMU and assert expected text is on the VGA screen.
# The QEMU monitor's `xp` command dumps physical memory, so we read the
# 80x25x2-byte text buffer at 0xB8000 and decode every even byte (the
# character; odd bytes are color attributes).
#
# Usage: sh tests/boot_test.sh "must contain" "!must not contain" ...
set -u

ISO=${ISO:-kfs.iso}
BOOT_WAIT=${BOOT_WAIT:-6}
KEYS=${KEYS:-}

# Monitor script: wait for boot, optionally type, then dump the text buffer.
# Each sendkey is one press+release; the small sleep lets the polling kernel
# drain the 8042 between keys.
monitor_script() {
    sleep "$BOOT_WAIT"
    if [ -n "$KEYS" ]; then
        for k in $KEYS; do
            echo "sendkey $k"
            sleep 0.1
        done
        sleep 1
    fi
    echo 'xp /4000bx 0xb8000'
    sleep 1
    echo quit
}

DUMP=$(monitor_script \
    | qemu-system-i386 -cdrom "$ISO" -display none -monitor stdio -no-reboot 2>/dev/null)

SCREEN=$(printf '%s\n' "$DUMP" \
	| grep ': 0x' \
	| sed 's/^[^:]*: *//' \
	| tr ' ' '\n' \
	| awk 'NR % 2 == 1' \
	| gawk '{ printf "%c", strtonum($1) }' \
	| tr -d '\000')

echo "---- decoded VGA screen (squeezed) ----"
printf '%s\n' "$SCREEN" | tr -s ' '
echo "---------------------------------------"

status=0
for want in "$@"; do
	case "$want" in
	!*)
		pat=${want#!}
		if printf '%s' "$SCREEN" | grep -qF -- "$pat"; then
			echo "FAIL present (must be absent): $pat"
			status=1
		else
			echo "OK   absent: $pat"
		fi
		;;
	*)
		if printf '%s' "$SCREEN" | grep -qF -- "$want"; then
			echo "OK   found: $want"
		else
			echo "FAIL missing: $want"
			status=1
		fi
		;;
	esac
done

if printf '%s' "$SCREEN" | grep -qF 'SELFTEST FAIL'; then
	echo "FAIL kernel selftest reported failure"
	status=1
fi

exit $status
