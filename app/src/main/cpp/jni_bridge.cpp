// Jembatan JNI antara UI Java dan engine native
#include <jni.h>
#include <android/native_window_jni.h>
#include <string>
#include "core/common.h"
#include "fs/vfs.h"
#include "game/game.h"

using namespace cs;

static std::string jstr(JNIEnv* env, jstring s) {
    if (!s) return "";
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

// NewStringUTF butuh "modified UTF-8" yang valid — byte non-ASCII diganti '?'
static jstring safeString(JNIEnv* env, const std::string& in) {
    std::string s(in);
    for (auto& c : s) if ((unsigned char)c >= 0x80 || c == 0) c = '?';
    return env->NewStringUTF(s.c_str());
}

static const char* kindName(ContainerKind k) {
    switch (k) {
        case ContainerKind::Folder: return "Folder";
        case ContainerKind::Stfs: return "STFS";
        case ContainerKind::Iso: return "ISO";
        case ContainerKind::Zip: return "ZIP";
        case ContainerKind::Vpk: return "VPK";
        default: return "Tidak didukung";
    }
}

extern "C" {

// Pindai folder game: mengembalikan teks berisi kontainer & daftar peta
JNIEXPORT jstring JNICALL Java_com_aldiianshr_csgo_NativeBridge_scan(JNIEnv* env, jclass, jstring dir) {
    std::string d = jstr(env, dir);
    Vfs vfs;
    vfs.mountGameDir(d);
    std::string out;
    for (auto& c : vfs.containers()) {
        out += "container=" + std::string(kindName(c.kind)) + "|" + (c.ok ? "ok" : "gagal") + "|" + std::to_string(c.files) +
               "|" + c.path + "|" + c.detail + "\n";
    }
    for (auto& m : vfs.listMaps()) out += "map=" + m + "\n";
    out += "files=" + std::to_string(vfs.fileCount()) + "\n";
    bool hasXex = vfs.exists("default.xex") || !vfs.listPrefix("default.xex").empty();
    out += "xex=" + std::string(hasXex ? "1" : "0") + "\n";
    return safeString(env, out);
}

JNIEXPORT jint JNICALL Java_com_aldiianshr_csgo_NativeBridge_start(JNIEnv* env, jclass, jstring cfg) {
    return Game::instance().start(jstr(env, cfg));
}

JNIEXPORT void JNICALL Java_com_aldiianshr_csgo_NativeBridge_stop(JNIEnv*, jclass, jint session) {
    Game::instance().stop(session);
}

JNIEXPORT void JNICALL Java_com_aldiianshr_csgo_NativeBridge_setSurface(JNIEnv* env, jclass, jobject surface) {
    ANativeWindow* w = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    Game::instance().setWindow(w);
}

JNIEXPORT void JNICALL Java_com_aldiianshr_csgo_NativeBridge_setInput(JNIEnv*, jclass, jfloat mx, jfloat my, jfloat rx, jfloat ry, jint buttons) {
    Game::instance().setInput(mx, my, rx, ry, buttons);
}

JNIEXPORT void JNICALL Java_com_aldiianshr_csgo_NativeBridge_addLook(JNIEnv*, jclass, jfloat dyaw, jfloat dpitch) {
    Game::instance().addLook(dyaw, dpitch);
}

JNIEXPORT void JNICALL Java_com_aldiianshr_csgo_NativeBridge_command(JNIEnv* env, jclass, jstring cmd) {
    Game::instance().command(jstr(env, cmd));
}

JNIEXPORT void JNICALL Java_com_aldiianshr_csgo_NativeBridge_setPaused(JNIEnv*, jclass, jboolean p) {
    Game::instance().setPaused(p);
}

JNIEXPORT jstring JNICALL Java_com_aldiianshr_csgo_NativeBridge_status(JNIEnv* env, jclass) {
    return safeString(env, Game::instance().status());
}

} // extern "C"
