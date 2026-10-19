package com.aldiianshr.csgo;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;

/** Pembantu UI terprogram (tanpa XML) dengan gaya gelap konsisten. */
final class Ui {
    static final int BG = 0xFF111317, CARD = 0xFF1C2027, ACCENT = 0xFFF0A21B, TEXT = 0xFFE9ECEF, MUTED = 0xFF98A2AD, DANGER = 0xFFE5534B;

    private Ui() {}

    static int dp(Context c, float v) {
        return Math.round(v * c.getResources().getDisplayMetrics().density);
    }

    static LinearLayout column(Context c) {
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.VERTICAL);
        return l;
    }

    static LinearLayout row(Context c) {
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.HORIZONTAL);
        l.setGravity(Gravity.CENTER_VERTICAL);
        return l;
    }

    static LinearLayout card(Context c) {
        LinearLayout l = column(c);
        GradientDrawable g = new GradientDrawable();
        g.setColor(CARD);
        g.setCornerRadius(dp(c, 14));
        l.setBackground(g);
        int p = dp(c, 16);
        l.setPadding(p, p, p, p);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, 0, 0, dp(c, 12));
        l.setLayoutParams(lp);
        return l;
    }

    static TextView text(Context c, String s, float sp, int color, boolean bold) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    static TextView title(Context c, String s) {
        TextView t = text(c, s, 18, TEXT, true);
        t.setPadding(0, 0, 0, dp(c, 8));
        return t;
    }

    static Button button(Context c, String s, boolean primary, View.OnClickListener l) {
        Button b = new Button(c);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextColor(primary ? Color.BLACK : TEXT);
        GradientDrawable g = new GradientDrawable();
        g.setColor(primary ? ACCENT : 0xFF2B313A);
        g.setCornerRadius(dp(c, 10));
        b.setBackground(g);
        int p = dp(c, 12);
        b.setPadding(p, 0, p, 0);
        b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(c, 44));
        lp.setMargins(0, dp(c, 4), dp(c, 8), dp(c, 4));
        b.setLayoutParams(lp);
        return b;
    }

    interface IntCallback { void on(int v); }
    interface BoolCallback { void on(boolean v); }

    static Spinner spinnerRow(Context c, LinearLayout parent, String label, String[] items, int sel, IntCallback cb) {
        LinearLayout r = row(c);
        r.setPadding(0, dp(c, 6), 0, dp(c, 6));
        TextView t = text(c, label, 15, TEXT, false);
        r.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Spinner s = new Spinner(c, Spinner.MODE_DROPDOWN);
        ArrayAdapter<String> a = new ArrayAdapter<String>(c, android.R.layout.simple_spinner_item, items) {
            @Override
            public View getView(int position, View convertView, ViewGroup p) {
                TextView v = (TextView) super.getView(position, convertView, p);
                v.setTextColor(ACCENT);
                return v;
            }
        };
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        s.setAdapter(a);
        s.setSelection(Math.max(0, Math.min(items.length - 1, sel)), false);
        s.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> p, View v, int pos, long id) { cb.on(pos); }
            @Override
            public void onNothingSelected(AdapterView<?> p) {}
        });
        r.addView(s, new LinearLayout.LayoutParams(dp(c, 260), ViewGroup.LayoutParams.WRAP_CONTENT));
        parent.addView(r);
        return s;
    }

    static Switch switchRow(Context c, LinearLayout parent, String label, boolean on, BoolCallback cb) {
        Switch s = new Switch(c);
        s.setText(label);
        s.setTextColor(TEXT);
        s.setTextSize(15);
        s.setChecked(on);
        s.setPadding(0, dp(c, 8), 0, dp(c, 8));
        s.setOnCheckedChangeListener((b, v) -> cb.on(v));
        parent.addView(s);
        return s;
    }

    interface FloatCallback { void on(float v); }

    static SeekBar sliderRow(Context c, LinearLayout parent, String label, float min, float max, float value, String fmt, FloatCallback cb) {
        LinearLayout r = row(c);
        r.setPadding(0, dp(c, 4), 0, dp(c, 4));
        TextView t = text(c, label, 15, TEXT, false);
        r.addView(t, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        TextView val = text(c, String.format(java.util.Locale.US, fmt, value), 14, ACCENT, true);
        SeekBar sb = new SeekBar(c);
        sb.setMax(100);
        sb.setProgress(Math.round((value - min) / (max - min) * 100));
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar s, int p, boolean u) {
                float v = min + (max - min) * p / 100f;
                val.setText(String.format(java.util.Locale.US, fmt, v));
                cb.on(v);
            }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        });
        r.addView(sb, new LinearLayout.LayoutParams(dp(c, 220), ViewGroup.LayoutParams.WRAP_CONTENT));
        r.addView(val, new LinearLayout.LayoutParams(dp(c, 56), ViewGroup.LayoutParams.WRAP_CONTENT));
        parent.addView(r);
        return sb;
    }
}
