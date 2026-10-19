package com.aldiianshr.csgo;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Kontrol layar sentuh: joystick mengambang di kiri, area geser kamera di kanan, dan tombol aksi.
 * Mode edit: tombol bisa digeser untuk mengatur tata letak (disimpan sebagai posisi relatif).
 */
public class TouchControlsView extends View {
    public interface Listener {
        void onMove(float x, float y, int buttons);
        void onLook(float dxDp, float dyDp);
        void onAction(String id);
    }

    static final class Btn {
        final String id, label;
        final boolean toggle, action, hold;
        float x, y; // posisi relatif 0..1
        final float defX, defY, size;
        boolean on;
        Btn(String id, String label, float x, float y, float size, boolean toggle, boolean action) {
            this.id = id; this.label = label; this.x = defX = x; this.y = defY = y; this.size = size;
            this.toggle = toggle; this.action = action; this.hold = !toggle && !action;
        }
    }

    private final List<Btn> buttons = new ArrayList<>();
    private final SparseArray<Object> pointers = new SparseArray<>(); // Btn, "joy", float[] (look)
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), stroke = new Paint(Paint.ANTI_ALIAS_FLAG), text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float density;
    private Listener listener;
    private boolean editMode;
    private float opacity = 0.55f, scale = 1.0f;
    private float joyCx, joyCy, joyX, joyY;
    private boolean joyActive;
    private Btn dragging;
    private float dragOffX, dragOffY;

    public TouchControlsView(Context c) {
        super(c);
        density = c.getResources().getDisplayMetrics().density;
        buttons.add(new Btn("jump", "LOMPAT", 0.90f, 0.70f, 42, false, false));
        buttons.add(new Btn("duck", "JONGKOK", 0.80f, 0.86f, 38, true, false));
        buttons.add(new Btn("walk", "JALAN", 0.93f, 0.46f, 32, true, false));
        buttons.add(new Btn("noclip", "NOCLIP", 0.70f, 0.08f, 28, false, true));
        buttons.add(new Btn("spawn", "SPAWN", 0.78f, 0.08f, 28, false, true));
        buttons.add(new Btn("save", "SIMPAN", 0.86f, 0.08f, 28, false, true));
        buttons.add(new Btn("load", "MUAT", 0.94f, 0.08f, 28, false, true));
        buttons.add(new Btn("menu", "MENU", 0.50f, 0.07f, 28, false, true));
        text.setColor(Color.WHITE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.DEFAULT_BOLD);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2 * density);
    }

    public void setListener(Listener l) { listener = l; }
    public void setEditMode(boolean e) { editMode = e; invalidate(); }
    public void setAppearance(float opacity, float scale) { this.opacity = opacity; this.scale = scale; invalidate(); }

    public String serializeLayout() {
        StringBuilder b = new StringBuilder();
        for (Btn bt : buttons) b.append(bt.id).append(':').append(String.format(Locale.US, "%.4f,%.4f", bt.x, bt.y)).append(';');
        return b.toString();
    }

    public void loadLayout(String s) {
        if (s == null) return;
        for (String part : s.split(";")) {
            int c = part.indexOf(':');
            if (c <= 0) continue;
            String id = part.substring(0, c);
            String[] xy = part.substring(c + 1).split(",");
            if (xy.length != 2) continue;
            for (Btn b : buttons)
                if (b.id.equals(id)) {
                    try {
                        b.x = clamp(Float.parseFloat(xy[0]));
                        b.y = clamp(Float.parseFloat(xy[1]));
                    } catch (NumberFormatException ignored) {
                    }
                }
        }
        invalidate();
    }

    public void resetLayout() {
        for (Btn b : buttons) { b.x = b.defX; b.y = b.defY; }
        invalidate();
    }

    public void setToggle(String id, boolean on) {
        for (Btn b : buttons) if (b.id.equals(id)) b.on = on;
        invalidate();
    }

    private static float clamp(float v) { return Math.max(0.02f, Math.min(0.98f, v)); }

    private float radius(Btn b) { return b.size * density * scale; }

    private Btn hit(float x, float y) {
        Btn best = null;
        float bd = Float.MAX_VALUE;
        for (Btn b : buttons) {
            float dx = x - b.x * getWidth(), dy = y - b.y * getHeight();
            float d = (float) Math.sqrt(dx * dx + dy * dy);
            if (d < radius(b) * 1.25f && d < bd) { bd = d; best = b; }
        }
        return best;
    }

    private int buttonMask() {
        int m = 0;
        for (Btn b : buttons) {
            if (!b.on) continue;
            if (b.id.equals("jump")) m |= NativeBridge.BTN_JUMP;
            if (b.id.equals("duck")) m |= NativeBridge.BTN_DUCK;
            if (b.id.equals("walk")) m |= NativeBridge.BTN_WALK;
        }
        return m;
    }

    private void emitMove() {
        if (listener == null) return;
        float r = 60 * density * scale;
        float mx = 0, my = 0;
        if (joyActive) {
            mx = (joyX - joyCx) / r;
            my = -(joyY - joyCy) / r;
            float len = (float) Math.sqrt(mx * mx + my * my);
            if (len > 1) { mx /= len; my /= len; }
        }
        listener.onMove(mx, my, buttonMask());
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int act = e.getActionMasked();
        int idx = e.getActionIndex();
        int pid = e.getPointerId(idx);
        if (editMode) return editTouch(e, act, idx);
        switch (act) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                float x = e.getX(idx), y = e.getY(idx);
                Btn b = hit(x, y);
                if (b != null) {
                    pointers.put(pid, b);
                    if (b.toggle) b.on = !b.on;
                    else if (b.hold) b.on = true;
                    else if (listener != null) { b.on = true; listener.onAction(b.id); }
                    emitMove();
                } else if (x < getWidth() * 0.45f && !joyActive) {
                    joyActive = true;
                    joyCx = joyX = x;
                    joyCy = joyY = y;
                    pointers.put(pid, "joy");
                    emitMove();
                } else {
                    pointers.put(pid, new float[]{x, y});
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < e.getPointerCount(); ++i) {
                    Object o = pointers.get(e.getPointerId(i));
                    float x = e.getX(i), y = e.getY(i);
                    if ("joy".equals(o)) {
                        joyX = x;
                        joyY = y;
                        float r = 60 * density * scale, dx = joyX - joyCx, dy = joyY - joyCy;
                        float len = (float) Math.sqrt(dx * dx + dy * dy);
                        if (len > r * 1.6f) { joyCx = joyX - dx / len * r * 1.6f; joyCy = joyY - dy / len * r * 1.6f; }
                        emitMove();
                    } else if (o instanceof float[]) {
                        float[] last = (float[]) o;
                        if (listener != null) listener.onLook((x - last[0]) / density, (y - last[1]) / density);
                        last[0] = x;
                        last[1] = y;
                    }
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                if (act == MotionEvent.ACTION_CANCEL) {
                    pointers.clear();
                    joyActive = false;
                    for (Btn b : buttons) if (!b.toggle) b.on = false;
                } else {
                    Object o = pointers.get(pid);
                    pointers.remove(pid);
                    if ("joy".equals(o)) joyActive = false;
                    else if (o instanceof Btn && !((Btn) o).toggle) ((Btn) o).on = false;
                }
                emitMove();
                invalidate();
                return true;
            }
        }
        return true;
    }

    private boolean editTouch(MotionEvent e, int act, int idx) {
        float x = e.getX(idx), y = e.getY(idx);
        if (act == MotionEvent.ACTION_DOWN) {
            dragging = hit(x, y);
            if (dragging != null) {
                dragOffX = dragging.x * getWidth() - x;
                dragOffY = dragging.y * getHeight() - y;
            }
        } else if (act == MotionEvent.ACTION_MOVE && dragging != null) {
            dragging.x = clamp((e.getX() + dragOffX) / getWidth());
            dragging.y = clamp((e.getY() + dragOffY) / getHeight());
            invalidate();
        } else if (act == MotionEvent.ACTION_UP || act == MotionEvent.ACTION_CANCEL) {
            dragging = null;
        }
        return true;
    }

    @Override
    protected void onDraw(Canvas c) {
        int a = Math.round(255 * opacity);
        if (editMode) {
            fill.setColor(Color.argb(40, 255, 255, 255));
            c.drawRect(0, 0, getWidth() * 0.45f, getHeight(), fill);
            text.setTextSize(14 * density);
            text.setColor(Color.argb(200, 255, 255, 255));
            c.drawText("Area joystick (kiri)", getWidth() * 0.225f, getHeight() * 0.5f, text);
            c.drawText("Area geser kamera (kanan) — seret tombol untuk memindahkan", getWidth() * 0.72f, getHeight() * 0.5f, text);
        }
        if (joyActive) {
            float r = 60 * density * scale;
            fill.setColor(Color.argb(a / 3, 255, 255, 255));
            c.drawCircle(joyCx, joyCy, r, fill);
            stroke.setColor(Color.argb(a, 255, 255, 255));
            c.drawCircle(joyCx, joyCy, r, stroke);
            fill.setColor(Color.argb(a, 240, 162, 27));
            float dx = joyX - joyCx, dy = joyY - joyCy, len = (float) Math.sqrt(dx * dx + dy * dy);
            if (len > r) { dx = dx / len * r; dy = dy / len * r; }
            c.drawCircle(joyCx + dx, joyCy + dy, r * 0.45f, fill);
        }
        for (Btn b : buttons) {
            float cx = b.x * getWidth(), cy = b.y * getHeight(), r = radius(b);
            fill.setColor(b.on ? Color.argb(a, 240, 162, 27) : Color.argb(a / 2, 20, 22, 26));
            c.drawCircle(cx, cy, r, fill);
            stroke.setColor(Color.argb(a, 255, 255, 255));
            c.drawCircle(cx, cy, r, stroke);
            text.setColor(Color.argb(Math.min(255, a + 60), 255, 255, 255));
            text.setTextSize(Math.max(9, r * 0.32f));
            c.drawText(b.label, cx, cy + text.getTextSize() * 0.35f, text);
        }
    }

    /** Setelah aksi sekali-tekan dilepas, matikan sorotan. */
    @Override
    public boolean performClick() { return super.performClick(); }
}
