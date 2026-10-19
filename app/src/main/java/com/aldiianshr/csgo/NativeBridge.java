package com.aldiianshr.csgo;

import android.view.Surface;

import java.util.HashMap;
import java.util.Map;

/** Antarmuka ke engine native (libcsgo.so). */
public final class NativeBridge {
    static {
        System.loadLibrary("csgo");
    }

    private NativeBridge() {}

    public static native String scan(String gameDir);
    public static native int start(String config);
    public static native void stop(int session);
    public static native void setSurface(Surface surface);
    public static native void setInput(float moveX, float moveY, float lookRateX, float lookRateY, int buttons);
    public static native void addLook(float dyaw, float dpitch);
    public static native void command(String cmd);
    public static native void setPaused(boolean paused);
    public static native String status();

    public static final int BTN_JUMP = 1, BTN_DUCK = 2, BTN_WALK = 4;

    /** Parse teks "kunci=nilai" per baris. */
    public static Map<String, String> parse(String text) {
        Map<String, String> m = new HashMap<>();
        if (text == null) return m;
        for (String line : text.split("\n")) {
            int i = line.indexOf('=');
            if (i > 0) m.put(line.substring(0, i), line.substring(i + 1));
        }
        return m;
    }
}
