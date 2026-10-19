package com.aldiianshr.csgo;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;

/** Memilih driver GPU: driver sistem Qualcomm atau Turnip (Mesa) dari zip adrenotools. */
public class DriverActivity extends Activity {
    private static final int REQ_ZIP = 21;
    private GameSettings s;
    private LinearLayout list;
    private TextView current;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        s = new GameSettings(this);
        getWindow().setStatusBarColor(Ui.BG);
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Ui.BG);
        LinearLayout root = Ui.column(this);
        int p = Ui.dp(this, 20);
        root.setPadding(p, p, p, p);
        sv.addView(root);
        root.addView(Ui.text(this, "Driver GPU", 26, Ui.ACCENT, true));
        root.addView(Ui.text(this, "Perangkat: " + Build.MANUFACTURER + " " + Build.MODEL + (Build.VERSION.SDK_INT >= 31 ? " • SoC " + Build.SOC_MODEL : "") +
                " • Android " + Build.VERSION.RELEASE, 13, Ui.MUTED, false));

        LinearLayout c = Ui.card(this);
        c.addView(Ui.title(this, "Driver aktif"));
        current = Ui.text(this, "", 15, Ui.TEXT, false);
        c.addView(current);
        LinearLayout r = Ui.row(this);
        r.addView(Ui.button(this, "Pakai driver sistem", false, v -> { s.driver = 0; s.save(); refresh(); }));
        r.addView(Ui.button(this, "Impor zip Turnip…", true, v -> pickZip()));
        r.addView(Ui.button(this, "Impor dari folder drivers", false, v -> importFromInbox()));
        c.addView(r);
        root.addView(c);

        LinearLayout c2 = Ui.card(this);
        c2.addView(Ui.title(this, "Driver terpasang"));
        list = Ui.column(this);
        c2.addView(list);
        root.addView(c2);

        LinearLayout c3 = Ui.card(this);
        c3.addView(Ui.title(this, "Petunjuk Turnip untuk Xiaomi Pad 6 (Adreno 650)"));
        c3.addView(Ui.text(this,
                "1. Unduh driver Turnip berformat adrenotools (zip berisi meta.json + libvulkan_freedreno.so), mis. rilis \"Turnip\" " +
                "dari repositori AdrenoToolsDrivers di GitHub. Adreno 650 (Snapdragon 870) didukung Turnip/Mesa.\n" +
                "2. Taruh zip di " + s.driversInbox() + " lalu tekan \"Impor dari folder drivers\", atau pilih zip langsung.\n" +
                "3. Pilih driver di daftar, lalu mulai peta. Bila Turnip gagal dimuat, engine otomatis kembali ke driver sistem dan alasannya tampil di HUD/log.\n" +
                "Cache shader dibuat ulang otomatis saat driver berganti.",
                13, Ui.MUTED, false));
        root.addView(c3);
        root.addView(Ui.button(this, "Selesai", true, v -> finish()));
        setContentView(sv);
        refresh();
    }

    private void refresh() {
        s.load();
        File active = DriverManager.activeDriverDir(this, s);
        current.setText(s.driver == 0 ? "System Driver (bawaan Qualcomm)"
                : active != null ? "Turnip: " + DriverManager.describe(active) : "Turnip dipilih tetapi belum ada driver — memakai driver sistem");
        list.removeAllViews();
        for (String name : DriverManager.installed(this)) {
            File d = new File(DriverManager.driversRoot(this), name);
            LinearLayout r = Ui.row(this);
            TextView t = Ui.text(this, DriverManager.describe(d) + "\n" + DriverManager.driverLibName(d), 14, Ui.TEXT, false);
            r.addView(t, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
            boolean sel = s.driver == 1 && name.equals(s.driverName);
            r.addView(Ui.button(this, sel ? "Aktif" : "Gunakan", !sel, v -> { s.driver = 1; s.driverName = name; s.save(); refresh(); }));
            r.addView(Ui.button(this, "Hapus", false, v -> {
                DriverManager.remove(this, name);
                if (name.equals(s.driverName)) { s.driverName = ""; s.driver = 0; s.save(); }
                refresh();
            }));
            list.addView(r);
        }
        if (list.getChildCount() == 0) list.addView(Ui.text(this, "Belum ada driver kustom.", 14, Ui.MUTED, false));
    }

    private void pickZip() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"application/zip", "application/x-zip-compressed", "application/octet-stream"});
        startActivityForResult(i, REQ_ZIP);
    }

    @Override
    protected void onActivityResult(int rc, int res, Intent data) {
        super.onActivityResult(rc, res, data);
        if (rc != REQ_ZIP || res != RESULT_OK || data == null || data.getData() == null) return;
        Uri u = data.getData();
        String name = u.getLastPathSegment() == null ? "turnip" : new File(u.getLastPathSegment().replace(':', '/')).getName();
        try (InputStream in = getContentResolver().openInputStream(u)) {
            install(in, name);
        } catch (Exception e) {
            error(e);
        }
    }

    private void importFromInbox() {
        File[] zips = s.driversInbox().listFiles((d, n) -> n.toLowerCase().endsWith(".zip"));
        if (zips == null || zips.length == 0) {
            Toast.makeText(this, "Tidak ada zip di " + s.driversInbox(), Toast.LENGTH_LONG).show();
            return;
        }
        String[] names = new String[zips.length];
        for (int i = 0; i < zips.length; ++i) names[i] = zips[i].getName();
        new AlertDialog.Builder(this).setTitle("Pilih zip driver").setItems(names, (d, w) -> {
            try (InputStream in = new FileInputStream(zips[w])) {
                install(in, zips[w].getName());
            } catch (Exception e) {
                error(e);
            }
        }).show();
    }

    private void install(InputStream in, String name) throws Exception {
        String dir = DriverManager.install(this, in, name);
        s.driver = 1;
        s.driverName = dir;
        s.save();
        refresh();
        Toast.makeText(this, "Driver dipasang dan diaktifkan: " + dir, Toast.LENGTH_LONG).show();
    }

    private void error(Exception e) {
        new AlertDialog.Builder(this).setTitle("Gagal mengimpor").setMessage(e.getMessage()).setPositiveButton("OK", null).show();
    }
}
