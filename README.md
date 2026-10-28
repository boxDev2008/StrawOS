<p align="center">
  <img src="content/strawos-logo.svg" width="128" alt="StrawOS logo">
</p>

<h1 align="center">StrawOS</h1>

<p align="center">A small ring 0 operating system for x86_64, with its own kernel, display server and desktop.</p>

---

## Features

**Kernel**
- Boots through [Limine](https://codeberg.org/Limine/Limine) on BIOS and UEFI
- SMP, with a preemptive scheduler driven by the local APIC timer
- Physical and virtual memory management, kernel heap, ELF loader for userspace programs
- VFS with a RAM filesystem (populated from a tar archive at boot) and a device filesystem
- Drivers: PCI, xHCI with USB HID keyboard and mouse, PS/2 mouse, RTC, RTL8139 and RTL8169 network cards
- TCP/IP networking with DHCP, DNS and a socket API

**Userspace**
- **StrawX**: a compositing display server, with a desktop shell, panels, app menu and themes
- A custom libc
- **Lua shell** (`lsh`) and a terminal
- **Apps**: Files, Notes, Paint, Pictures, Calculator, Clock, System Monitor, Settings and a 3D cube demo
- **NetSurf**: web browser, ported to StrawX
- **TinyCC**: compile C programs
- **Discord**: an experimental client
- `fetch`: a command-line HTTP(S) downloader

## Building

### Requirements

- `clang` and `ld.lld`
- `xorriso`
- Noto fonts in `/usr/share/fonts/noto` (`noto-fonts` on Arch)
- `qemu-system-x86_64` to run it

### Steps

```sh
git clone --recursive https://github.com/boxDev2008/StrawOSModern.git
cd StrawOSModern
./build.sh   # produces strawos-x86_64.iso
./run.sh     # boots it in QEMU
```

If you cloned without `--recursive`, fetch the submodules with:

```sh
git submodule update --init --recursive
```

`run.sh` uses KVM when it is available and falls back to TCG. In QEMU, click inside the window to capture the mouse and press <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>G</kbd> to release it.

## Project layout

| Path | Contents |
| --- | --- |
| `kernel/` | The kernel: CPU setup, memory, scheduler, filesystems, drivers, networking, kernel call table |
| `modules/src/` | Userspace programs: StrawX server and shell, apps, Lua shell, NetSurf and Discord frontends |
| `modules/lib/` | Userspace libraries: libc, StrawX client library, networking/TLS, and third-party submodules |
| `modules/root/` | Files copied into the OS root filesystem (icons, wallpapers, desktop entries, scripts) |
| `content/` | Project assets such as the logo |
| `limine/` | Limine bootloader binaries (submodule) |

## Third-party code

StrawOS bundles these projects as git submodules:
[Limine](https://codeberg.org/Limine/Limine),
[Lua](https://github.com/lua/lua),
[BearSSL](https://www.bearssl.org/),
[NetSurf](https://www.netsurf-browser.org/) and its libraries,
[stb](https://github.com/nothings/stb) and
[TinyCC](https://repo.or.cz/tinycc.git).
Each one keeps its own license. Wallpaper credits are in `modules/root/pictures/CREDITS.txt`.
