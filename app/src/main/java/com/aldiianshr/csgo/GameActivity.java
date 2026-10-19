package com.aldiianshr.csgo;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.util.Locale;
import java.util.Map;

/** Layar permainan: SurfaceView (Vulkan native) + kontrol sentuh + gamepad/keyboard + HUD. */
public class GameActivity extends Activity implements SurfaceHolder.Callback, TouchControlsView.Listener {
    private GameSettings s;
    private String map;
    private volatile int session;
    private SurfaceView surface;
    private TouchControlsView touch;
    private TextView hud, loadText, toast;
    private ProgressBar progress;
    private LinearLayout overlay;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean errorShown, menuOpen;
    private long toastUntil;

    // gabungan input sentuh + gamepad + keyboard
    private float touchX, touchY, padX, padY, padLookX, padLookY, keyX, keyY;
    private int touchButtons, padButtons, keyButtons;
    private boolean padDuckToggle, padWalkToggle;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        s = new GameSettings(this);
        map = getIntent().getStringExtra("map");
        if (map == null) map = "de_dust2";
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28)
            getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        if (s.perf == 0) {
            PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
            if (pm != null && pm.isSustainedPerformanceModeSupported()) getWindow().setSustainedPerformanceMode(true);
        }

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        surface = new SurfaceView(this);
        surface.getHolder().addCallback(this);
        applyResolution();
        root.addView(surface, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        touch = new TouchControlsView(this);
        touch.setListener(this);
        touch.setAppearance(s.touchOpacity, s.touchScale);
        touch.loadLayout(s.touchLayout);
        root.addView(touch, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        hud = Ui.text(this, "", 12, Color.WHITE, true);
        hud.setShadowLayer(3, 1, 1, Color.BLACK);
        int p = Ui.dp(this, 10);
        hud.setPadding(p, p, p, p);
        hud.setVisibility(s.showFps ? View.VISIBLE : View.GONE);
        root.addView(hud, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.START));

        toast = Ui.text(this, "", 14, Color.WHITE, true);
        toast.setShadowLayer(3, 1, 1, Color.BLACK);
        toast.setPadding(p, p, p, Ui.dp(this, 40));
        root.addView(toast, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL));

        overlay = Ui.column(this);
        overlay.setGravity(Gravity.CENTER);
        overlay.setBackgroundColor(0xF0111317);
        TextView t = Ui.text(this, map, 28, Ui.ACCENT, true);
        t.setGravity(Gravity.CENTER);
        overlay.addView(t);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        LinearLayout.LayoutParams pl = new LinearLayout.LayoutParams(Ui.dp(this, 420), Ui.dp(this, 24));
        pl.setMargins(0, Ui.dp(this, 16), 0, Ui.dp(this, 8));
        overlay.addView(progress, pl);
        loadText = Ui.text(this, "Menyiapkan…", 14, Ui.TEXT, false);
        loadText.setGravity(Gravity.CENTER);
        loadText.setPadding(Ui.dp(this, 40), 0, Ui.dp(this, 40), 0);
        overlay.addView(loadText);
        root.addView(overlay, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        setContentView(root);
        applyFullscreen();

        final String cfg = s.toNativeConfig(this, map);
        new Thread(() -> session = NativeBridge.start(cfg), "start-engine").start();
        handler.post(poll);
    }

    /** Resolusi = tinggi buffer layar; lebar mengikuti rasio layar fisik (landscape). */
    private void applyResolution() {
        int w, h;
        if (Build.VERSION.SDK_INT >= 30) {
            Rect r = getWindowManager().getCurrentWindowMetrics().getBounds();
            w = r.width(); h = r.height();
        } else {
            DisplayMetrics dm = new DisplayMetrics();
            getWindowManager().getDefaultDisplay().getRealMetrics(dm);
            w = dm.widthPixels; h = dm.heightPixels;
        }
        int lw = Math.max(w, h), lh = Math.min(w, h);
        int target = GameSettings.RESOLUTIONS[s.resolution];
        if (target >= lh) {
            surface.getHolder().setSizeFromLayout();
        } else {
            int tw = Math.round(target * (float) lw / lh) & ~1;
            surface.getHolder().setFixedSize(tw, target);
        }
    }

    private void applyFullscreen() {
        if (!s.fullscreen) return;
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean f) {
        super.onWindowFocusChanged(f);
        if (f) applyFullscreen();
    }

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            if (session > 0) updateStatus(NativeBridge.parse(NativeBridge.status()));
            handler.postDelayed(this, 250);
        }
    };

    private void updateStatus(Map<String, String> st) {
        String state = st.get("state");
        String msg = st.get("msg");
        if ("loading".equals(state) || "waiting".equals(state)) {
            overlay.setVisibility(View.VISIBLE);
            try { progress.setProgress(Math.round(Float.parseFloat(st.get("progress")) * 1000)); } catch (Exception ignored) {}
            loadText.setText(msg);
        } else if ("running".equals(state)) {
            overlay.setVisibility(View.GONE);
            if (msg != null && !msg.equals("Berjalan") && !msg.equals(lastMsg)) showToast(msg);
            lastMsg = msg;
        } else if ("error".equals(state) && !errorShown) {
            errorShown = true;
            overlay.setVisibility(View.VISIBLE);
            progress.setVisibility(View.GONE);
            loadText.setText("Galat: " + msg + "\n\nLog lengkap: " + s.logFile());
            loadText.setTextColor(Ui.DANGER);
            overlay.addView(Ui.button(this, "Kembali", true, v -> finish()));
        }
        if (s.showFps) {
            StringBuilder b = new StringBuilder();
            b.append(String.format(Locale.US, "FPS %s  |  %s ms", st.getOrDefault("fps", "0"), st.getOrDefault("frameMs", "0")));
            if (st.containsKey("render")) b.append("  |  3D ").append(st.get("render")).append(" → ").append(st.get("out"));
            b.append('\n').append(st.getOrDefault("gpu", "")).append("1".equals(st.get("turnip")) ? "  [Turnip]" : "");
            String te = st.get("turnipError");
            if (te != null && !te.isEmpty()) b.append("  (Turnip gagal: ").append(te).append(")");
            b.append('\n').append(map).append("  pos ").append(st.getOrDefault("pos", "-"))
                    .append("  spd ").append(st.getOrDefault("speed", "0"));
            if ("1".equals(st.get("noclip"))) b.append("  NOCLIP");
            hud.setText(b.toString());
        }
        if (toastUntil > 0 && System.currentTimeMillis() > toastUntil) {
            toast.setText("");
            toastUntil = 0;
        }
    }

    private String lastMsg;

    private void showToast(String m) {
        toast.setText(m);
        toastUntil = System.currentTimeMillis() + 2500;
    }

    // ------------------------------------------------------------ surface
    @Override
    public void surfaceCreated(SurfaceHolder h) {
        NativeBridge.setSurface(h.getSurface());
    }

    @Override
    public void surfaceChanged(SurfaceHolder h, int f, int w, int hh) {
        NativeBridge.setSurface(h.getSurface());
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder h) {
        NativeBridge.setSurface(null);
    }

    @Override
    protected void onPause() {
        super.onPause();
        NativeBridge.setPaused(true);
        resetInput();
    }

    @Override
    protected void onResume() {
        super.onResume();
        NativeBridge.setPaused(false);
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        final int sid = session;
        new Thread(() -> {
            int id = sid;
            // jika start belum selesai, tunggu id sesi
            for (int i = 0; i < 100 && id == 0; ++i) {
                try { Thread.sleep(50); } catch (InterruptedException ignored) {}
                id = session;
            }
            if (id > 0) NativeBridge.stop(id);
        }, "stop-engine").start();
        super.onDestroy();
    }

    // ------------------------------------------------------------ input
    private void push() {
        float mx = touchX + padX + keyX, my = touchY + padY + keyY;
        mx = Math.max(-1, Math.min(1, mx));
        my = Math.max(-1, Math.min(1, my));
        int bt = touchButtons | padButtons | keyButtons;
        if (padDuckToggle) bt |= NativeBridge.BTN_DUCK;
        if (padWalkToggle) bt |= NativeBridge.BTN_WALK;
        NativeBridge.setInput(mx, my, padLookX, padLookY, bt);
    }

    private void resetInput() {
        touchX = touchY = padX = padY = padLookX = padLookY = keyX = keyY = 0;
        padButtons = keyButtons = 0;
        push();
    }

    @Override
    public void onMove(float x, float y, int buttons) {
        touchX = x;
        touchY = y;
        touchButtons = buttons;
        push();
    }

    @Override
    public void onLook(float dxDp, float dyDp) {
        NativeBridge.addLook(dxDp * 0.15f, dyDp * 0.15f);
    }

    @Override
    public void onAction(String id) {
        switch (id) {
            case "menu": openMenu(); break;
            case "noclip": NativeBridge.command("noclip"); break;
            case "save": NativeBridge.command("save"); break;
            case "load": NativeBridge.command("load"); break;
            case "spawn": nextSpawn(); break;
        }
    }

    private int spawnIdx;

    private void nextSpawn() {
        NativeBridge.command("spawn:" + (++spawnIdx));
    }

    private void openMenu() {
        if (menuOpen) return;
        menuOpen = true;
        NativeBridge.setPaused(true);
        String[] items = {"Lanjutkan", "Simpan posisi (" + map + ".sav)", "Muat posisi", "Noclip on/off", "Pindah ke spawn berikutnya",
                "Kembali ke spawn", "Sembunyikan/tampilkan kontrol sentuh", "Keluar ke menu utama"};
        new AlertDialog.Builder(this).setTitle("CS:GO Mobile — " + map).setItems(items, (d, w) -> {
            switch (w) {
                case 1: NativeBridge.command("save"); break;
                case 2: NativeBridge.command("load"); break;
                case 3: NativeBridge.command("noclip"); break;
                case 4: nextSpawn(); break;
                case 5: NativeBridge.command("respawn"); break;
                case 6: touch.setVisibility(touch.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE); break;
                case 7: finish(); break;
                default: break;
            }
        }).setOnDismissListener(d -> {
            menuOpen = false;
            NativeBridge.setPaused(false);
            applyFullscreen();
        }).show();
    }

    private static float dz(float v) {
        return Math.abs(v) < 0.15f ? 0 : (v - Math.signum(v) * 0.15f) / 0.85f;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK && e.getAction() == MotionEvent.ACTION_MOVE) {
            float hx = e.getAxisValue(MotionEvent.AXIS_HAT_X), hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
            padX = dz(e.getAxisValue(MotionEvent.AXIS_X)) + hx;
            padY = -dz(e.getAxisValue(MotionEvent.AXIS_Y)) - hy;
            padLookX = dz(e.getAxisValue(MotionEvent.AXIS_Z));
            padLookY = dz(e.getAxisValue(MotionEvent.AXIS_RZ));
            float lt = Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE));
            float rt = Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS));
            padButtons = (padButtons & ~(NativeBridge.BTN_DUCK | NativeBridge.BTN_JUMP));
            if (lt > 0.5f) padButtons |= NativeBridge.BTN_DUCK;
            if (rt > 0.5f) padButtons |= NativeBridge.BTN_JUMP;
            push();
            return true;
        }
        return super.onGenericMotionEvent(e);
    }

    private boolean isGamepad(KeyEvent e) {
        int src = e.getSource();
        return (src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent e) {
        if (e.getRepeatCount() > 0 && code != KeyEvent.KEYCODE_BACK) return true;
        if (isGamepad(e) || KeyEvent.isGamepadButton(code)) {
            if (code == s.keyJump) { padButtons |= NativeBridge.BTN_JUMP; push(); return true; }
            if (code == s.keyDuck) { padDuckToggle = !padDuckToggle; push(); return true; }
            if (code == s.keyWalk) { padWalkToggle = !padWalkToggle; push(); return true; }
            if (code == s.keyNoclip) { NativeBridge.command("noclip"); return true; }
            if (code == s.keyMenu) { openMenu(); return true; }
            if (code == s.keySave) { NativeBridge.command("save"); return true; }
            if (code == s.keyLoad) { NativeBridge.command("load"); return true; }
            if (code == s.keySpawn) { nextSpawn(); return true; }
        }
        switch (code) { // keyboard fisik
            case KeyEvent.KEYCODE_W: keyY = 1; push(); return true;
            case KeyEvent.KEYCODE_S: keyY = -1; push(); return true;
            case KeyEvent.KEYCODE_A: keyX = -1; push(); return true;
            case KeyEvent.KEYCODE_D: keyX = 1; push(); return true;
            case KeyEvent.KEYCODE_SPACE: keyButtons |= NativeBridge.BTN_JUMP; push(); return true;
            case KeyEvent.KEYCODE_CTRL_LEFT: keyButtons |= NativeBridge.BTN_DUCK; push(); return true;
            case KeyEvent.KEYCODE_SHIFT_LEFT: keyButtons |= NativeBridge.BTN_WALK; push(); return true;
            case KeyEvent.KEYCODE_N: NativeBridge.command("noclip"); return true;
            case KeyEvent.KEYCODE_F5: NativeBridge.command("save"); return true;
            case KeyEvent.KEYCODE_F9: NativeBridge.command("load"); return true;
            case KeyEvent.KEYCODE_ESCAPE: openMenu(); return true;
            case KeyEvent.KEYCODE_DPAD_LEFT: keyX = -1; push(); return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT: keyX = 1; push(); return true;
            case KeyEvent.KEYCODE_DPAD_UP: keyY = 1; push(); return true;
            case KeyEvent.KEYCODE_DPAD_DOWN: keyY = -1; push(); return true;
            case KeyEvent.KEYCODE_BACK: openMenu(); return true;
        }
        return super.onKeyDown(code, e);
    }

    @Override
    public boolean onKeyUp(int code, KeyEvent e) {
        if (isGamepad(e) || KeyEvent.isGamepadButton(code)) {
            if (code == s.keyJump) { padButtons &= ~NativeBridge.BTN_JUMP; push(); return true; }
        }
        switch (code) {
            case KeyEvent.KEYCODE_W: case KeyEvent.KEYCODE_S: case KeyEvent.KEYCODE_DPAD_UP: case KeyEvent.KEYCODE_DPAD_DOWN:
                keyY = 0; push(); return true;
            case KeyEvent.KEYCODE_A: case KeyEvent.KEYCODE_D: case KeyEvent.KEYCODE_DPAD_LEFT: case KeyEvent.KEYCODE_DPAD_RIGHT:
                keyX = 0; push(); return true;
            case KeyEvent.KEYCODE_SPACE: keyButtons &= ~NativeBridge.BTN_JUMP; push(); return true;
            case KeyEvent.KEYCODE_CTRL_LEFT: keyButtons &= ~NativeBridge.BTN_DUCK; push(); return true;
            case KeyEvent.KEYCODE_SHIFT_LEFT: keyButtons &= ~NativeBridge.BTN_WALK; push(); return true;
        }
        return super.onKeyUp(code, e);
    }

    @Override
    public void onBackPressed() {
        openMenu();
    }
}
