package com.aldiianshr.csgo;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.List;

/** Peluncur: pemilihan folder game, pemindaian file Xbox 360, daftar peta, menu pengaturan. */
public class MainActivity extends Activity {
    private static final int REQ_TREE = 11, REQ_PERM = 12;
    private GameSettings s;
    private TextView folderText, permText, scanText;
    private LinearLayout mapList;
    private boolean scanning;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        s = new GameSettings(this);
        getWindow().setStatusBarColor(Ui.BG);
        getWindow().setNavigationBarColor(Ui.BG);

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Ui.BG);
        LinearLayout root = Ui.column(this);
        int p = Ui.dp(this, 20);
        root.setPadding(p, p, p, p);
        sv.addView(root);

        TextView h = Ui.text(this, "CS:GO Mobile", 30, Ui.ACCENT, true);
        root.addView(h);
        root.addView(Ui.text(this, "Engine native Android (Vulkan) yang membaca file asli Counter-Strike: Global Offensive Xbox 360 milik Anda. Tanpa emulator.", 14, Ui.MUTED, false));
        TextView ver = Ui.text(this, "Versi " + BuildConfigHelper.versionName(this) + " • " + Build.MANUFACTURER + " " + Build.MODEL, 12, Ui.MUTED, false);
        ver.setPadding(0, Ui.dp(this, 4), 0, Ui.dp(this, 16));
        root.addView(ver);

        // ---- folder game
        LinearLayout fc = Ui.card(this);
        fc.addView(Ui.title(this, "Folder game"));
        folderText = Ui.text(this, "", 15, Ui.TEXT, false);
        fc.addView(folderText);
        permText = Ui.text(this, "", 13, Ui.MUTED, false);
        permText.setPadding(0, Ui.dp(this, 4), 0, Ui.dp(this, 8));
        fc.addView(permText);
        LinearLayout br = Ui.row(this);
        br.addView(Ui.button(this, "Izinkan akses file", true, v -> requestStorage()));
        br.addView(Ui.button(this, "Pilih folder…", false, v -> pickFolder()));
        br.addView(Ui.button(this, "Pakai /CSGO", false, v -> { s.gameDir = GameSettings.DEFAULT_GAME_DIR; s.save(); refresh(); }));
        br.addView(Ui.button(this, "Pindai ulang", false, v -> scan()));
        fc.addView(br);
        TextView help = Ui.text(this,
                "Salin file CS:GO Xbox 360 Anda ke folder ini, salah satu dari:\n" +
                "• Paket XBLA asli (file STFS \"LIVE\", biasanya di Content/0000000000000000/5841125A/000D0000/)\n" +
                "• ISO Xbox 360 (XDVDFS)\n" +
                "• Folder hasil ekstrak (berisi csgo/maps, csgo/materials, *.360.zip / *.vpk)\n" +
                "Save: " + "<folder>/saves/   •   Driver Turnip: <folder>/drivers/   •   Log: <folder>/logs/last.log",
                13, Ui.MUTED, false);
        help.setPadding(0, Ui.dp(this, 8), 0, 0);
        fc.addView(help);
        root.addView(fc);

        // ---- hasil pindai & peta
        LinearLayout mc = Ui.card(this);
        mc.addView(Ui.title(this, "Peta"));
        scanText = Ui.text(this, "", 13, Ui.MUTED, false);
        mc.addView(scanText);
        mapList = Ui.column(this);
        mapList.setPadding(0, Ui.dp(this, 8), 0, 0);
        mc.addView(mapList);
        root.addView(mc);

        // ---- menu
        LinearLayout menu = Ui.card(this);
        menu.addView(Ui.title(this, "Pengaturan"));
        LinearLayout mr = Ui.row(this);
        mr.addView(Ui.button(this, "Grafis", false, v -> startActivity(new Intent(this, SettingsActivity.class))));
        mr.addView(Ui.button(this, "Kontrol & Gamepad", false, v -> startActivity(new Intent(this, ControlsActivity.class))));
        mr.addView(Ui.button(this, "Driver GPU (Turnip)", false, v -> startActivity(new Intent(this, DriverActivity.class))));
        mr.addView(Ui.button(this, "Log", false, v -> showLog()));
        menu.addView(mr);
        root.addView(menu);

        LinearLayout lim = Ui.card(this);
        lim.addView(Ui.title(this, "Yang sudah & belum didukung"));
        lim.addView(Ui.text(this,
                "Sudah: peta (BSP konsol big-endian + LZMA), tekstur VTFX/DXT, material VMT, lightmap, displacement, skybox, " +
                "gerak & kolisi FPS, kontrol sentuh/gamepad, save posisi, pengaturan grafis, driver Turnip.\n" +
                "Belum: logika permainan asli (senjata, bot, ronde, ekonomi), model/props (.mdl), karakter, efek partikel, audio, multiplayer. " +
                "Kode game asli (default.xex, PowerPC) tidak dijalankan — itu butuh emulator atau rekompilasi penuh.",
                13, Ui.MUTED, false));
        root.addView(lim);

        setContentView(sv);

        // Pintasan/otomasi: am start -n com.aldiianshr.csgo/.MainActivity --es autostart de_dust2
        handleAutostart(getIntent());
    }

    /** Otomasi/uji: --es autostart <peta> [--ei preset 0..4] */
    private void handleAutostart(Intent intent) {
        String auto = intent.getStringExtra("autostart");
        if (auto == null || auto.isEmpty() || !hasStorage()) return;
        int preset = intent.getIntExtra("preset", -1);
        if (preset >= 0 && preset <= 4) {
            s.applyPreset(preset);
            s.save();
        }
        launch(auto);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleAutostart(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        s.load();
        refresh();
    }

    private boolean hasStorage() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestStorage() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQ_PERM);
        }
    }

    @Override
    public void onRequestPermissionsResult(int rc, String[] p, int[] r) {
        refresh();
    }

    private void pickFolder() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        startActivityForResult(i, REQ_TREE);
    }

    @Override
    protected void onActivityResult(int rc, int res, Intent data) {
        super.onActivityResult(rc, res, data);
        if (rc == REQ_TREE && res == RESULT_OK && data != null && data.getData() != null) {
            String path = treeUriToPath(data.getData());
            if (path == null) {
                new AlertDialog.Builder(this).setMessage("Folder ini tidak bisa dipetakan ke path penyimpanan. Pilih folder di memori internal atau kartu SD.").setPositiveButton("OK", null).show();
                return;
            }
            s.gameDir = path;
            s.save();
            refresh();
        }
    }

    /** content://.../tree/primary:CSGO -> /storage/emulated/0/CSGO */
    static String treeUriToPath(Uri uri) {
        try {
            String id = DocumentsContract.getTreeDocumentId(uri);
            int c = id.indexOf(':');
            String vol = c >= 0 ? id.substring(0, c) : id;
            String rel = c >= 0 ? id.substring(c + 1) : "";
            String base = "primary".equalsIgnoreCase(vol) ? Environment.getExternalStorageDirectory().getAbsolutePath() : "/storage/" + vol;
            return rel.isEmpty() ? base : base + "/" + rel;
        } catch (Exception e) {
            return null;
        }
    }

    private void refresh() {
        folderText.setText(s.gameDir);
        boolean ok = hasStorage();
        if (ok) {
            File d = new File(s.gameDir);
            //noinspection ResultOfMethodCallIgnored
            d.mkdirs();
            //noinspection ResultOfMethodCallIgnored
            s.savesDir().mkdirs();
            //noinspection ResultOfMethodCallIgnored
            s.driversInbox().mkdirs();
            permText.setText("Akses file: diizinkan" + (d.isDirectory() ? "" : " — folder belum ada"));
            permText.setTextColor(Ui.MUTED);
            scan();
        } else {
            permText.setText("Akses file belum diizinkan. Tekan \"Izinkan akses file\" lalu aktifkan untuk CS:GO Mobile.");
            permText.setTextColor(Ui.DANGER);
            scanText.setText("");
            mapList.removeAllViews();
        }
    }

    private void scan() {
        if (scanning || !hasStorage()) return;
        scanning = true;
        scanText.setText("Memindai " + s.gameDir + " …");
        mapList.removeAllViews();
        final String dir = s.gameDir;
        new Thread(() -> {
            String res;
            try {
                res = NativeBridge.scan(dir);
            } catch (Throwable t) {
                res = "error=" + t;
            }
            final String r = res;
            runOnUiThread(() -> {
                scanning = false;
                showScan(r);
            });
        }, "scan").start();
    }

    private void showScan(String r) {
        StringBuilder sb = new StringBuilder();
        List<String> maps = new ArrayList<>();
        String files = "0";
        boolean xex = false;
        for (String line : r.split("\n")) {
            if (line.startsWith("container=")) {
                String[] f = line.substring(10).split("\\|", -1);
                if (f.length >= 5) {
                    sb.append("• ").append(f[0]).append(" (").append(f[1]).append(", ").append(f[2]).append(" file): ")
                            .append(new File(f[3]).getName()).append(f[4].isEmpty() ? "" : " — " + f[4]).append('\n');
                }
            } else if (line.startsWith("map=")) maps.add(line.substring(4));
            else if (line.startsWith("files=")) files = line.substring(6);
            else if (line.startsWith("xex=")) xex = line.endsWith("1");
            else if (line.startsWith("error=")) sb.append(line).append('\n');
        }
        sb.append("Total ").append(files).append(" file terindeks");
        if (xex) sb.append(" • default.xex terdeteksi (tidak dijalankan; engine native memakai aset)");
        if (maps.isEmpty()) sb.append("\nBelum ada peta (maps/*.bsp) yang ditemukan.");
        scanText.setText(sb.toString());
        mapList.removeAllViews();
        LinearLayout row = null;
        int i = 0;
        for (String m : maps) {
            if (i % 4 == 0) {
                row = Ui.row(this);
                mapList.addView(row);
            }
            Button b = Ui.button(this, m, m.startsWith("de_") || m.startsWith("cs_"), v -> launch(m));
            b.setLayoutParams(new LinearLayout.LayoutParams(0, Ui.dp(this, 48), 1));
            ((LinearLayout.LayoutParams) b.getLayoutParams()).setMargins(0, Ui.dp(this, 4), Ui.dp(this, 8), Ui.dp(this, 4));
            row.addView(b);
            i++;
        }
        if (row != null) while (i % 4 != 0) { row.addView(new TextView(this), new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1)); i++; }
    }

    private void launch(String map) {
        Intent i = new Intent(this, GameActivity.class);
        i.putExtra("map", map);
        startActivity(i);
    }

    private void showLog() {
        String text;
        try {
            text = DriverManager.readAll(new FileInputStream(s.logFile()));
            if (text.length() > 60000) text = text.substring(text.length() - 60000);
        } catch (Exception e) {
            text = "Belum ada log (" + s.logFile() + ")";
        }
        TextView t = Ui.text(this, text, 11, Ui.TEXT, false);
        t.setTextIsSelectable(true);
        int p = Ui.dp(this, 12);
        t.setPadding(p, p, p, p);
        ScrollView sv = new ScrollView(this);
        sv.addView(t);
        final String share = text;
        new AlertDialog.Builder(this).setTitle("Log engine").setView(sv)
                .setPositiveButton("Tutup", null)
                .setNeutralButton("Bagikan", (d, w) -> {
                    Intent si = new Intent(Intent.ACTION_SEND);
                    si.setType("text/plain");
                    si.putExtra(Intent.EXTRA_TEXT, share);
                    startActivity(Intent.createChooser(si, "Bagikan log"));
                }).show();
    }
}
