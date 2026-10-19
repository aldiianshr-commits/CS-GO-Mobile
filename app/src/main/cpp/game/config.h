// Konfigurasi grafis & sesi, dikirim dari Java sebagai teks "kunci=nilai\n"
#pragma once
#include <map>
#include <string>

namespace cs {

struct GraphicsConfig {
    int resolution = 1080;     // 480/720/900/1080 (tinggi buffer layar; diterapkan via SurfaceHolder.setFixedSize)
    int renderScale = 100;     // 50/75/100/125/150 (% resolusi render 3D)
    int fpsLimit = 60;         // 30/45/60
    int textureQuality = 2;    // 0 rendah, 1 sedang, 2 tinggi
    int shadowQuality = 2;     // 0 mati, 1 rendah, 2 sedang, 3 tinggi (lightmap statis)
    int antiAliasing = 1;      // 0 mati, 1 FXAA, 2 MSAA
    int anisotropy = 4;        // 0/2/4/8/16
    bool vsync = true;
    bool shaderCache = true;
    int gpuDriver = 0;         // 0 sistem, 1 Turnip
    int perfMode = 1;          // 0 hemat daya, 1 seimbang, 2 performa tinggi
    int aspect = 0;            // 0 otomatis, 1 16:9, 2 16:10
    bool fullscreen = true;
    float fovDeg = 90.f;       // FOV horizontal (pada 4:3, seperti CS:GO)
    bool showFps = true;
};

struct SessionConfig {
    std::string gameDir;       // /storage/emulated/0/CSGO
    std::string map;           // nama peta, mis. de_dust2
    std::string nativeLibDir;  // untuk hook adrenotools
    std::string filesDir;      // direktori privat aplikasi
    std::string driverDir;     // folder privat berisi libvulkan_freedreno.so (Turnip)
    std::string driverName;    // nama file driver
    std::string savePath;      // /CSGO/saves/<map>.sav
    std::string cacheDir;      // /CSGO/cache
    float sensitivity = 1.0f;
    bool invertY = false;
};

inline std::map<std::string, std::string> parseKv(const std::string& text) {
    std::map<std::string, std::string> m;
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(i, e - i);
        size_t eq = line.find('=');
        if (eq != std::string::npos) m[line.substr(0, eq)] = line.substr(eq + 1);
        i = e + 1;
    }
    return m;
}

inline void applyConfig(const std::map<std::string, std::string>& kv, GraphicsConfig& g, SessionConfig& s) {
    auto gi = [&](const char* k, int& v) { auto it = kv.find(k); if (it != kv.end()) v = atoi(it->second.c_str()); };
    auto gb = [&](const char* k, bool& v) { auto it = kv.find(k); if (it != kv.end()) v = it->second == "1" || it->second == "true"; };
    auto gf = [&](const char* k, float& v) { auto it = kv.find(k); if (it != kv.end()) v = (float)atof(it->second.c_str()); };
    auto gs = [&](const char* k, std::string& v) { auto it = kv.find(k); if (it != kv.end()) v = it->second; };
    gi("resolution", g.resolution);
    gi("renderScale", g.renderScale);
    gi("fpsLimit", g.fpsLimit);
    gi("textureQuality", g.textureQuality);
    gi("shadowQuality", g.shadowQuality);
    gi("antiAliasing", g.antiAliasing);
    gi("anisotropy", g.anisotropy);
    gb("vsync", g.vsync);
    gb("shaderCache", g.shaderCache);
    gi("gpuDriver", g.gpuDriver);
    gi("perfMode", g.perfMode);
    gi("aspect", g.aspect);
    gb("fullscreen", g.fullscreen);
    gf("fov", g.fovDeg);
    gb("showFps", g.showFps);
    gs("gameDir", s.gameDir);
    gs("map", s.map);
    gs("nativeLibDir", s.nativeLibDir);
    gs("filesDir", s.filesDir);
    gs("driverDir", s.driverDir);
    gs("driverName", s.driverName);
    gs("savePath", s.savePath);
    gs("cacheDir", s.cacheDir);
    gf("sensitivity", s.sensitivity);
    gb("invertY", s.invertY);
}

} // namespace cs
