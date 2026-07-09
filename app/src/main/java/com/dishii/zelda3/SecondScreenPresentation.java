package com.dishii.zelda3;

import android.app.Presentation;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;

/** Presentation shown on the Ayn Thor's bottom screen, hosting the MinimapView. */
public class SecondScreenPresentation extends Presentation {

    private MinimapView minimap;

    public SecondScreenPresentation(Context outerContext, Display display) {
        super(outerContext, display);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (getWindow() != null) {
            getWindow().setBackgroundDrawable(new ColorDrawable(Color.BLACK));
            getWindow().setLayout(WindowManager.LayoutParams.MATCH_PARENT,
                    WindowManager.LayoutParams.MATCH_PARENT);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            // edge-to-edge: hide the nav/status bars of the secondary display
            getWindow().getDecorView().setSystemUiVisibility(
                    android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        minimap = new MinimapView(getContext());
        setContentView(minimap);
    }

    /** Debug: render the current view into a PNG (triggered via adb broadcast). */
    public void dumpToFile(File file) {
        if (minimap == null || minimap.getWidth() == 0) return;
        try {
            Bitmap b = Bitmap.createBitmap(minimap.getWidth(), minimap.getHeight(),
                    Bitmap.Config.ARGB_8888);
            minimap.draw(new Canvas(b));
            try (FileOutputStream fos = new FileOutputStream(file)) {
                b.compress(Bitmap.CompressFormat.PNG, 100, fos);
            }
            b.recycle();
            Log.i("Zelda3SecondScreen", "dumped " + file);
        } catch (Exception e) {
            Log.e("Zelda3SecondScreen", "dump failed", e);
        }
    }
}
