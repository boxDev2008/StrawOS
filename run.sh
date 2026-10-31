qemu-system-x86_64 -cdrom strawos-x86_64.iso -m 512M -display sdl -smp 4 \
    -accel kvm -accel tcg \
    -serial stdio \
    -netdev user,id=net0,net=192.168.76.0/24,dhcpstart=192.168.76.50,hostfwd=tcp::8080-:8080 \
    -device rtl8139,netdev=net0 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-mouse,bus=xhci.0
