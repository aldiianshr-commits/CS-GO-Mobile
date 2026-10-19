package com.aldiianshr.csgo;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Environment;

import java.io.File;
import java.util.Locale;

/** Semua pengaturan grafis, kontrol, dan folder game (disimpan di SharedPreferences). */
public final class GameSettings {
    public static final String DEFAULT_GAME_DIR = Environment.getExternalStorageDirectory().getAbsolutePath() + "/CSGO";

    // ---- nilai pilihan (urutan = indeks spinner)
    public static final int[] RESOLUTIONS = {480, 720, 900, 1080};
    public static final String[] RESOLUTION_NAMES = {"480p", "720p", "900p", "1080p"};
    public static final int[] RENDER_SCALES = {50, 75, 100, 125, 150};
    public static final String[] RENDER_SCALE_NAMES = {"50%", "75%", "100%", "125%", "150%"};
    public static final int[] FPS_LIMITS = {30, 45, 60};
    public static final String[] FPS_NAMES = {"30 FPS", "45 FPS", "60 FPS"};
    public static final String[] TEXTURE_NAMES = {"Rendah", "Sedang", "Tinggi"};
    public static final String[] SHADOW_NAMES = {"Mati", "Rendah", "Sedang", "Tinggi"};
    public static final String[] AA_NAMES = {"Mati", "FXAA", "MSAA (jika didukung)"};
    public static final int[] ANISO = {0, 2, 4, 8, 16};
    public static final String[] ANISO_NAMES = {"Mati", "2x", "4x", "8x", "16x"};
    public static final String[] ONOFF = {"Nonaktif", "Aktif"};
    public static final String[] DRIVER_NAMES = {"System Driver", "Turnip Driver (jika kompatibel)"};
    public static final String[] PERF_NAMES = {"Hemat Daya", "Seimbang", "Performa Tinggi"};
    public static final String[] ASPECT_NAMES = {"Otomatis", "16:9", "16:10"};
    public static final String[] PRESET_NAMES = {"Very Low", "Low", "Medium", "High", "Ultra", "Kustom"};

    // ---- grafis (indeks pilihan)
    public int resolution = 3, renderScale = 2, fps = 2, texture = 2, shadow = 2, aa = 1, aniso = 2;
    public boolean vsync = true, shaderCache = true, fullscreen = true, showFps = true;
    public int driver = 0, perf = 1, aspect = 0, preset = 2;

    // ---- kontrol
    public float sensitivity = 1.0f;
    public boolean invertY = false;
    public float touchOpacity = 0.55f, touchScale = 1.0f;
    public String touchLayout = "";
    public int keyJump, keyDuck, keyWalk, keyNoclip, keyMenu, keySave, keyLoad, keySpawn;

    // ---- folder & driver
    public String gameDir = DEFAULT_GAME_DIR;
    public String driverName = "";

    private final SharedPreferences sp;

    public GameSettings(Context c) {
        sp = c.getSharedPreferences("csgo", Context.MODE_PRIVATE);
        load();
    }

    /** Preset bawaan pertama kali: Medium (cocok untuk Xiaomi Pad 6 / Snapdragon 870). */
    public void applyPreset(int p) {
        preset = p;
        switch (p) {
            case 0: resolution = 0; renderScale = 0; fps = 0; texture = 0; shadow = 0; aa = 0; aniso = 0; break;
            case 1: resolution = 1; renderScale = 1; fps = 0; texture = 0; shadow = 1; aa = 0; aniso = 1; break;
            case 2: resolution = 1; renderScale = 2; fps = 2; texture = 1; shadow = 2; aa = 1; aniso = 2; break;
            case 3: resolution = 2; renderScale = 2; fps = 2; texture = 2; shadow = 3; aa = 1; aniso = 3; break;
            case 4: resolution = 3; renderScale = 3; fps = 2; texture = 2; shadow = 3; aa = 2; aniso = 4; break;
            default: break;
        }
    }

    public void resetDefaults() {
        applyPreset(2);
        vsync = true; shaderCache = true; fullscreen = true; showFps = true;
        driver = 0; perf = 1; aspect = 0;
    }

    public void resetControls() {
        sensitivity = 1.0f; invertY = false; touchOpacity = 0.55f; touchScale = 1.0f; touchLayout = "";
        keyJump = 96; keyDuck = 97; keyWalk = 106; keyNoclip = 100; keyMenu = 108; keySave = 103; keyLoad = 102; keySpawn = 99;
        // 96=A 97=B 106=L3 100=Y 108=START 103=R1 102=L1 99=X
    }

    public void load() {
        resetDefaults();
        resetControls();
        resolution = sp.getInt("resolution", resolution);
        renderScale = sp.getInt("renderScale", renderScale);
        fps = sp.getInt("fps", fps);
        texture = sp.getInt("texture", texture);
        shadow = sp.getInt("shadow", shadow);
        aa = sp.getInt("aa", aa);
        aniso = sp.getInt("aniso", aniso);
        vsync = sp.getBoolean("vsync", vsync);
        shaderCache = sp.getBoolean("shaderCache", shaderCache);
        fullscreen = sp.getBoolean("fullscreen", fullscreen);
        showFps = sp.getBoolean("showFps", showFps);
        driver = sp.getInt("driver", driver);
        perf = sp.getInt("perf", perf);
        aspect = sp.getInt("aspect", aspect);
        preset = sp.getInt("preset", preset);
        sensitivity = sp.getFloat("sensitivity", sensitivity);
        invertY = sp.getBoolean("invertY", invertY);
        touchOpacity = sp.getFloat("touchOpacity", touchOpacity);
        touchScale = sp.getFloat("touchScale", touchScale);
        touchLayout = sp.getString("touchLayout", touchLayout);
        keyJump = sp.getInt("keyJump", keyJump);
        keyDuck = sp.getInt("keyDuck", keyDuck);
        keyWalk = sp.getInt("keyWalk", keyWalk);
        keyNoclip = sp.getInt("keyNoclip", keyNoclip);
        keyMenu = sp.getInt("keyMenu", keyMenu);
        keySave = sp.getInt("keySave", keySave);
        keyLoad = sp.getInt("keyLoad", keyLoad);
        keySpawn = sp.getInt("keySpawn", keySpawn);
        gameDir = sp.getString("gameDir", gameDir);
        driverName = sp.getString("driverName", driverName);
    }

    public void save() {
        sp.edit()
                .putInt("resolution", resolution).putInt("renderScale", renderScale).putInt("fps", fps)
                .putInt("texture", texture).putInt("shadow", shadow).putInt("aa", aa).putInt("aniso", aniso)
                .putBoolean("vsync", vsync).putBoolean("shaderCache", shaderCache)
                .putBoolean("fullscreen", fullscreen).putBoolean("showFps", showFps)
                .putInt("driver", driver).putInt("perf", perf).putInt("aspect", aspect).putInt("preset", preset)
                .putFloat("sensitivity", sensitivity).putBoolean("invertY", invertY)
                .putFloat("touchOpacity", touchOpacity).putFloat("touchScale", touchScale)
                .putString("touchLayout", touchLayout)
                .putInt("keyJump", keyJump).putInt("keyDuck", keyDuck).putInt("keyWalk", keyWalk)
                .putInt("keyNoclip", keyNoclip).putInt("keyMenu", keyMenu).putInt("keySave", keySave)
                .putInt("keyLoad", keyLoad).putInt("keySpawn", keySpawn)
                .putString("gameDir", gameDir).putString("driverName", driverName)
                .apply();
    }

    public File savesDir() { return new File(gameDir, "saves"); }
    public File cacheDir() { return new File(gameDir, "cache"); }
    public File logFile() { return new File(gameDir, "logs/last.log"); }
    public File driversInbox() { return new File(gameDir, "drivers"); }

    public String toNativeConfig(Context c, String map) {
        File drv = DriverManager.activeDriverDir(c, this);
        StringBuilder b = new StringBuilder();
        kv(b, "resolution", RESOLUTIONS[resolution]);
        kv(b, "renderScale", RENDER_SCALES[renderScale]);
        kv(b, "fpsLimit", FPS_LIMITS[fps]);
        kv(b, "textureQuality", texture);
        kv(b, "shadowQuality", shadow);
        kv(b, "antiAliasing", aa);
        kv(b, "anisotropy", ANISO[aniso]);
        kv(b, "vsync", vsync ? 1 : 0);
        kv(b, "shaderCache", shaderCache ? 1 : 0);
        kv(b, "gpuDriver", driver);
        kv(b, "perfMode", perf);
        kv(b, "aspect", aspect);
        kv(b, "fullscreen", fullscreen ? 1 : 0);
        kv(b, "showFps", showFps ? 1 : 0);
        kv(b, "sensitivity", String.format(Locale.US, "%.3f", sensitivity));
        kv(b, "invertY", invertY ? 1 : 0);
        kv(b, "gameDir", gameDir);
        kv(b, "map", map);
        kv(b, "nativeLibDir", c.getApplicationInfo().nativeLibraryDir);
        kv(b, "filesDir", c.getFilesDir().getAbsolutePath());
        kv(b, "driverDir", drv != null ? drv.getAbsolutePath() : "");
        kv(b, "driverName", drv != null ? DriverManager.driverLibName(drv) : "");
        kv(b, "savePath", new File(savesDir(), map + ".sav").getAbsolutePath());
        kv(b, "cacheDir", cacheDir().getAbsolutePath());
        return b.toString();
    }

    private static void kv(StringBuilder b, String k, Object v) {
        b.append(k).append('=').append(v).append('\n');
    }
}
