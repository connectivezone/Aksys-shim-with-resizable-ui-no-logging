# Akai S5000 / S6000 (IB-S56USB) — 64-bit Windows WinUSB Proxy Shim & High-DPI UI Scaler (No-Log Release)

[![Windows Support](https://img.shields.io/badge/Windows-7%20%7C%2010%20%7C%2011%20(64--bit)-0078D6?logo=windows&logoColor=white)](https://github.com/connectivezone/Aksys-shim-no-logging)
[![ak.Sys Compatibility](https://img.shields.io/badge/ak.Sys-v1.53%20Compatible-brightgreen)](https://github.com/connectivezone/Aksys-shim-no-logging)
[![Hardware](https://img.shields.io/badge/Hardware-Akai%20IB--S56USB-crimson)](https://github.com/connectivezone/Aksys-shim-no-logging)
[![UI Scaler](https://img.shields.io/badge/High--DPI%20UI%20Scaler-1.0x--2.0x%20%2B%20Free%20Resize-00b4d8)](https://github.com/connectivezone/Aksys-shim-no-logging)
[![Build](https://img.shields.io/badge/Build-v13.3%20Release%20%2F%20No--Log-success)](https://github.com/connectivezone/Aksys-shim-no-logging/releases)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A lightweight 32-bit user-mode WinUSB proxy shim and High-DPI UI scaler (`version.dll`) that allows **Akai ak.Sys (v1.53)** to run seamlessly on **64-bit Windows 11, Windows 10, and Windows 7** without custom kernel drivers, without disabling Windows Driver Signature Enforcement, and without test-signing mode—while upgrading `ak.Sys` with **real-time resizable editor windows**, **floating zoom controls**, and **High-DPI / 4K scaling**.

This is the **v13.3 Production Release / No-Logging** build: all diagnostic file writing (`akai_shim_debug.log`) and debug hooks have been stripped out, providing zero disk I/O overhead and maximum USB transfer bandwidth for high-speed sample dumps, program transfers, and real-time screen streaming.

---

## ⚡ The Problems Solved

### 1. 64-Bit USB Connectivity Without Unsigned Kernel Drivers
The original 1999–2002 Akai driver (`akS56USB.sys`) is a legacy 32-bit Windows 98/2000 kernel driver. Modern 64-bit Windows versions strictly enforce 64-bit kernel driver signing, preventing original drivers from loading.

Rather than installing unsigned 64-bit third-party kernel drivers or putting Windows into insecure test-signing modes, this project leverages **Microsoft's built-in, digitally signed `WinUSB` (`winusb.sys`) driver** for direct hardware communication. The shim sits in user space and transparently intercepts legacy Win32 I/O calls inside `S56Device.dll` via dynamic Import Address Table (IAT) hooks and API trampolines.

### 2. High-DPI & 4K Resizable UI for Modern Monitors
`ak.Sys` was designed in 1999–2001 with fixed-pixel bitmaps that appear tiny on modern 1080p, 1440p, and 4K displays, and its sub-windows could not be resized. This shim adds a complete 32-bit `DIBSection` offscreen backbuffer and halftone GDI scaling engine that makes every `ak.Sys` editor window freely resizable and zoomable.

### Key Capabilities
* **Floating Zoom Control Bar (`1.0x` – `2.0x` + Aspect Ratio Lock)**: Runs on its own dedicated UI message pump thread so it never stalls during heavy USB transfers. Includes one-click presets (`1.0x`, `1.25x`, `1.5x` default, `1.75x`, `2.0x`) and a **Lock Original Aspect Ratio When Resizing** toggle for manual window border dragging.
* **S5000 / S6000 Remote Panel LCD Viewer (`540×368` base)**: Handles the complete 9,600-byte (320×240) LCD bitmap stream with multi-chunk framebuffer reassembly, scales smoothly to any size, strips out the raw `RGB(255, 0, 255)` magenta chroma-key border, and applies a geometrically scaled rounded bezel region.
* **Full 4-Rack Program Editor (`766×546` base)**: Scales all 4 synthesis racks (*Common/Program Tune*, *Keyspan Piano Roll*, *Keygroup Filter/LFOs*, and *Zones/Envelopes*), dynamically adjusts when racks are toggled via `Themes ▼`, and automatically repairs stuck/collapsed Program Editor layout keys in `HKCU\Software\AKSys\ak.Sys`.
* **32-Channel Multi Mixer (`1600×522` base) & 4-Rack EB20 FX Editor (`650×352` base)**: Unlocks all 32 Multi mixer channels (`1A–16A`, `1B–16B`) and all 4 EB20 effects units (`FX1`, `FX2`, `RV3`, `RV4`) with zero scrollbar clipping or black overhang bars.
* **Scaled Owner-Drawn Dropdown Menus & 100% Accurate Mouse Mapping**: Intercepts `WM_MEASUREITEM` and `WM_DRAWITEM` to scale popup dropdown menus (`MIDI 1A..16B`, `OUTPUT`, `FX CHAN`, `Themes ▼`, filter types, etc.) and inverse-maps mouse coordinates (`ScreenToClient`, `ClientToScreen`, `WM_LBUTTONDOWN`, `WM_MOUSEMOVE`) so rotary knobs, sliders, envelope handles, and soft-keys respond at their exact scaled screen positions.
* **Bi-directional Sample & Program Transfers**: Supports high-speed transfer of `.WAV` samples, multis, programs, and virtual folder structures.
* **Zero Disk I/O Overhead**: Diagnostics and file logging are completely removed in this build to maximize USB throughput and eliminate disk wear.
* **No Test Mode or Kernel Hacks**: Functions on clean, standard Windows 10 and 11 installations with Secure Boot and Driver Signature Enforcement fully active.

---

## 📦 Compatibility Matrix

| Hardware / Environment | Supported | Configuration Method |
| :--- | :---: | :--- |
| **Akai S5000 / S6000** | ✅ | Must have the **IB-S56USB** expansion board installed |
| **Akai ak.Sys** | ✅ | Version **1.53** (x86 32-bit application) |
| **Windows 11 (64-bit)** | ✅ | Native driver selection in Device Manager (or Zadig) |
| **Windows 10 (64-bit)** | ✅ | Native driver selection in Device Manager (or Zadig) |
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

*(Note for 64-bit Windows 7 users—or as a one-click alternative on Windows 10/11: Open [Zadig](https://zadig.akeo.ie/), select the Akai sampler (`09E8:005E`), choose **WinUSB** as the target driver, and click "Install Driver".)*

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

### Step 3: Launch `ak.Sys` & Use the Zoom Controls
1. Turn on your sampler and start `ak.Sys`.
2. Your S5000/S6000 will appear in the device browser tree ready for sample transfers and Remote Panel control.
3. Use the floating **ak.sys Zoom Controls (v13.3)** toolbar to switch between `1.0x`, `1.25x`, `1.5x` (default), `1.75x`, and `2.0x` scaling, or drag the borders/corners of any sub-window (`Program Editor`, `Multi Mixer`, `EB20 Effects`, or `AKAI S5000/S6000` Remote Panel) to resize freely.

---

## 🔬 How It Works (Technical Overview)

The shim employs user-mode DLL sideloading via the system `version.dll` library name:

```
[aksys.exe / S56Device.dll / AKComCtrls.dll / MFC42.dll]
          │
          ▼ (Loads local version.dll proxy)
   ┌───────────────────────────────────────────────────────────────┐
   │                  version.dll Shim (v13.3)                     │
   │  - Export forwarding to C:\Windows\SysWOW64\version.dll       │
   │  - USB IAT Hooking: CreateFileA, ReadFile, WriteFile,         │
   │                     DeviceIoControl, CancelIo, SetupAPI       │
   │  - UI / GDI Hooking: BitBlt, GetClientRect, InvalidateRect,   │
   │                      SetWindowRgn, ScreenToClient,            │
   │                      ClientToScreen, WM_MEASURE/DRAWITEM      │
   │  - 32-Bit DIBSection Backbuffer + Halftone Stretch Engine     │
   │  - 64 KB Read Buffer + Multi-Packet LCD Frame Assembly        │
   └──────────────────────────────┬────────────────────────────────┘
                                  │
                                  ▼ (Translates to WinUSB calls)
                          [winusb.sys (Kernel)]
                                  │
                                  ▼ (Bulk IN: 0x82 / Bulk OUT: 0x02)
                       [Akai IB-S56USB Hardware]
```

1. **DLL Proxy & Redirection**: `ak.Sys` and `S56Device.dll` statically link Windows versioning APIs (`GetFileVersionInfoA`, `VerQueryValueA`, etc.). Placing `version.dll` in the application or `devices` folder causes Windows to load the proxy shim first. Non-intercepted exports are forwarded directly to `C:\Windows\SysWOW64\version.dll`.
2. **SetupAPI Emulation**: `S56Device.dll` searches for the legacy Akai device interface GUID `{9D4B6529-D4A6-11D3-8EAE-0050DA2E2D53}`. The shim hooks `SetupDiGetClassDevsA`, `SetupDiEnumDeviceInterfaces`, and `SetupDiGetDeviceInterfaceDetailA`, redirecting discovery queries to the active WinUSB device node.
3. **Overlapped Completion Emulation**: The legacy Akai communication module expects kernel-style asynchronous overlapped behavior (`IoMarkIrpPending` / immediate return of `FALSE` with `ERROR_IO_PENDING` on bulk reads). The shim manages caller event handles and asynchronous monitor routines to prevent handshake dropouts.
4. **Framebuffer Stream Assembly**: The Remote Panel LCD stream transmits 9,600 bytes across successive 64-byte bulk packets. The shim provisions a 64 KB transfer buffer and aggregates incoming chunks until packet completion, preventing endpoint desynchronization.
5. **High-DPI Offscreen Backbuffer & Coordinate Mapper**: Subclasses `AfxFrameOrView42` editor frames and their inner `Afx:400000:8` canvases. `Hook_GetClientRect` reports 1x native dimensions back to `ak.Sys` so its internal layout engine renders 1:1 into a persistent 32-bit top-down `DIBSection` backbuffer (`hBackDC`), which is then chroma-cleaned and stretched to the resized window via `StretchBlt(..., HALFTONE)`. Mouse and menu coordinates are inverse-scaled in real time.

---

## 🛠️ Building from Source

The shim compiles into a 32-bit x86 DLL using the Microsoft Visual C++ compiler (`cl.exe`).

### Prerequisites
* Microsoft Visual Studio (2017, 2019, 2022, or 2026 Build Tools) with Desktop C++ (x86) components.
* Windows SDK (providing `winusb.lib`, `setupapi.lib`, `gdi32.lib`, `user32.lib`).

### Build Steps

1. Open an **Administrator x86 Native Tools Command Prompt for VS** (or run the batch file as Administrator—it will auto-locate `vcvars32.bat`).
2. Clone the repository:
   ```bash
   git clone https://github.com/connectivezone/Aksys-shim-with-resizable-ui-no-logging.git
   cd Aksys-shim-with-resizable-ui-no-logging
   ```
3. Run the automated build script (which also cleans any stuck collapsed editor layout keys in `HKCU\Software\AKSys\ak.Sys` and installs `version.dll` directly into your `ak.Sys` folder):
   ```cmd
   .\build_akai_win11_shim.bat
   ```
   Or invoke the MSVC compiler directly:
   ```cmd
   cl.exe /nologo /O2 /LD /EHsc akS56USB_WinUSB_Shim.cpp /link /OUT:version.dll /MACHINE:X86 setupapi.lib winusb.lib user32.lib gdi32.lib advapi32.lib ole32.lib
   ```
4. Collect the generated `version.dll`.

---

## ❓ Troubleshooting

* **Sampler not showing in ak.Sys**:
  * Verify in Device Manager that the sampler hardware is bound to `WinUsb Device` (Hardware ID `USB\VID_09E8&PID_005E`).
  * Ensure `version.dll` is placed in `C:\Program Files (x86)\AKAIPRO\aksys\devices\`.
* **Sampler displays "USB Error" on LCD screen**:
  * If the sampler previously encountered a timeout from an aborted session, clear the prompt using the front-panel soft keys or power-cycle the unit before reopening `ak.Sys`.
* **Program Editor opens showing only the top row of knobs**:
  * Click **`Themes ▼`** in the top-left corner of the Program Editor's brown wooden header bar to toggle the lower synthesis racks on, or run `build_akai_win11_shim.bat` to automatically reset stuck Registry layout flags in `HKCU\Software\AKSys\ak.Sys`.

---

## 📄 License & Disclaimer

This project is open-source software distributed under the [MIT License](LICENSE).

*Akai, Akai Professional, S5000, S6000, and ak.Sys are trademarks or registered trademarks of their respective owners. This project is an independent community compatibility initiative and is not affiliated with, endorsed by, or associated with inMusic Brands or Akai Professional.*
