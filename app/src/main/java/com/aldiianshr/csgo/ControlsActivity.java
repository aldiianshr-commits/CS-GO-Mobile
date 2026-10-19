package com.aldiianshr.csgo;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/** Pengaturan kontrol layar sentuh (sensitivitas, tata letak) dan pemetaan tombol gamepad. */
public class ControlsActivity extends Activity {
    private GameSettings s;
    private LinearLayout mapRows;
    private TextView padList;
    private AlertDialog waitDialog;
    private String waitingFor;
    private FrameLayout editor;
    private TouchControlsView editView;

    private static final String[][] ACTIONS = {
            {"keyJump", "Lompat"}, {"keyDuck", "Jongkok (toggle)"}, {"keyWalk", "Jalan pelan (toggle)"},
            {"keyNoclip", "Noclip"}, {"keyMenu", "Menu"}, {"keySave", "Simpan posisi"},
            {"keyLoad", "Muat posisi"}, {"keySpawn", "Spawn berikutnya"}};

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        s = new GameSettings(this);
        getWindow().setStatusBarColor(Ui.BG);
        build();
    }

    private void build() {
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Ui.BG);
        LinearLayout root = Ui.column(this);
        int p = Ui.dp(this, 20);
        root.setPadding(p, p, p, p);
        sv.addView(root);
        root.addView(Ui.text(this, "Kontrol", 26, Ui.ACCENT, true));

        LinearLayout c1 = Ui.card(this);
        c1.addView(Ui.title(this, "Layar sentuh"));
        Ui.sliderRow(this, c1, "Sensitivitas kamera", 0.2f, 3.0f, s.sensitivity, "%.2f", v -> { s.sensitivity = v; s.save(); });
        Ui.sliderRow(this, c1, "Opasitas tombol", 0.1f, 1.0f, s.touchOpacity, "%.2f", v -> { s.touchOpacity = v; s.save(); });
        Ui.sliderRow(this, c1, "Ukuran tombol", 0.6f, 1.6f, s.touchScale, "%.2f", v -> { s.touchScale = v; s.save(); });
        Ui.switchRow(this, c1, "Balik sumbu Y (invert)", s.invertY, v -> { s.invertY = v; s.save(); });
        LinearLayout r1 = Ui.row(this);
        r1.addView(Ui.button(this, "Atur tata letak tombol…", true, v -> openEditor()));
        r1.addView(Ui.button(this, "Reset kontrol", false, v -> {
            s.resetControls();
            s.save();
            recreate();
        }));
        c1.addView(r1);
        c1.addView(Ui.text(this, "Kiri: joystick mengambang untuk bergerak. Kanan: geser untuk melihat. Keyboard fisik: WASD, Spasi, Ctrl, Shift, N (noclip), F5/F9 (simpan/muat), Esc (menu).", 13, Ui.MUTED, false));
        root.addView(c1);

        LinearLayout c2 = Ui.card(this);
        c2.addView(Ui.title(this, "Gamepad"));
        padList = Ui.text(this, "", 13, Ui.MUTED, false);
        c2.addView(padList);
        c2.addView(Ui.text(this, "Stik kiri/D-pad: gerak • Stik kanan: kamera • L2: jongkok tahan • R2: lompat. Ketuk aksi di bawah untuk mengganti tombol.", 13, Ui.MUTED, false));
        mapRows = Ui.column(this);
        c2.addView(mapRows);
        c2.addView(Ui.button(this, "Pindai ulang gamepad", false, v -> refreshPads()));
        root.addView(c2);

        LinearLayout c3 = Ui.row(this);
        c3.addView(Ui.button(this, "Selesai", true, v -> finish()));
        root.addView(c3);

        FrameLayout frame = new FrameLayout(this);
        frame.addView(sv);
        editor = new FrameLayout(this);
        editor.setBackgroundColor(0xFF20262E);
        editor.setVisibility(View.GONE);
        editView = new TouchControlsView(this);
        editView.setEditMode(true);
        editor.addView(editView, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        LinearLayout eb = Ui.row(this);
        eb.addView(Ui.button(this, "Simpan tata letak", true, v -> {
            s.touchLayout = editView.serializeLayout();
            s.save();
            editor.setVisibility(View.GONE);
            Toast.makeText(this, "Tata letak disimpan", Toast.LENGTH_SHORT).show();
        }));
        eb.addView(Ui.button(this, "Kembalikan default", false, v -> editView.resetLayout()));
        eb.addView(Ui.button(this, "Batal", false, v -> editor.setVisibility(View.GONE)));
        editor.addView(eb, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL));
        frame.addView(editor, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setContentView(frame);
        refreshPads();
        refreshMappings();
    }

    private void openEditor() {
        editView.setAppearance(Math.max(0.6f, s.touchOpacity), s.touchScale);
        editView.loadLayout(s.touchLayout);
        editor.setVisibility(View.VISIBLE);
    }

    private void refreshPads() {
        StringBuilder b = new StringBuilder();
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null) continue;
            int src = d.getSources();
            if ((src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
                b.append("🎮 ").append(d.getName()).append('\n');
        }
        padList.setText(b.length() == 0 ? "Tidak ada gamepad terhubung (Bluetooth/USB)." : "Terdeteksi:\n" + b);
    }

    private int get(String key) {
        switch (key) {
            case "keyJump": return s.keyJump;
            case "keyDuck": return s.keyDuck;
            case "keyWalk": return s.keyWalk;
            case "keyNoclip": return s.keyNoclip;
            case "keyMenu": return s.keyMenu;
            case "keySave": return s.keySave;
            case "keyLoad": return s.keyLoad;
            default: return s.keySpawn;
        }
    }

    private void set(String key, int code) {
        switch (key) {
            case "keyJump": s.keyJump = code; break;
            case "keyDuck": s.keyDuck = code; break;
            case "keyWalk": s.keyWalk = code; break;
            case "keyNoclip": s.keyNoclip = code; break;
            case "keyMenu": s.keyMenu = code; break;
            case "keySave": s.keySave = code; break;
            case "keyLoad": s.keyLoad = code; break;
            default: s.keySpawn = code; break;
        }
        s.save();
    }

    private void refreshMappings() {
        mapRows.removeAllViews();
        for (String[] a : ACTIONS) {
            LinearLayout r = Ui.row(this);
            TextView l = Ui.text(this, a[1], 15, Ui.TEXT, false);
            r.addView(l, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            String keyName = KeyEvent.keyCodeToString(get(a[0])).replace("KEYCODE_", "").replace("BUTTON_", "");
            r.addView(Ui.button(this, keyName, false, v -> waitKey(a[0], a[1])));
            mapRows.addView(r);
        }
    }

    private void waitKey(String key, String label) {
        waitingFor = key;
        TextView t = Ui.text(this, "Tekan tombol gamepad untuk \"" + label + "\"…", 16, Color.WHITE, false);
        int p = Ui.dp(this, 20);
        t.setPadding(p, p, p, p);
        waitDialog = new AlertDialog.Builder(this).setView(t).setNegativeButton("Batal", (d, w) -> waitingFor = null)
                .setOnKeyListener((d, code, e) -> {
                    if (waitingFor == null || e.getAction() != KeyEvent.ACTION_DOWN) return false;
                    if (code == KeyEvent.KEYCODE_BACK && !KeyEvent.isGamepadButton(code) && (e.getSource() & InputDevice.SOURCE_GAMEPAD) == 0) return false;
                    set(waitingFor, code);
                    waitingFor = null;
                    d.dismiss();
                    refreshMappings();
                    return true;
                }).show();
    }
}
