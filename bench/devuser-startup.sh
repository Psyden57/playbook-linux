#!/bin/sh
### Custom devuser startup (runs on every TabletOS boot via
### /radio/scripts/post-boot.sh). ###

# Keep the factory boot budget topped up.  Every successful factory/CRC boot
# decrements BOOTCTR_FCTRY.  A value of 0 (or > 0x32) makes the bootloader
# fall back to the product/ECDSA path, where the resealed os_type=6 image no
# longer boots.  Reset it to 50 (0x32) each boot while the OS is still up.
# The readback check matters: an early write can be rejected by the RPMB
# resmgr, and logging success without verifying would hide a draining counter.
top_up_bootctr() {
    i=0
    while [ ! -e /dev/rpmb/BOOTCTR_FCTRY ] && [ $i -lt 100 ]; do
        sleep 0.2
        i=$((i + 1))
    done
    if [ ! -e /dev/rpmb/BOOTCTR_FCTRY ]; then
        echo "bootctr: device missing ($(date))" >> /accounts/devuser/.bootctr.log
        return
    fi

    n=0
    while [ $n -lt 6 ]; do
        dd if=/dev/zero of=/tmp/bc32.bin bs=4 count=1 2>/dev/null
        printf '\062' | dd of=/tmp/bc32.bin bs=1 count=1 conv=notrunc 2>/dev/null
        dd if=/tmp/bc32.bin of=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 conv=notrunc 2>/dev/null
        v=$(dd if=/dev/rpmb/BOOTCTR_FCTRY bs=1 count=1 2>/dev/null)
        if [ "$v" = "2" ]; then
            echo "bootctr -> 0x32 ok ($(date))" >> /accounts/devuser/.bootctr.log
            rm -f /tmp/bc32.bin
            return
        fi
        n=$((n + 1))
        sleep 1
    done
    echo "bootctr: top-up FAILED after $n tries ($(date)), first byte='$v'" >> /accounts/devuser/.bootctr.log
    rm -f /tmp/bc32.bin
}

top_up_bootctr

# Existing boot indicator.
echo "blue::on" >> /pps/services/led/status
sleep 3
echo "blue::off" >> /pps/services/led/status
