#!/bin/sh
### Custom devuser startup (runs on every TabletOS boot via
### /radio/scripts/post-boot.sh). ###

# Keep the factory boot budget topped up.  Every successful factory/CRC boot
# decrements BOOTCTR_FCTRY.  A value of 0 (or > 0x32) is a HARD LOCK -- the unit
# stops booting with "expired factory mode" and there is NO fallback to the
# product/ECDSA path.  Reset it to 50 (0x32) each boot while the OS is still up.
#
# 2026-10-27 FIX 1.  The value used to be built with
#     printf '\062' | dd of=/tmp/bc32.bin bs=1 count=1 conv=notrunc
# `printf` exists neither as a builtin nor as a binary on TabletOS, so the pipe
# produced nothing, the temp file stayed 00 00 00 00, and the script wrote
# 0x00000000 -- the brick value -- to BOOTCTR_FCTRY six times a boot, while
# logging "ok" because its check only re-read the counter.  The value is now
# built with `echo` and verified before any write.
#
# 2026-10-27 FIX 2.  The temp file lived in /tmp, which post-boot.sh can be
# clearing while this script runs in the background ("... &" in post-boot.sh).
# On the 16:18 boot the build silently produced nothing and the guard refused
# to write -- correctly, but the reason was invisible because dd's stderr went
# to /dev/null.  The temp file now lives next to the script, and dd's stderr is
# captured and logged on refusal.
top_up_bootctr() {
    BC=/accounts/devuser/.bc32.bin      # NOT /tmp: it is cleaned during boot
    LOG=/accounts/devuser/.bootctr.log

    i=0
    while [ ! -e /dev/rpmb/BOOTCTR_FCTRY ] && [ $i -lt 100 ]; do
        sleep 0.2
        i=$((i + 1))
    done
    if [ ! -e /dev/rpmb/BOOTCTR_FCTRY ]; then
        echo "bootctr: device missing ($(date))" >> $LOG
        return
    fi

    # Already 0x32?  Do not touch RPMB at all.
    v=$(dd if=/dev/rpmb/BOOTCTR_FCTRY bs=1 count=1 2>/dev/null)
    if [ "$v" = "2" ]; then
        echo "bootctr: already 0x32, no write ($(date))" >> $LOG
        return
    fi

    # 0x32 little-endian is 32 00 00 00; '2' == 0x32 is the low byte.
    e1=$(dd if=/dev/zero of=$BC bs=4 count=1 2>&1 >/dev/null)
    e2=$(echo 2 | dd of=$BC bs=1 count=1 conv=notrunc 2>&1 >/dev/null)

    # Never write a value we have not verified in the temp file.
    t=$(dd if=$BC bs=1 count=1 2>/dev/null)
    if [ "$t" != "2" ]; then
        echo "bootctr: REFUSING to write - temp byte '$t' (file: $(sed -n l $BC 2>&1)) dd1='$e1' dd2='$e2' ($(date))" >> $LOG
        rm -f $BC
        return
    fi

    prev=$v
    n=0
    while [ $n -lt 6 ]; do
        dd if=$BC of=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 conv=notrunc 2>/dev/null
        v=$(dd if=/dev/rpmb/BOOTCTR_FCTRY bs=1 count=1 2>/dev/null)
        if [ "$v" = "2" ]; then
            echo "bootctr: wrote 0x32, readback ok (was '$prev') ($(date))" >> $LOG
            rm -f $BC
            return
        fi
        n=$((n + 1))
        sleep 1
    done
    echo "bootctr: write FAILED after $n tries ($(date)), counter reads '$v'" >> $LOG
    rm -f $BC
}

top_up_bootctr

# Existing boot indicator.
echo "blue::on" >> /pps/services/led/status
sleep 3
echo "blue::off" >> /pps/services/led/status
