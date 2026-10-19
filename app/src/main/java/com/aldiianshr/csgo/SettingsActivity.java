package com.aldiianshr.csgo;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

/** Pengaturan grafis lengkap + preset + reset ke default. */
public class SettingsActivity extends Activity {
    private GameSettings s;
    private boolean applying;
    private Spinner preset, res, scale, fps, tex, shadow, aa, aniso, driver, perf, aspect;
    private Switch vsync, cache, full, showFps;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        s = new GameSettings(this);
        getWindow().setStatusBarColor(Ui.BG);
        build();
    }

    private void build() {
        applying = true;
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Ui.BG);
        LinearLayout root = Ui.column(this);
        int p = Ui.dp(this, 20);
        root.setPadding(p, p, p, p);
        sv.addView(root);
        root.addView(Ui.text(this, "Pengaturan Grafis", 26, Ui.ACCENT, true));
        TextView sub = Ui.text(this, "Diterapkan saat peta dimuat. Perangkat: " + Build.MODEL +
                (Build.VERSION.SDK_INT >= 31 ? " (" + Build.SOC_MODEL + ")" : ""), 13, Ui.MUTED, false);
        sub.setPadding(0, 0, 0, Ui.dp(this, 12));
        root.addView(sub);

        LinearLayout c0 = Ui.card(this);
        c0.addView(Ui.title(this, "Preset"));
        preset = Ui.spinnerRow(this, c0, "Preset Grafis", GameSettings.PRESET_NAMES, s.preset, v -> {
            if (applying || v == 5 || v == s.preset) return;
            s.applyPreset(v);
            s.save();
            refreshAll();
        });
        root.addView(c0);

        LinearLayout c1 = Ui.card(this);
        c1.addView(Ui.title(this, "Tampilan"));
        res = Ui.spinnerRow(this, c1, "Resolusi", GameSettings.RESOLUTION_NAMES, s.resolution, v -> { if (v == s.resolution) return; s.resolution = v; custom(); });
        scale = Ui.spinnerRow(this, c1, "Render Scale", GameSettings.RENDER_SCALE_NAMES, s.renderScale, v -> { if (v == s.renderScale) return; s.renderScale = v; custom(); });
        aspect = Ui.spinnerRow(this, c1, "Aspect Ratio", GameSettings.ASPECT_NAMES, s.aspect, v -> { if (v == s.aspect) return; s.aspect = v; save(); });
        full = Ui.switchRow(this, c1, "Fullscreen (sembunyikan bar sistem)", s.fullscreen, v -> { if (v == s.fullscreen) return; s.fullscreen = v; save(); });
        root.addView(c1);

        LinearLayout c2 = Ui.card(this);
        c2.addView(Ui.title(this, "Kualitas"));
        tex = Ui.spinnerRow(this, c2, "Kualitas Tekstur", GameSettings.TEXTURE_NAMES, s.texture, v -> { if (v == s.texture) return; s.texture = v; custom(); });
        shadow = Ui.spinnerRow(this, c2, "Kualitas Bayangan (lightmap)", GameSettings.SHADOW_NAMES, s.shadow, v -> { if (v == s.shadow) return; s.shadow = v; custom(); });
        aa = Ui.spinnerRow(this, c2, "Anti-Aliasing", GameSettings.AA_NAMES, s.aa, v -> { if (v == s.aa) return; s.aa = v; custom(); });
        aniso = Ui.spinnerRow(this, c2, "Anisotropic Filtering", GameSettings.ANISO_NAMES, s.aniso, v -> { if (v == s.aniso) return; s.aniso = v; custom(); });
        root.addView(c2);

        LinearLayout c3 = Ui.card(this);
        c3.addView(Ui.title(this, "Performa"));
        fps = Ui.spinnerRow(this, c3, "FPS Limit", GameSettings.FPS_NAMES, s.fps, v -> { if (v == s.fps) return; s.fps = v; custom(); });
        vsync = Ui.switchRow(this, c3, "V-Sync", s.vsync, v -> { if (v == s.vsync) return; s.vsync = v; save(); });
        perf = Ui.spinnerRow(this, c3, "Mode Performa", GameSettings.PERF_NAMES, s.perf, v -> { if (v == s.perf) return; s.perf = v; save(); });
        cache = Ui.switchRow(this, c3, "Shader Cache (simpan pipeline ke /CSGO/cache)", s.shaderCache, v -> { if (v == s.shaderCache) return; s.shaderCache = v; save(); });
        driver = Ui.spinnerRow(this, c3, "GPU Driver", GameSettings.DRIVER_NAMES, s.driver, v -> {
            if (v == s.driver) return;
            s.driver = v;
            save();
            if (!applying && v == 1 && DriverManager.installed(this).isEmpty())
                Toast.makeText(this, "Belum ada driver Turnip — impor di menu Driver GPU. Tanpa itu, driver sistem dipakai.", Toast.LENGTH_LONG).show();
        });
        showFps = Ui.switchRow(this, c3, "Tampilkan FPS & info", s.showFps, v -> { if (v == s.showFps) return; s.showFps = v; save(); });
        root.addView(c3);

        LinearLayout c4 = Ui.card(this);
        c4.addView(Ui.title(this, "Keterangan"));
        c4.addView(Ui.text(this,
                "• Resolusi: ukuran buffer layar (diskalakan compositor ke layar penuh).\n" +
                "• Render Scale: resolusi render 3D relatif terhadap resolusi layar.\n" +
                "• Bayangan: CS:GO memakai bayangan statis (lightmap). Mati = tanpa lightmap, Rendah = filter nearest, Sedang = bilinear, Tinggi = bilinear + tonemapping.\n" +
                "• MSAA memakai 4x (atau 2x) bila didukung GPU, jika tidak otomatis mati.\n" +
                "• Mode Performa: Hemat Daya = inti CPU efisien + sustained mode; Performa Tinggi = inti CPU besar + ADPF + GPU turbo.\n" +
                "• Turnip: driver Vulkan Mesa untuk Adreno 6xx (Adreno 650 pada Snapdragon 870) dimuat lewat adrenotools.",
                13, Ui.MUTED, false));
        LinearLayout br = Ui.row(this);
        br.addView(Ui.button(this, "Reset ke Default", true, v -> {
            s.resetDefaults();
            s.save();
            refreshAll();
            Toast.makeText(this, "Pengaturan grafis dikembalikan ke default (Medium)", Toast.LENGTH_SHORT).show();
        }));
        br.addView(Ui.button(this, "Selesai", false, v -> finish()));
        c4.addView(br);
        root.addView(c4);
        setContentView(sv);
        sv.post(() -> applying = false);
    }

    private void custom() {
        if (applying) return;
        s.preset = 5;
        save();
        applying = true;
        preset.setSelection(5, false);
        preset.post(() -> applying = false);
    }

    private void save() {
        s.save();
    }

    private void refreshAll() {
        applying = true;
        preset.setSelection(s.preset, false);
        res.setSelection(s.resolution, false);
        scale.setSelection(s.renderScale, false);
        fps.setSelection(s.fps, false);
        tex.setSelection(s.texture, false);
        shadow.setSelection(s.shadow, false);
        aa.setSelection(s.aa, false);
        aniso.setSelection(s.aniso, false);
        driver.setSelection(s.driver, false);
        perf.setSelection(s.perf, false);
        aspect.setSelection(s.aspect, false);
        vsync.setChecked(s.vsync);
        cache.setChecked(s.shaderCache);
        full.setChecked(s.fullscreen);
        showFps.setChecked(s.showFps);
        preset.post(() -> applying = false);
    }
}
