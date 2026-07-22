package com.dishii.zelda3;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;

/**
 * Hosts the MinimapView on the device's MAIN display when the game runs on the
 * secondary one (SecondScreenSwap = 1 in zelda3.ini). A Presentation can't be
 * used for this: the framework refuses to attach presentation windows to the
 * default display (InvalidDisplayException), so the swapped layout needs a real
 * activity. The window is non-focusable for the same reason the Presentation
 * is: gamepad input must stay with the game window even while this screen is
 * being touched.
 */
public class CompanionActivity extends Activity {

    private static final String TAG = "Zelda3SecondScreen";

    // The game activity starts/finishes us and the DUMP debug broadcast needs
    // the live view; both run in this process, so a static instance is enough.
    static volatile CompanionActivity instance;

    private MinimapView minimap;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            // Unlike the bottom-screen Presentation, this activity is focusable: it
            // owns the default display, and leaving that display with no focusable
            // window makes any input routed there ANR ("no focused window"). The
            // game runs on a separate display and keeps its own per-display focus,
            // so touching this screen doesn't steal the gamepad from the game.
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            getWindow().getDecorView().setBackgroundColor(Color.BLACK);
            getWindow().getDecorView().setSystemUiVisibility(
                    android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
            minimap = new MinimapView(this);
            setContentView(minimap);
        } catch (Throwable e) {
            // shares the game's process, so dying here would take the game
            // down with it; drop the companion screen instead (issue #19)
            Log.e(TAG, "Companion screen failed", e);
            CrashLog.report(this, "companion screen", e);
            finish();
            return;
        }
        instance = this;
        Log.i(TAG, "Companion activity up on display "
                + getWindowManager().getDefaultDisplay().getDisplayId());
    }

    @Override
    protected void onDestroy() {
        if (instance == this) {
            instance = null;
        }
        super.onDestroy();
    }

    /** Debug: render the current view into a PNG (triggered via adb broadcast). */
    void dumpToFile(File file) {
        if (minimap == null || minimap.getWidth() == 0) return;
        try {
            Bitmap b = Bitmap.createBitmap(minimap.getWidth(), minimap.getHeight(),
                    Bitmap.Config.ARGB_8888);
            minimap.draw(new Canvas(b));
            try (FileOutputStream fos = new FileOutputStream(file)) {
                b.compress(Bitmap.CompressFormat.PNG, 100, fos);
            }
            b.recycle();
            Log.i(TAG, "dumped " + file);
        } catch (Exception e) {
            Log.e(TAG, "dump failed", e);
        }
    }
}
