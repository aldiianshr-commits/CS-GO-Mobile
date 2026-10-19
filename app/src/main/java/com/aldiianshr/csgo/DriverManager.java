package com.aldiianshr.csgo;

import android.content.Context;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Mengelola driver GPU kustom (Turnip/Mesa untuk Adreno) dalam format zip adrenotools
 * (berisi meta.json + libvulkan_freedreno.so). Driver diekstrak ke folder privat aplikasi
 * karena dlopen tidak boleh memuat .so langsung dari penyimpanan bersama.
 */
public final class DriverManager {
    private DriverManager() {}

    public static File driversRoot(Context c) {
        File d = new File(c.getFilesDir(), "drivers");
        d.mkdirs();
        return d;
    }

    public static List<String> installed(Context c) {
        List<String> out = new ArrayList<>();
        File[] fs = driversRoot(c).listFiles();
        if (fs != null) for (File f : fs) if (f.isDirectory() && driverLibName(f) != null) out.add(f.getName());
        return out;
    }

    public static File activeDriverDir(Context c, GameSettings s) {
        if (s.driver != 1 || s.driverName == null || s.driverName.isEmpty()) return null;
        File d = new File(driversRoot(c), s.driverName);
        return driverLibName(d) != null ? d : null;
    }

    /** Nama file .so driver di folder (dari meta.json atau .so pertama). */
    public static String driverLibName(File dir) {
        try {
            File meta = new File(dir, "meta.json");
            if (meta.exists()) {
                JSONObject o = new JSONObject(readAll(new FileInputStream(meta)));
                String lib = o.optString("libraryName", "");
                if (!lib.isEmpty() && new File(dir, lib).exists()) return lib;
            }
        } catch (Exception ignored) {
        }
        File[] fs = dir.listFiles();
        if (fs != null) for (File f : fs) if (f.getName().endsWith(".so")) return f.getName();
        return null;
    }

    public static String describe(File dir) {
        try {
            File meta = new File(dir, "meta.json");
            if (meta.exists()) {
                JSONObject o = new JSONObject(readAll(new FileInputStream(meta)));
                return o.optString("name", dir.getName()) + " " + o.optString("driverVersion", "") +
                        (o.has("vendor") ? " (" + o.optString("vendor") + ")" : "");
            }
        } catch (Exception ignored) {
        }
        return dir.getName();
    }

    /** Ekstrak zip driver; mengembalikan nama folder driver atau melempar pesan galat. */
    public static String install(Context c, InputStream zip, String suggestedName) throws Exception {
        String name = suggestedName.replaceAll("\\.zip$", "").replaceAll("[^A-Za-z0-9._-]", "_");
        if (name.isEmpty()) name = "turnip";
        File dir = new File(driversRoot(c), name);
        deleteRec(dir);
        dir.mkdirs();
        boolean anySo = false;
        try (ZipInputStream zis = new ZipInputStream(zip)) {
            ZipEntry e;
            byte[] buf = new byte[1 << 16];
            while ((e = zis.getNextEntry()) != null) {
                if (e.isDirectory()) continue;
                String fn = new File(e.getName()).getName();
                if (!(fn.endsWith(".so") || fn.equals("meta.json"))) continue;
                File out = new File(dir, fn);
                try (OutputStream os = new FileOutputStream(out)) {
                    int n;
                    while ((n = zis.read(buf)) > 0) os.write(buf, 0, n);
                }
                if (fn.endsWith(".so")) {
                    anySo = true;
                    out.setReadable(true, true);
                    out.setExecutable(true, true);
                }
            }
        }
        if (!anySo) {
            deleteRec(dir);
            throw new Exception("Zip tidak berisi file driver .so (format adrenotools: meta.json + libvulkan_freedreno.so)");
        }
        return name;
    }

    public static void remove(Context c, String name) {
        deleteRec(new File(driversRoot(c), name));
    }

    static void deleteRec(File f) {
        if (f.isDirectory()) {
            File[] fs = f.listFiles();
            if (fs != null) for (File x : fs) deleteRec(x);
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }

    static String readAll(InputStream in) throws Exception {
        try (InputStream is = in) {
            byte[] b = new byte[8192];
            StringBuilder sb = new StringBuilder();
            int n;
            while ((n = is.read(b)) > 0) sb.append(new String(b, 0, n, "UTF-8"));
            return sb.toString();
        }
    }
}
