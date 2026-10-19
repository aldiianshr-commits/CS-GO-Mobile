# CS:GO Mobile

Aplikasi **native Android** (C++17 + Vulkan) yang membaca **file asli Counter-Strike: Global Offensive versi Xbox 360 milik Anda** dan menjalankan petanya langsung di Android — **tanpa emulator**. Target utama: **Xiaomi Pad 6 (Snapdragon 870 / Adreno 650)** dengan dukungan **driver Turnip** (Mesa) melalui adrenotools.

> Proyek pribadi/komunitas, tidak berafiliasi dengan Valve atau Microsoft. Aplikasi ini **tidak berisi aset game apa pun** — semua peta, tekstur, dan material dibaca dari file yang Anda salin sendiri.

## Pendekatan teknis (dan kenapa)

CS:GO Xbox 360 adalah program PowerPC (`default.xex`) untuk CPU Xenon + GPU Xenos. Menjalankan kode itu di ARM hanya mungkin lewat **emulasi** (seperti Xenia) atau **rekompilasi statis** (seperti XenonRecomp/Unleashed Recompiled — butuh berbulan-bulan kerja manual per game, plus reimplementasi kernel Xbox dan GPU Xenos). Keduanya bukan "tanpa emulator" atau tidak realistis untuk proyek ini.

Jadi proyek ini memakai pendekatan **engine native yang membaca format data asli**:

| Lapisan | Yang diimplementasikan |
|---|---|
| Kontainer | Paket XBLA **STFS** (`LIVE`/`PIRS`/`CON`, termasuk rantai blok hash), **ISO XDVDFS** (XGD1/2/3/polos), folder hasil ekstrak, arsip **ZIP Valve** (`*.360.zip`, entri store/deflate/LZMA + header LZMA Valve), **VPK** v1/v2 (LE/BE) |
| Peta | **BSP konsol** (`PSBV`, big-endian, lump terkompresi LZMA), v19–21: face, edge, texinfo, displacement, lightmap (LDR/HDR), entitas, pakfile, node/leaf/brush untuk kolisi |
| Tekstur | **VTFX** (Xbox 360/PS3) & VTF PC: DXT1/3/5 (dengan byte-swap 16-bit 360), RGBA/BGRA/565/I8/IA88/A8/16F, LZMA, rantai mip |
| Material | **VMT** KeyValues: `LightmappedGeneric`, `WorldVertexTransition` (blend 2 tekstur), `UnlitGeneric`, `Water`, `$alphatest`, `$translucent`, `$additive`, `$color`, shader `patch` |
| Render | **Vulkan** (via volk): atlas lightmap RGBA16F, skybox 6 sisi, FXAA / MSAA, render scale, anisotropi, pipeline cache, letterbox aspect ratio |
| Gameplay | Gerak pemain gaya Source (gesekan, akselerasi, lompat, jongkok, step 18 unit), kolisi kotak terhadap brush BSP, lantai displacement, noclip, titik spawn T/CT |

### Batasan teknis yang **belum** terselesaikan

Ini perlu jelas sejak awal — yang Anda dapatkan adalah **mode jelajah peta**, bukan pertandingan CS:GO lengkap:

- **Logika game asli tidak berjalan**: senjata, peluru/damage, bot, ronde, ekonomi, bom, HUD asli, menu asli. Semua itu ada di kode PowerPC `default.xex` yang tidak dieksekusi.
- **Model `.mdl`** (props statis, senjata, karakter) belum dirender — peta tampil tanpa props seperti tong, mobil, dll.
- **Audio, partikel, decal, refleksi air/cubemap, bump/normal map, bayangan dinamis** belum ada. "Kualitas Bayangan" mengatur bayangan statis (lightmap) yang memang dipakai CS:GO untuk dunia.
- **Multiplayer** tidak ada.
- Paket **Games on Demand (SVOD/GOD)** belum didukung — ubah ke ISO atau ekstrak isinya.
- Parser divalidasi dengan **data uji sintetis** yang mengikuti spesifikasi (lihat `tests/`), belum dengan file CS:GO 360 asli karena file game tidak tersedia di lingkungan build. Jika ada peta/tekstur yang gagal dimuat, kirim `/CSGO/logs/last.log` agar bisa diperbaiki.
- Driver **Turnip** hanya bisa diuji di perangkat Adreno sungguhan; jalur kode adrenotools sudah ada dan otomatis kembali ke driver sistem bila gagal.

## Pengujian yang sudah berjalan otomatis (CI)

Setiap push menjalankan:

1. **Uji parser di host** (`tests/format_tests.cpp`): LZMA Valve, BSP big-endian + lump LZMA, VTFX DXT1 (byte-swap 360, dengan/tanpa LZMA), ZIP + VMT `patch`, KeyValues biner, STFS (blok berurutan & rantai hash), atlas lightmap, kolisi + gerak pemain, dekoder BC3.
2. **Build APK rilis** (arm64-v8a) — hasil `aapt2`, tanda tangan, dan simbol JNI dicatat di cabang `ci-report`.
3. **Uji asap di emulator Android 13** (build x86_64 khusus uji, Vulkan SwiftShader): memasang APK, menyalin folder game sintetis berformat Xbox 360 (paket STFS + `zip0.360.zip` + `de_test.360.bsp`) ke `/sdcard/CSGO`, memindai, memuat peta dengan preset Medium / Ultra (MSAA 4x, skala 125%) / Very Low, menggerakkan pemain lewat sentuhan, menyimpan posisi, lalu mengambil tangkapan layar & log ke cabang `ci-smoke`.

Otomasi/pintasan: `adb shell am start -n com.aldiianshr.csgo/.MainActivity --es autostart de_dust2 --ei preset 2` langsung memuat peta (preset opsional 0–4).

## Instalasi

1. Unduh **`CSGO-Mobile.apk`** dari halaman [Releases](../../releases) (atau artefak `CSGO-Mobile-apk` di tab Actions).
2. Buka file APK di tablet → izinkan *Instal aplikasi tidak dikenal* untuk browser/file manager → **Instal**.
   Rilis memakai kunci tanda tangan yang sama, jadi versi baru bisa langsung di-install di atas versi lama.
3. Buka **CS:GO** → tekan **Izinkan akses file** → aktifkan *Izinkan akses untuk mengelola semua file* (dibutuhkan untuk membaca `/storage/emulated/0/CSGO/`).
4. Salin file game Anda ke **`/storage/emulated/0/CSGO/`**, salah satu dari:
   - Paket XBLA asli (file STFS tanpa ekstensi, biasanya dari `Content/0000000000000000/<TitleID>/000D0000/`) — boleh beserta struktur foldernya;
   - ISO Xbox 360;
   - Folder hasil ekstrak (berisi `csgo/maps/*.360.bsp`, `csgo/materials`, `*.360.zip` atau `*_dir.vpk`).
5. Tekan **Pindai ulang** → daftar peta muncul → ketuk peta (mis. `de_dust2`).

Struktur folder yang dibuat aplikasi:

```
/storage/emulated/0/CSGO/
├── (file game Anda)
├── saves/      ← <peta>.sav (Simpan/Muat posisi), autosave_<peta>.sav
├── drivers/    ← taruh zip driver Turnip di sini
├── cache/      ← pipeline_cache.bin (Shader Cache)
└── logs/       ← last.log (log engine sesi terakhir)
```

Folder lain dapat dipilih lewat **Pilih folder…** (SAF) di layar utama.

## Kontrol

- **Sentuh**: joystick mengambang di separuh kiri, geser di kanan untuk kamera, tombol LOMPAT / JONGKOK / JALAN / NOCLIP / SPAWN / SIMPAN / MUAT / MENU. Tata letak, ukuran, opasitas, sensitivitas, dan invert Y diatur di **Kontrol & Gamepad**.
- **Gamepad** (Bluetooth/USB): stik kiri/D-pad gerak, stik kanan kamera, R2 lompat, L2 jongkok; tombol A/B/L3/Y/START/R1/L1/X dapat dipetakan ulang.
- **Keyboard fisik**: WASD, Spasi, Ctrl, Shift, N (noclip), F5/F9 (simpan/muat), Esc (menu).

## Pengaturan grafis

| Opsi | Nilai | Implementasi |
|---|---|---|
| Resolusi | 480p / 720p / 900p / 1080p | `SurfaceHolder.setFixedSize` (lebar mengikuti rasio layar; compositor menskalakan) |
| Render Scale | 50–150% | ukuran target render 3D offscreen relatif ke resolusi |
| FPS Limit | 30 / 45 / 60 | pembatas frame + `ANativeWindow_setFrameRate` (panel 144 Hz Pad 6 ikut turun) |
| Kualitas Tekstur | Rendah / Sedang / Tinggi | melewati 2 / 1 / 0 mip terbesar dari VTF |
| Kualitas Bayangan | Mati / Rendah / Sedang / Tinggi | lightmap mati / nearest / bilinear / bilinear + tonemapping ACES |
| Anti-Aliasing | Mati / FXAA / MSAA | FXAA di pass akhir; MSAA 4x (atau 2x) dengan resolve, otomatis mati jika tidak didukung |
| Anisotropic Filtering | Mati / 2x–16x | sampler anisotropy, dibatasi `maxSamplerAnisotropy` |
| V-Sync | Aktif / Nonaktif | present mode FIFO vs MAILBOX/IMMEDIATE |
| Shader Cache | Aktif / Nonaktif | `VkPipelineCache` disimpan di `/CSGO/cache`, divalidasi per driver |
| GPU Driver | System / Turnip | Turnip dimuat lewat **adrenotools** dari zip; gagal → kembali otomatis ke driver sistem |
| Mode Performa | Hemat Daya / Seimbang / Performa Tinggi | afinitas thread ke inti efisien/besar (dibaca dari cpufreq), sustained performance mode, ADPF performance hint, GPU turbo (KGSL) |
| Aspect Ratio | Otomatis / 16:9 / 16:10 | viewport letterbox |
| Fullscreen | Aktif / Nonaktif | sembunyikan bar sistem (immersive) |
| Preset | Very Low → Ultra | mengisi opsi di atas; mengubah satu opsi → "Kustom" |
| Reset | — | kembali ke default (Medium) |

## Driver Turnip (Adreno 650)

1. Unduh zip driver Turnip berformat **adrenotools** (berisi `meta.json` + `libvulkan_freedreno.so`), mis. dari rilis repositori *AdrenoToolsDrivers* di GitHub.
2. Taruh di `/CSGO/drivers/` lalu **Driver GPU (Turnip) → Impor dari folder drivers** (atau *Impor zip Turnip…*).
3. Driver diekstrak ke folder privat aplikasi (dlopen tidak diizinkan dari penyimpanan bersama) dan diaktifkan.
4. HUD menampilkan nama GPU; `[Turnip]` berarti driver Mesa aktif. Jika gagal, alasannya tampil di HUD dan log.

## Build dari source

Butuh JDK 17, Android SDK 35, NDK 27.2.12479018, CMake 3.22.1.

```sh
./gradlew assembleRelease          # APK: app/build/outputs/apk/release/app-release.apk
cmake -S tests -B build-tests && cmake --build build-tests && ./build-tests/format_tests   # uji parser di PC
GLSLANG=/path/glslang tools/compile_shaders.sh   # hanya jika shader GLSL diubah
```

GitHub Actions (`.github/workflows/build.yml`) menjalankan uji parser lalu membangun APK di setiap push; tag `v*` otomatis membuat GitHub Release berisi APK.

## Struktur kode

```
app/src/main/cpp/
├── fs/vfs.*          STFS, XDVDFS, ZIP, VPK, indeks path (.360 dinormalisasi)
├── format/           lzma_valve, bsp, vtf (VTFX/VTF + dekoder DXT), vmt
├── game/             world (mesh+lightmap+displacement), collision, player, game (thread engine), config
├── render/renderer.* Vulkan + adrenotools
├── shaders/          GLSL + SPIR-V tertanam
└── jni_bridge.cpp
app/src/main/java/com/aldiianshr/csgo/   UI Android (peluncur, pengaturan, kontrol, driver, layar game)
tests/                uji host dengan data sintetis (BSP big-endian + LZMA, VTFX, ZIP, STFS, kolisi)
```

## Lisensi pihak ketiga

- [adrenotools](https://github.com/bylaws/libadrenotools) — BSD-2-Clause
- [volk](https://github.com/zeux/volk) — MIT
- [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) — Apache-2.0 / MIT
- LZMA SDK (Igor Pavlov) — domain publik
