# PROJECT CONTEXT: LEGO Marvel Super Heroes (Universal Android ARM64 Native Loader)

## 1. Target Platform & Prinsip Desain
- **Game:** LEGO MARVEL Super Heroes (Title ID: `01006F600FFC8000`)
- **Target Device:** Semua Android ARM64 (`arm64-v8a`), dioptimalkan untuk **Low-End Devices (RAM 3-4 GB, Vulkan 1.0/1.1, Adreno / Mali / PowerVR)**.
- **Workflow:** 100% Android Termux + GitHub Actions (Tanpa PC/Laptop).
- **Package Name:** `com.scratch.legomarvel`
- **Data Path di HP:** `/sdcard/LegoPort/`
  - `exefs/main.elf` -> Binary game Switch hasil `nx2elf`
  - `romfs/` -> Folder aset game hasil `hactool`
  - `boot_log.txt` -> Log eksekusi, profil hardware, SVC Horizon OS, dan `[MISSING SYMBOL]`

## 2. Arsitektur File C++ (`app/src/main/cpp/`)
- `device_profile.h` : Deteksi otomatis RAM fisik (`sysinfo`), klasifikasi tier (`LOW_END` <= 4.5 GB vs `STANDARD`), dan batas heap aman.
- `logger.h` : Pencatat log real-time ke Android Logcat dan `/sdcard/LegoPort/boot_log.txt`.
- `elf_loader.cpp` : Pemetaan segmen ELF64 (`mmap`), patch instruksi `TPIDRRO_EL0 -> TPIDR_EL0` & `SVC -> BRK`, Switch TLR, dan signal crash handler.
- `symbol_table.cpp` : Pemetaan nama fungsi mangled Switch (`_ZN2nn...` & `nvn*`) ke implementasi C++.
- `shims_os.cpp` : Implementasi `nn::init`, `nn::os` (manajemen heap adaptif, thread, mutex).
- `shims_fs.cpp` : Implementasi `nn::fs` (streaming langsung dari `/sdcard/LegoPort/romfs`).
- `shims_nvn.cpp` : Implementasi `nvn` ke Vulkan 1.0/1.1 multi-vendor (Adreno/Mali/PowerVR).
- `main.cpp` : Entry point `android_main()` + permintaan izin `MANAGE_EXTERNAL_STORAGE` otomatis.

## 3. Instruksi untuk AI Saat Membantu Fix Bug
1. Berikan kode **hanya untuk file yang perlu diubah** secara utuh agar mudah di-replace di Termux.
2. Jaga agar kode tetap **universal (Adreno/Mali/PowerVR, Vulkan 1.0+)** dan **hemat RAM (target aman <= 1.2 GB heap di mode Low-End)**.
3. Pastikan setiap fungsi shim baru didaftarkan di `symbol_table.cpp`.
