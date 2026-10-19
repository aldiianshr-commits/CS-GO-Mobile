#include "game.h"
#include "collision.h"
#include "world.h"
#include "../format/bsp.h"
#include "../format/vtf.h"
#include "../fs/vfs.h"
#include "../render/renderer.h"
#include "adrenotools/driver.h"

#include <android/native_window.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace cs {

using Clock = std::chrono::steady_clock;

Game& Game::instance() {
    static Game g;
    return g;
}

void Game::setStatus(const std::string& state, const std::string& msg, float progress) {
    std::lock_guard<std::mutex> lk(stMutex_);
    state_ = state;
    message_ = msg;
    if (progress >= 0) progress_ = progress;
    if (state == "error") LOGE("%s", msg.c_str());
}

std::string Game::status() {
    std::lock_guard<std::mutex> lk(stMutex_);
    char buf[256];
    snprintf(buf, sizeof(buf), "\nprogress=%.3f\nfps=%.1f\nframeMs=%.2f\n", progress_, fps_, frameMs_);
    return "state=" + state_ + "\nmsg=" + message_ + buf + info_;
}

int Game::start(const std::string& configText) {
    std::lock_guard<std::mutex> life(lifeMutex_);
    // tunggu sesi sebelumnya benar-benar selesai
    if (running_) running_ = false;
    winCv_.notify_all();
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lk(winMutex_);
        windowChanged_ = pendingWindow_ != nullptr;
        windowAck_ = true;
    }
    gcfg_ = GraphicsConfig();
    scfg_ = SessionConfig();
    applyConfig(parseKv(configText), gcfg_, scfg_);
    {
        std::lock_guard<std::mutex> lk(inMutex_);
        moveX_ = moveY_ = rateX_ = rateY_ = accYaw_ = accPitch_ = 0;
        buttons_ = 0;
        commands_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(stMutex_);
        state_ = "loading";
        message_ = "Menyiapkan...";
        info_.clear();
        progress_ = 0;
        fps_ = frameMs_ = 0;
    }
    running_ = true;
    thread_ = std::thread(&Game::threadMain, this);
    return ++session_;
}

void Game::stop(int session) {
    std::lock_guard<std::mutex> life(lifeMutex_);
    if (session != session_) return;
    if (!running_ && !thread_.joinable()) return;
    running_ = false;
    winCv_.notify_all();
    if (thread_.joinable()) thread_.join();
    // window milik UI (dilepas lewat setWindow(nullptr) saat surface dihancurkan)
    std::lock_guard<std::mutex> lk(winMutex_);
    windowAck_ = true;
}

void Game::setWindow(ANativeWindow* w) {
    std::unique_lock<std::mutex> lk(winMutex_);
    if (pendingWindow_ && pendingWindow_ != w) ANativeWindow_release(pendingWindow_);
    else if (pendingWindow_ && pendingWindow_ == w) ANativeWindow_release(w); // referensi ganda dari fromSurface
    pendingWindow_ = w;
    windowChanged_ = true;
    windowAck_ = engineWindow_ == nullptr;
    winCv_.notify_all();
    if (!w && running_ && engineWindow_) {
        // surface akan dihancurkan: tunggu thread engine melepas swapchain
        winCv_.wait_for(lk, std::chrono::seconds(3), [&] { return windowAck_ || !running_; });
    }
}

void Game::setInput(float mx, float my, float rx, float ry, int buttons) {
    std::lock_guard<std::mutex> lk(inMutex_);
    moveX_ = mx; moveY_ = my; rateX_ = rx; rateY_ = ry; buttons_ = buttons;
}

void Game::addLook(float dyaw, float dpitch) {
    std::lock_guard<std::mutex> lk(inMutex_);
    accYaw_ += dyaw;
    accPitch_ += dpitch;
}

void Game::command(const std::string& cmd) {
    std::lock_guard<std::mutex> lk(inMutex_);
    commands_.push_back(cmd);
}

bool Game::saveState(const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "map=" << mapName_ << "\n";
    f << "origin=" << player_.origin.x << " " << player_.origin.y << " " << player_.origin.z << "\n";
    f << "angles=" << player_.pitch << " " << player_.yaw << "\n";
    f << "noclip=" << (player_.noclip ? 1 : 0) << "\n";
    f << "ducked=" << (player_.ducked ? 1 : 0) << "\n";
    f << "time=" << (long long)time(nullptr) << "\n";
    return (bool)f;
}

bool Game::loadState(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    auto kv = parseKv(ss.str());
    if (kv["map"] != mapName_) return false;
    Vec3 o = parseVec3(kv["origin"]);
    float p = 0, y = 0;
    sscanf(kv["angles"].c_str(), "%f %f", &p, &y);
    player_.origin = o;
    player_.velocity = Vec3();
    player_.pitch = p;
    player_.yaw = y;
    player_.noclip = kv["noclip"] == "1";
    player_.ducked = kv["ducked"] == "1";
    return true;
}

static std::string texturePath(const std::string& name) {
    std::string p = normPath(name);
    if (!startsWith(p, "materials/")) p = "materials/" + p;
    if (!endsWith(p, ".vtf")) p += ".vtf";
    return p;
}

bool Game::loadEverything(Renderer& r, Vfs& vfs, Bsp& bsp, WorldMesh& wm, Collision& col) {
    setStatus("loading", "Memindai file game di " + scfg_.gameDir, 0.02f);
    vfs.mountGameDir(scfg_.gameDir, [&](const std::string& m) { setStatus("loading", m, -1); });
    if (!running_) return false;
    if (vfs.fileCount() == 0) {
        setStatus("error", "Tidak ada file game di " + scfg_.gameDir + ". Salin paket XBLA/ISO/folder CS:GO Xbox 360 ke folder ini.");
        return false;
    }

    setStatus("loading", "Membaca peta " + scfg_.map, 0.12f);
    std::vector<uint8_t> data;
    if (!vfs.readFile("maps/" + scfg_.map + ".bsp", data)) {
        setStatus("error", "Peta maps/" + scfg_.map + ".bsp tidak ditemukan di file game");
        return false;
    }
    std::string err;
    if (!bsp.load(data, err)) { setStatus("error", "BSP: " + err); return false; }
    data.clear();
    data.shrink_to_fit();
    if (!bsp.pakfile.empty()) vfs.setOverlayZip(makeMemFile(std::vector<uint8_t>(bsp.pakfile)));

    setStatus("loading", "Membangun geometri dunia", 0.2f);
    WorldBuildOptions wo;
    wo.lightmaps = gcfg_.shadowQuality > 0;
    if (!buildWorld(bsp, vfs, wo, wm, err)) { setStatus("error", "Dunia: " + err); return false; }
    col.build(bsp, wm.dispTriangles);
    if (!running_) return false;

    setStatus("loading", "Menginisialisasi Vulkan", 0.3f);
    if (!r.init(gcfg_, scfg_, err)) { setStatus("error", "Vulkan: " + err); return false; }
    if (gcfg_.perfMode == 2) adrenotools_set_turbo(true);

    // tekstur material (dibagi pakai antar material)
    int mipSkip = 2 - std::max(0, std::min(2, gcfg_.textureQuality));
    std::unordered_map<std::string, int> texCache;
    size_t missing = 0, loaded = 0;
    auto getTex = [&](const std::string& name) -> int {
        if (name.empty()) return -1;
        auto it = texCache.find(name);
        if (it != texCache.end()) return it->second;
        int id = -1;
        std::vector<uint8_t> f;
        if (vfs.readFile(texturePath(name), f)) {
            TextureData td;
            std::string e;
            if (loadVtf(f, mipSkip, r.bcSupported, td, e)) { id = r.createTexture(td, name); ++loaded; }
            else LOGW("tekstur %s: %s", name.c_str(), e.c_str());
        } else {
            ++missing;
            LOGW("tekstur tidak ditemukan: %s", name.c_str());
        }
        texCache[name] = id;
        return id;
    };
    std::vector<int> base(wm.materials.size(), -1), base2(wm.materials.size(), -1);
    for (size_t i = 0; i < wm.materials.size() && running_; ++i) {
        if (i % 8 == 0)
            setStatus("loading", "Memuat tekstur " + std::to_string(i + 1) + "/" + std::to_string(wm.materials.size()),
                      0.3f + 0.6f * (float)i / std::max<size_t>(1, wm.materials.size()));
        base[i] = getTex(wm.materials[i].baseTex);
        if (wm.materials[i].blend2) base2[i] = getTex(wm.materials[i].baseTex2);
    }
    if (!running_) return false;
    setStatus("loading", "Mengunggah ke GPU", 0.92f);
    r.flushUploads();
    if (!r.uploadWorld(wm, base, base2)) { setStatus("error", "Gagal mengunggah dunia ke GPU"); return false; }

    // skybox
    if (!wm.skyName.empty()) {
        static const char* suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
        int faces[6];
        int found = 0;
        for (int f = 0; f < 6; ++f) {
            faces[f] = -1;
            for (const std::string& nm : {wm.skyName, wm.skyName + "_hdr"}) {
                Material m = loadMaterial(vfs, "skybox/" + nm + suf[f]);
                std::string t = m.found ? normPath(m.get("$basetexture")) : "skybox/" + nm + suf[f];
                if (endsWith(t, ".vtf")) t = t.substr(0, t.size() - 4);
                faces[f] = getTex(t);
                if (faces[f] >= 0) break;
            }
            if (faces[f] >= 0) ++found;
        }
        r.flushUploads();
        r.setSky(faces);
        LOGI("Skybox %s: %d/6 sisi", wm.skyName.c_str(), found);
    }
    r.flushUploads();

    char info[512];
    snprintf(info, sizeof(info),
             "gpu=%s\ndriver=%s\nturnip=%d\nturnipError=%s\nbc=%d\nmsaa=%d\ntextures=%zu\ntexMissing=%zu\nmatMissing=%zu\nfaces=%zu\ndisps=%zu\n",
             r.gpuName.c_str(), r.driverInfo.c_str(), r.usingTurnip ? 1 : 0, r.turnipError.c_str(), r.bcSupported ? 1 : 0,
             r.msaaSamples, loaded, missing, wm.missingMaterials, wm.faceCount, wm.dispCount);
    {
        std::lock_guard<std::mutex> lk(stMutex_);
        info_ = info;
    }
    return true;
}

void Game::threadMain() {
    mkdir((scfg_.gameDir + "/logs").c_str(), 0775);
    mkdir((scfg_.gameDir + "/saves").c_str(), 0775);
    mkdir(scfg_.cacheDir.c_str(), 0775);
    csgo_set_log_file(scfg_.gameDir + "/logs/last.log");
    LOGI("=== CS:GO Mobile: peta %s, folder %s ===", scfg_.map.c_str(), scfg_.gameDir.c_str());
    mapName_ = scfg_.map;

    Renderer r;
    Vfs vfs;
    Bsp bsp;
    WorldMesh wm;
    Collision col;
    bool ok = loadEverything(r, vfs, bsp, wm, col);

    // spawn
    if (ok) {
        Vec3 sp = wm.spawns.empty() ? (wm.mins + wm.maxs) * 0.5f : wm.spawns[0].origin;
        float yaw = wm.spawns.empty() ? 0 : wm.spawns[0].yaw;
        for (auto& s : wm.spawns) if (s.team == "CT") { sp = s.origin; yaw = s.yaw; break; }
        player_ = Player();
        player_.spawn(sp, yaw);
        player_.worldMinZ = wm.mins.z;
        if (wm.spawns.empty()) player_.noclip = true;
        // buang memori CPU yang tidak dibutuhkan lagi
        wm.vertices.clear(); wm.vertices.shrink_to_fit();
        wm.indices.clear(); wm.indices.shrink_to_fit();
        wm.lightmapRGBA16F.clear(); wm.lightmapRGBA16F.shrink_to_fit();
        setStatus("waiting", "Siap", 1.0f);
    }

    // ADPF performance hint (Android 13+), dimuat dinamis
    void* libandroid = dlopen("libandroid.so", RTLD_NOW);
    using GetMgr = void* (*)();
    using CreateSess = void* (*)(void*, const int32_t*, size_t, int64_t);
    using Report = int (*)(void*, int64_t);
    using CloseSess = void (*)(void*);
    using SetFrameRate = int32_t (*)(ANativeWindow*, float, int8_t);
    GetMgr getMgr = libandroid ? (GetMgr)dlsym(libandroid, "APerformanceHint_getManager") : nullptr;
    CreateSess createSess = libandroid ? (CreateSess)dlsym(libandroid, "APerformanceHint_createSession") : nullptr;
    Report report = libandroid ? (Report)dlsym(libandroid, "APerformanceHint_reportActualWorkDuration") : nullptr;
    CloseSess closeSess = libandroid ? (CloseSess)dlsym(libandroid, "APerformanceHint_closeSession") : nullptr;
    SetFrameRate setFrameRate = libandroid ? (SetFrameRate)dlsym(libandroid, "ANativeWindow_setFrameRate") : nullptr;
    void* hintSession = nullptr;
    int fpsLimit = std::max(15, std::min(144, gcfg_.fpsLimit));
    int64_t targetNs = 1000000000LL / fpsLimit;
    if (ok && gcfg_.perfMode != 0 && getMgr && createSess) {
        void* mgr = getMgr();
        int32_t tid = gettid();
        if (mgr) hintSession = createSess(mgr, &tid, 1, targetNs);
        LOGI("ADPF performance hint: %s", hintSession ? "aktif" : "tidak tersedia");
    }

    ANativeWindow* window = nullptr;
    auto lastStat = Clock::now();
    auto last = Clock::now();
    int frames = 0;
    double frameAccum = 0;
    float autosaveTimer = 0;

    while (running_ && ok) {
        // --- perubahan window dari UI thread
        {
            std::unique_lock<std::mutex> lk(winMutex_);
            if (!window && !windowChanged_) {
                winCv_.wait_for(lk, std::chrono::milliseconds(100));
            }
            if (windowChanged_) {
                windowChanged_ = false;
                ANativeWindow* nw = pendingWindow_;
                if (nw) ANativeWindow_acquire(nw); // ambil referensi sebelum UI bisa melepasnya
                engineWindow_ = nw ? nw : window;  // UI menunggu ack selama engine memakai window
                lk.unlock();
                if (window) {
                    r.detachWindow();
                    ANativeWindow_release(window);
                    window = nullptr;
                }
                if (nw) {
                    std::string err;
                    if (setFrameRate) setFrameRate(nw, (float)fpsLimit, 0);
                    if (r.attachWindow(nw, err)) {
                        window = nw;
                        setStatus("running", "Berjalan");
                    } else {
                        r.detachWindow();
                        ANativeWindow_release(nw);
                        LOGW("Surface belum siap: %s", err.c_str());
                    }
                }
                lk.lock();
                engineWindow_ = window;
                windowAck_ = true;
                winCv_.notify_all();
            }
        }
        if (!window) { last = Clock::now(); continue; }

        auto frameStart = Clock::now();
        float dt = std::chrono::duration<float>(frameStart - last).count();
        last = frameStart;

        // --- input & perintah
        PlayerInput in;
        std::vector<std::string> cmds;
        {
            std::lock_guard<std::mutex> lk(inMutex_);
            in.moveX = moveX_;
            in.moveY = moveY_;
            float sens = scfg_.sensitivity;
            in.lookYaw = accYaw_ * sens + rateX_ * 180.f * sens * dt;
            float pitchIn = accPitch_ * sens + rateY_ * 140.f * sens * dt;
            in.lookPitch = scfg_.invertY ? -pitchIn : pitchIn;
            accYaw_ = accPitch_ = 0;
            in.jump = buttons_ & BTN_JUMP;
            in.duck = buttons_ & BTN_DUCK;
            in.walk = buttons_ & BTN_WALK;
            cmds.swap(commands_);
        }
        for (auto& c : cmds) {
            if (c == "noclip") player_.noclip = !player_.noclip;
            else if (c == "respawn") player_.spawn(player_.spawnOrigin, player_.spawnYaw);
            else if (c == "save") setStatus("running", saveState(scfg_.savePath) ? "Posisi disimpan ke " + scfg_.savePath : "Gagal menyimpan");
            else if (c == "load") setStatus("running", loadState(scfg_.savePath) ? "Posisi dimuat" : "Belum ada save untuk peta ini");
            else if (startsWith(c, "spawn:")) {
                size_t i = (size_t)atoi(c.c_str() + 6);
                if (!wm.spawns.empty()) { auto& s = wm.spawns[i % wm.spawns.size()]; player_.spawn(s.origin, s.yaw); }
            }
        }
        if (!paused_) player_.update(dt, in, col);

        if (!r.drawFrame(player_.eye(), player_.pitch, player_.yaw)) {
            setStatus("error", "GPU mengalami galat (device lost). Coba driver lain atau turunkan pengaturan.");
            break;
        }

        // --- pembatas FPS
        auto workEnd = Clock::now();
        int64_t workNs = std::chrono::duration_cast<std::chrono::nanoseconds>(workEnd - frameStart).count();
        if (hintSession && report) report(hintSession, workNs);
        int64_t remain = targetNs - workNs;
        if (remain > 200000) std::this_thread::sleep_for(std::chrono::nanoseconds(remain - 150000));
        while (std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - frameStart).count() < targetNs) {}

        // --- statistik
        ++frames;
        frameAccum += workNs / 1e6;
        autosaveTimer += dt;
        auto now = Clock::now();
        float el = std::chrono::duration<float>(now - lastStat).count();
        if (el >= 0.5f) {
            std::lock_guard<std::mutex> lk(stMutex_);
            fps_ = frames / el;
            frameMs_ = (float)(frameAccum / frames);
            frames = 0;
            frameAccum = 0;
            lastStat = now;
            char pos[200];
            snprintf(pos, sizeof(pos), "pos=%.0f %.0f %.0f\nspeed=%.0f\nnoclip=%d\nrender=%ux%u\nout=%ux%u\n",
                     player_.origin.x, player_.origin.y, player_.origin.z, player_.speed2D(), player_.noclip ? 1 : 0,
                     r.sceneW, r.sceneH, r.outW, r.outH);
            size_t p = info_.find("pos=");
            info_ = (p == std::string::npos ? info_ : info_.substr(0, p)) + pos;
        }
        if (autosaveTimer > 60.f) {
            autosaveTimer = 0;
            saveState(scfg_.gameDir + "/saves/autosave_" + mapName_ + ".sav");
        }
    }

    if (ok) saveState(scfg_.gameDir + "/saves/autosave_" + mapName_ + ".sav");
    if (hintSession && closeSess) closeSess(hintSession);
    if (gcfg_.perfMode == 2) adrenotools_set_turbo(false);
    if (window) { r.detachWindow(); ANativeWindow_release(window); }
    r.shutdown();
    {
        std::lock_guard<std::mutex> lk(winMutex_);
        engineWindow_ = nullptr;
        windowAck_ = true;
        winCv_.notify_all();
    }
    {
        std::lock_guard<std::mutex> lk(stMutex_);
        if (state_ != "error") state_ = "stopped";
    }
    LOGI("Engine berhenti");
    csgo_set_log_file("");
    running_ = false;
}

} // namespace cs
