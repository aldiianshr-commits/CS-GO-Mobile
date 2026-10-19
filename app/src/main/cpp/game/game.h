// Inti sesi permainan: thread engine (muat file asli -> dunia -> render loop)
#pragma once
#include <android/native_window.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include "config.h"
#include "player.h"

namespace cs {

enum Button { BTN_JUMP = 1, BTN_DUCK = 2, BTN_WALK = 4 };

class Game {
public:
    static Game& instance();
    // Mengembalikan id sesi (>0) atau 0 jika gagal. Aman dipanggil dari thread mana pun.
    int start(const std::string& configText);
    void setWindow(ANativeWindow* w);
    void setInput(float moveX, float moveY, float lookRateX, float lookRateY, int buttons);
    void addLook(float dyaw, float dpitch);
    void command(const std::string& cmd);
    std::string status();
    void stop(int session); // hanya menghentikan sesi dengan id yang sama
    void setPaused(bool p) { paused_ = p; }

private:
    void threadMain();
    bool loadEverything(class Renderer& r, class Vfs& vfs, class Bsp& bsp, struct WorldMesh& wm, class Collision& col);
    void setStatus(const std::string& state, const std::string& msg, float progress = -1.f);
    bool saveState(const std::string& path);
    bool loadState(const std::string& path);

    std::mutex lifeMutex_;
    int session_ = 0;
    GraphicsConfig gcfg_;
    SessionConfig scfg_;
    std::thread thread_;
    std::atomic<bool> running_{false}, paused_{false};

    std::mutex winMutex_;
    std::condition_variable winCv_;
    ANativeWindow* pendingWindow_ = nullptr;
    ANativeWindow* engineWindow_ = nullptr; // window yang sedang dipakai swapchain
    bool windowChanged_ = false;
    bool windowAck_ = true;

    std::mutex inMutex_;
    float moveX_ = 0, moveY_ = 0, rateX_ = 0, rateY_ = 0, accYaw_ = 0, accPitch_ = 0;
    int buttons_ = 0;
    std::vector<std::string> commands_;

    std::mutex stMutex_;
    std::string state_ = "idle", message_, info_;
    float progress_ = 0;
    float fps_ = 0, frameMs_ = 0;
    Player player_;
    std::string mapName_;
};

} // namespace cs
