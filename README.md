# Akai S5000 / S6000 (IB-S56USB) — 64-bit Windows WinUSB Proxy Shim (No-Log Release)

[![Windows Support](https://img.shields.io/badge/Windows-7%20%7C%2010%20%7C%2011%20(64--bit)-0078D6?logo=windows&logoColor=white)](https://github.com/connectivezone/Aksys-shim-no-logging)
[![ak.Sys Compatibility](https://img.shields.io/badge/ak.Sys-v1.53%20Compatible-brightgreen)](https://github.com/connectivezone/Aksys-shim-no-logging)
[![Hardware](https://img.shields.io/badge/Hardware-Akai%20IB--S56USB-crimson)](https://github.com/connectivezone/Aksys-shim-no-logging)
[![Build](https://img.shields.io/badge/Build-v10.1%20Release%20%2F%20No--Log-success)](https://github.com/connectivezone/Aksys-shim-no-logging/releases)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A lightweight 32-bit user-mode WinUSB proxy shim (`version.dll`) that allows **Akai ak.Sys (v1.53)** to run seamlessly on **64-bit Windows 11, Windows 10, and Windows 7** without custom kernel drivers, without disabling Windows Driver Signature Enforcement, and without test-signing mode.

This is the **Release / No-Logging** build: all diagnostic file writing has been compiled out into inline no-ops, providing zero disk I/O overhead and maximum USB transfer bandwidth for high-speed sample dumps, program transfers, and real-time screen streaming.

---

## ⚡ The Problem Solved

The original 1999–2002 Akai driver (`akS56USB.sys`) is a legacy 32-bit Windows 98/2000 kernel driver. Modern 64-bit Windows versions strictly enforce 64-bit kernel driver signing, preventing original drivers from loading.

Rather than installing unsigned 64-bit third-party kernel drivers or putting Windows into insecure test-signing modes, this project leverages **Microsoft's built-in, digitally signed `WinUSB` (`winusb.sys`) driver** for direct hardware communication. The shim sits in user space and transparently intercepts legacy Win32 I/O calls inside `S56Device.dll` via dynamic Import Address Table (IAT) hooks and API trampolines.

### Key Capabilities
* **Full Remote Panel LCD Viewer**: Handles the complete 9,600-byte (320×240) LCD bitmap stream with multi-chunk framebuffer reassembly and zero timeouts.
* **Bi-directional Sample & Program Transfers**: Supports high-speed transfer of `.WAV` samples, multis, programs, and virtual folder structures.
* **Zero Disk I/O Overhead**: Diagnostics and file logging are disabled in this build to maximize USB throughput and eliminate disk wear.
* **No Test Mode or Kernel Hacks**: Functions on clean, standard Windows 10 and 11 installations with Secure Boot and Driver Signature Enforcement fully active.
* **Windows 7 64-bit Compatible**: Operates on 64-bit Windows 7 when WinUSB is bound to the hardware.

---

## 📦 Compatibility Matrix

| Hardware / Environment | Supported | Configuration Method |
| :--- | :---: | :--- |
| **Akai S5000 / S6000** | ✅ | Must have the **IB-S56USB** expansion board installed |
| **Akai ak.Sys** | ✅ | Version **1.53** (x86 32-bit application) |
| **Windows 11 (64-bit)** | ✅ | Native driver selection in Device Manager |
| **Windows 10 (64-bit)** | ✅ | Native driver selection in Device Manager |
| **Windows 7 (64-bit)** | ✅ | Assign WinUSB using [Zadig](https://zadig.akeo.ie/) |

---

## 🚀 Quick Start (Pre-Built Binary)

If you downloaded `version.dll` from the [Releases](https://github.com/connectivezone/Aksys-shim-no-logging/releases) tab:

### Step 1: Assign Microsoft WinUSB in Device Manager

1. Power on your Akai S5000/S6000 and connect the USB cable to your PC.
2. Press `Win + X` and select **Device Manager**.
3. Under **Other devices**, locate the entry for your sampler (typically displayed as `AKAI S5000/S6000 USB` or `Unknown USB Device` with a yellow warning icon).
4. Right-click the device and choose **Update driver** &rarr; **Browse my computer for drivers** &rarr; **Let me pick from a list of available drivers on my computer**.
5. Select **Universal Serial Bus devices** (near the bottom) and click **Next**.
6. Select:
   * **Manufacturer**: `WinUsb Device`
   * **Model**: `WinUsb Device`
7. Click **Next**, click **Yes** to confirm the prompt, and finish the installation.

*(Note for 64-bit Windows 7 users: Open [Zadig](https://zadig.akeo.ie/), select the Akai sampler, choose **WinUSB** as the target driver, and click "Install Driver".)*

### Step 2: Copy `version.dll`

1. Ensure `aksys.exe` is closed.
2. Copy `version.dll` into the `devices` subfolder of your ak.Sys installation directory (default path):
   ```
   C:\Program Files (x86)\AKAIPRO\aksys\devices\version.dll
   ```
3. *(Recommended)* Also copy `version.dll` into the root directory alongside `aksys.exe`:
   ```
   C:\Program Files (x86)\AKAIPRO\aksys\version.dll
   ```

#### Directory Layout Verification
```
C:\Program Files (x86)\AKAIPRO\aksys\
├── aksys.exe
├── AKComCtrls.dll
├── version.dll                  <-- Recommended
└── devices\
    ├── S56Device.dll            <-- Original Akai module (KEEP THIS!)
    └── version.dll              <-- REQUIRED: Drop shim here
```

### Step 3: Launch `ak.Sys`
Turn on your sampler, start `ak.Sys`, and your S5000/S6000 will appear in the device browser tree ready for sample transfers and Remote Panel control.

---

## 🔬 How It Works (Technical Overview)

The shim employs user-mode DLL sideloading via the system `version.dll` library name:

```
[aksys.exe / S56Device.dll]
          │
          ▼ (Loads local version.dll proxy)
   ┌─────────────────────────────────────────────────────────┐
   │                  version.dll Shim                       │
   │  - Export forwarding to C:\Windows\SysWOW64\version.dll │
   │  - IAT Hooking: CreateFileA, ReadFile, WriteFile,       │
   │                 DeviceIoControl, CancelIo, SetupAPI     │
   │  - SetupAPI Enumeration Spoofing                        │
   │  - WDM Overlapped Completion Lifecycle Emulation       │
   │  - 64 KB Read Buffer + Multi-Packet LCD Frame Assembly  │
   └──────────────────────────┬──────────────────────────────┘
                              │
                              ▼ (Translates to WinUSB calls)
                      [winusb.sys (Kernel)]
                              │
                              ▼ (Bulk IN: 0x82 / Bulk OUT: 0x02)
                   [Akai IB-S56USB Hardware]
```

1. **DLL Proxy & Redirection**: `ak.Sys` and `S56Device.dll` statically link Windows versioning APIs (`GetFileVersionInfoA`, `VerQueryValueA`, etc.). Placing `version.dll` in the application or `devices` folder causes Windows to load the proxy shim first. Non-intercepted exports are forwarded directly to `C:\Windows\SysWOW64\version.dll`.
2. **SetupAPI Emulation**: `S56Device.dll` searches for the legacy Akai device interface GUID `{9D4B6529-D4A6-11D3-8EAE-0050DA2E2D53}`. The shim hooks `SetupDiGetClassDevsA`, `SetupDiEnumDeviceInterfaces`, and `SetupDiGetDeviceInterfaceDetailA`, redirecting the discovery queries to the active WinUSB device node.
3. **Overlapped Completion Emulation**: The legacy Akai communication module expects kernel-style asynchronous overlapped behavior (`IoMarkIrpPending` / immediate return of `FALSE` with `ERROR_IO_PENDING` on bulk reads). The shim manages caller event handles and asynchronous monitor routines to prevent handshake dropouts.
4. **Framebuffer Stream Assembly**: The Remote Panel LCD stream transmits 9,600 bytes across successive 64-byte bulk packets. The shim provisions a 64 KB transfer buffer and aggregates incoming chunks until packet completion, preventing endpoint desynchronization.

---

## 🛠️ Building from Source

The shim compiles into a 32-bit x86 DLL using the Microsoft Visual C++ compiler (`cl.exe`).

### Prerequisites
* Microsoft Visual Studio (2017, 2019, 2022, or 2026 Build Tools) with Desktop C++ components.
* Windows SDK (providing `winusb.lib`, `setupapi.lib`).

### Build Steps

1. Open an **x86 Native Tools Command Prompt for VS** (or Developer PowerShell).
2. Clone the repository:
   ```bash
   git clone https://github.com/connectivezone/Aksys-shim-no-logging.git
   cd Aksys-shim-no-logging
   ```
3. Run the automated build script:
   ```cmd
   .\build_akai_win11_shim.bat
   ```
   Or invoke the MSVC compiler directly:
   ```cmd
   cl.exe /nologo /O2 /LD /EHsc akS56USB_WinUSB_Shim.cpp /link /OUT:version.dll /MACHINE:X86 setupapi.lib winusb.lib user32.lib advapi32.lib ole32.lib
   ```
4. Collect the generated `version.dll` (approximately 20–35 KB).

---

## ❓ Troubleshooting

* **Sampler not showing in ak.Sys**:
  * Verify in Device Manager that the sampler hardware is bound to `WinUsb Device` (Hardware ID `USB\VID_09E8&PID_005E`).
  * Ensure `version.dll` is placed in `C:\Program Files (x86)\AKAIPRO\aksys\devices\`.
* **Sampler displays "USB Error" on LCD screen**:
  * If the sampler previously encountered a timeout from an aborted session, clear the prompt using the front-panel soft keys or power-cycle the unit before reopening `ak.Sys`.
* **Remote Panel viewer freezes**:
  * Ensure you are running build v10.1 or later, which contains the 64 KB transfer buffer for multi-chunk LCD frame assembly.

---

## 📄 License & Disclaimer

This project is open-source software distributed under the [MIT License](LICENSE).

*Akai, Akai Professional, S5000, S6000, and ak.Sys are trademarks or registered trademarks of their respective owners. This project is an independent community compatibility initiative and is not affiliated with, endorsed by, or associated with inMusic Brands or Akai Professional.*