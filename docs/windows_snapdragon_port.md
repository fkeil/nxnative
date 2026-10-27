# Porting nxnative to Windows on ARM64 (Snapdragon & Upcoming Nvidia Chips)

The core premise of `nxnative` is that Nintendo Switch games are compiled for an ARM64 processor (the Tegra X1). Instead of emulating the CPU instructions, `nxnative` runs them directly on Apple Silicon (which is also ARM64), acting only as a translation layer for the OS and graphics.

Because Windows on ARM devices (like those with Snapdragon X Elite or upcoming Nvidia ARM PC chips) are also built on ARM64 architecture, this exact same "native execution" approach is entirely possible. However, the host environment and graphics stack would need to be adapted. Here is a breakdown of how a Windows ARM64 port would work:

## 1. CPU and Native Execution
Since the Snapdragon processor is an ARM64 chip, the game's machine code can execute natively on the CPU without instruction emulation, ensuring maximum performance just like on Apple Silicon.

- **Page Sizes:** The Switch uses 4 KiB memory pages. Apple Silicon uses 16 KiB pages, which forces `nxnative` to use complex workarounds (like instruction rewriting and alignment fixups) to load Switch binaries. Windows on ARM supports 4 KiB pages natively. This would actually make the memory mapping and binary loading **easier and cleaner** on Snapdragon than on Apple Silicon.
- **NSO Rewriter (`patch_nso.py`):** The rewriter would still be needed to patch out Switch-specific Supervisor Calls (SVCs) and handle Thread Local Storage (TLS). Instead of rewriting TLS to use Apple's ABI, it would be rewritten to use the Windows ARM64 Thread Environment Block (TEB) via the `tpidr_el0` register or Windows-specific TLS slots.

## 2. Graphics Translation (NVN to Vulkan/DirectX 12)
`nxnative` currently translates the Switch's proprietary NVN graphics API to Apple's Metal API (`dm/gfx/nvn`).
- **Graphics API:** For Windows, this layer would need to be rewritten to translate NVN calls to **Vulkan** or **DirectX 12**. Given that the project already uses Vulkan headers and SPIR-V internally, a Vulkan backend would be the most natural fit. Snapdragon Adreno GPUs have excellent Vulkan support.
- **Shader Translation:** The project currently uses `Ryujinx.Graphics.Shader` to translate Maxwell (Switch GPU) shaders into SPIR-V, and then uses SPIRV-Cross to convert them to Metal Shading Language (MSL). For Windows, the conversion to MSL would be dropped. The generated **SPIR-V could be consumed directly by Vulkan**, simplifying the shader pipeline.
- **Upcoming Nvidia ARM Chips:** If Windows on ARM hardware features Nvidia graphics, the graphics translation layer would be even more seamless. Since the Switch uses an Nvidia Tegra X1 (Maxwell architecture), modern Nvidia PC chips would share a common architectural lineage. This could mean fewer compatibility workarounds for things like proprietary texture formats, depth behaviors, or shader execution quirks, allowing NVN calls and shaders to map very cleanly to the desktop GPU.

## 3. Host System and Horizon OS (SVCs)
The host layer (`dm/host/`) acts as a stand-in for the Switch's Horizon OS. It intercepts SVCs (system calls) and routes them to the host OS.
- **Memory Management:** The Switch's memory management (`svcMapMemory`, `svcSetHeapSize`) is currently implemented using POSIX `mmap`. On Windows, this would be ported to use Win32 memory APIs, specifically `VirtualAlloc2`, `MapViewOfFile3`, and Windows' memory placeholder concepts to handle memory aliasing and reservation.
- **Threading and Synchronization:** The Switch's threading and synchronization primitives (Mutexes, Condition Variables, Address Arbiters) are currently built on top of `pthread` and POSIX sync primitives. These would be mapped to Windows APIs like `CreateThread`, `WaitOnAddress` (which maps perfectly to the Switch's Address Arbiter), and `SRWLock`.

## 4. Peripherals and OS Services
- **Audio:** CoreAudio on macOS/iOS would be replaced by **WASAPI** or **XAudio2** for low-latency sound playback.
- **Input:** Apple's GameController framework would be replaced with **XInput** or **Windows.Gaming.Input** to read Xbox, DualSense, and other standard PC controllers.
- **Windowing:** The AppKit/UIKit shell would be replaced with a standard **Win32** or **WinUI 3** windowing application.

## Conclusion
A Windows on ARM port (whether on Snapdragon or future Nvidia chips) is highly feasible and would benefit tremendously from the native ARM64 execution that `nxnative` pioneers. In fact, due to Windows on ARM's support for 4 KiB memory pages, native Vulkan drivers, and potentially highly-compatible Nvidia GPUs in the future, some of the most complex subsystems in the Apple Silicon version (like memory alignment patching and Metal shader cross-compilation) could be simplified or removed entirely.
