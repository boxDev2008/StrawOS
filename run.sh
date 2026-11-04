#!/bin/bash
set -e
cd "$(dirname "$0")"

OVMF=${OVMF:-/usr/share/edk2/x64}
DISK=vm/disk.img

mkdir -p vm
[ -f "$DISK" ] || qemu-img create -f raw "$DISK" ${STRAWOS_DISK_SIZE:-2G}

if [ "$1" = "disk" ]; then
    [ -f vm/vars.fd ] || cp "$OVMF/OVMF_VARS.4m.fd" vm/vars.fd
    BOOT=(-drive if=pflash,format=raw,readonly=on,file="$OVMF/OVMF_CODE.4m.fd"
          -drive if=pflash,format=raw,file=vm/vars.fd)
elif [ "$1" = "disk-bios" ]; then
    BOOT=()
else
    BOOT=(-cdrom strawos-x86_64.iso -boot d)
fi

qemu-system-x86_64 -M q35 -m 512M -display sdl -smp 4 \
    -accel kvm -accel tcg \
    -serial stdio \
    "${BOOT[@]}" \
    -drive file="$DISK",if=none,id=nvme0,format=raw \
    -device nvme,serial=strawos0,drive=nvme0 \
    -netdev user,id=net0,net=192.168.76.0/24,dhcpstart=192.168.76.50,hostfwd=tcp::8080-:8080 \
    -device rtl8139,netdev=net0 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-mouse,bus=xhci.0 \
    -audiodev ${STRAWOS_AUDIO:-pipewire},id=snd0 \
    -device intel-hda \
    -device hda-duplex,audiodev=snd0
