package com.dishii.zelda3;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * Launcher activity. On first run the game assets ({@code zelda3_assets.dat})
 * aren't present, so we ask the user to point us at their Zelda 3 ROM, extract
 * the assets from it once (by applying the bundled {@code zelda3_assets.bps}
 * patch), and only then hand off to the SDL {@link MainActivity}. On every
 * later launch the assets already exist and we go straight into the game.
 */
public class SetupActivity extends Activity {

    private static final String TAG = "Zelda3Setup";
    private static final String ASSETS_DAT = "zelda3_assets.dat";
    private static final String BUNDLED_BPS = "zelda3_assets.bps";
    private static final int REQUEST_PICK_ROM = 1001;

    // A standard headered (.smc) dump is the unheadered ROM plus a 512-byte
    // copier header; strip it so the patch's CRC check lines up.
    private static final int ROM_SIZE = 1048576;
    private static final int COPIER_HEADER = 512;

    private final Handler main = new Handler(Looper.getMainLooper());

    private TextView statusView;
    private Button selectButton;
    private ProgressBar spinner;
    private boolean working;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (assetsReady()) {
            launchGame();
            return;
        }
        buildUi();
    }

    /** The game can boot once a non-empty assets file exists in the files dir. */
    private boolean assetsReady() {
        File dir = getExternalFilesDir(null);
        if (dir == null) {
            return false;
        }
        File dat = new File(dir, ASSETS_DAT);
        return dat.isFile() && dat.length() > 0;
    }

    private void launchGame() {
        Intent intent = new Intent(this, MainActivity.class);
        int display = swapDisplayId();
        if (display != -1 && Build.VERSION.SDK_INT >= 26) {
            android.app.ActivityOptions options = android.app.ActivityOptions.makeBasic();
            options.setLaunchDisplayId(display);
            startActivity(intent, options.toBundle());
        } else {
            startActivity(intent);
        }
        finish();
    }

    /**
     * With SecondScreenSwap = 1 in zelda3.ini the game runs on the secondary
     * display (e.g. the Thor's bottom screen) and the companion map takes the
     * main one. Returns the display to launch the game on, or -1 for the
     * default launch (flag off, no ini yet, or no second display attached).
     */
    private int swapDisplayId() {
        File dir = getExternalFilesDir(null);
        if (dir == null || !readIniBool(new File(dir, "zelda3.ini"), "[General]", "SecondScreenSwap")) {
            return -1;
        }
        android.hardware.display.DisplayManager dm =
                (android.hardware.display.DisplayManager) getSystemService(DISPLAY_SERVICE);
        for (android.view.Display d : dm.getDisplays()) {
            if (d.getDisplayId() != android.view.Display.DEFAULT_DISPLAY) {
                return d.getDisplayId();
            }
        }
        return -1;
    }

    /** Minimal `key = value` lookup inside one [section] of an ini file. */
    private static boolean readIniBool(File ini, String section, String key) {
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(ini))) {
            String line, cur = "";
            while ((line = in.readLine()) != null) {
                String t = line.trim();
                if (t.startsWith("[")) {
                    cur = t;
                } else if (cur.equalsIgnoreCase(section)
                        && t.toLowerCase().startsWith(key.toLowerCase())
                        && t.substring(key.length()).trim().startsWith("=")) {
                    String v = t.substring(t.indexOf('=') + 1).trim();
                    return v.equals("1") || v.equalsIgnoreCase("true");
                }
            }
        } catch (IOException ignored) {
        }
        return false;
    }

    // ---- UI (built in code to avoid pulling in layout/androidx deps) ----

    private void buildUi() {
        int pad = dp(24);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(Color.parseColor("#101018"));
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("Welcome to Zelda 3");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 26);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView blurb = new TextView(this);
        blurb.setText("This game needs assets from your own copy of "
                + "“The Legend of Zelda: A Link to the Past”.\n\n"
                + "Select your ROM file below. It's read once to build the game "
                + "assets — the ROM itself is never copied or kept.");
        blurb.setTextColor(Color.parseColor("#C8C8D0"));
        blurb.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        blurb.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams blurbLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        blurbLp.topMargin = dp(16);
        blurb.setLayoutParams(blurbLp);
        root.addView(blurb);

        selectButton = new Button(this);
        selectButton.setText("Select ROM");
        selectButton.setAllCaps(false);
        selectButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        LinearLayout.LayoutParams btnLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        btnLp.topMargin = dp(28);
        selectButton.setLayoutParams(btnLp);
        selectButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                pickRom();
            }
        });
        root.addView(selectButton);

        spinner = new ProgressBar(this);
        spinner.setVisibility(View.GONE);
        LinearLayout.LayoutParams spinLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        spinLp.topMargin = dp(20);
        spinner.setLayoutParams(spinLp);
        root.addView(spinner);

        statusView = new TextView(this);
        statusView.setTextColor(Color.parseColor("#FF8A80"));
        statusView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        statusView.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams statusLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        statusLp.topMargin = dp(20);
        statusView.setLayoutParams(statusLp);
        root.addView(statusView);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);
    }

    private void pickRom() {
        String action = Build.VERSION.SDK_INT >= 19
                ? Intent.ACTION_OPEN_DOCUMENT
                : Intent.ACTION_GET_CONTENT;
        Intent intent = new Intent(action);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, REQUEST_PICK_ROM);
        } catch (android.content.ActivityNotFoundException e) {
            setStatus("No file picker available on this device.");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_ROM || resultCode != RESULT_OK || data == null
                || data.getData() == null) {
            return;
        }
        extractInBackground(data.getData());
    }

    private void extractInBackground(final Uri romUri) {
        if (working) {
            return;
        }
        working = true;
        setStatus("");
        spinner.setVisibility(View.VISIBLE);
        selectButton.setEnabled(false);

        new Thread(new Runnable() {
            @Override
            public void run() {
                String error = null;
                try {
                    extract(romUri);
                } catch (BpsPatcher.BpsException e) {
                    error = e.getMessage();
                } catch (IOException e) {
                    Log.e(TAG, "ROM extraction failed", e);
                    error = "Couldn't read that file: " + e.getMessage();
                } catch (Exception e) {
                    Log.e(TAG, "ROM extraction failed", e);
                    error = "Something went wrong: " + e.getMessage();
                }
                final String result = error;
                main.post(new Runnable() {
                    @Override
                    public void run() {
                        working = false;
                        spinner.setVisibility(View.GONE);
                        selectButton.setEnabled(true);
                        if (result == null) {
                            launchGame();
                        } else {
                            setStatus(result);
                        }
                    }
                });
            }
        }, "rom-extract").start();
    }

    /** Read the ROM, apply the bundled patch, and write out the assets file. */
    private void extract(Uri romUri) throws IOException, BpsPatcher.BpsException {
        File dir = getExternalFilesDir(null);
        if (dir == null) {
            throw new IOException("external storage unavailable");
        }
        dir.mkdirs();

        byte[] rom = readUri(romUri);
        rom = stripCopierHeader(rom);

        byte[] bps = readAsset(BUNDLED_BPS);
        byte[] dat = BpsPatcher.apply(rom, bps);

        // Write to a temp file first so a crash mid-write can't leave a
        // truncated assets file that would look "ready" next launch.
        File out = new File(dir, ASSETS_DAT);
        File tmp = new File(dir, ASSETS_DAT + ".tmp");
        FileOutputStream fos = new FileOutputStream(tmp);
        try {
            fos.write(dat);
            fos.getFD().sync();
        } finally {
            fos.close();
        }
        if (!tmp.renameTo(out)) {
            // renameTo can fail if the target exists on some filesystems.
            out.delete();
            if (!tmp.renameTo(out)) {
                tmp.delete();
                throw new IOException("couldn't save the assets file");
            }
        }
        Log.i(TAG, "Wrote " + out + " (" + dat.length + " bytes)");
    }

    // Drop a 512-byte SNES copier header if the file carries one.
    private byte[] stripCopierHeader(byte[] rom) {
        if (rom.length == ROM_SIZE + COPIER_HEADER && (rom.length % 1024) == COPIER_HEADER) {
            byte[] trimmed = new byte[ROM_SIZE];
            System.arraycopy(rom, COPIER_HEADER, trimmed, 0, ROM_SIZE);
            return trimmed;
        }
        return rom;
    }

    private byte[] readUri(Uri uri) throws IOException {
        InputStream in = getContentResolver().openInputStream(uri);
        if (in == null) {
            throw new IOException("couldn't open the selected file");
        }
        try {
            return readFully(in);
        } finally {
            in.close();
        }
    }

    private byte[] readAsset(String name) throws IOException {
        InputStream in = getAssets().open(name);
        try {
            return readFully(in);
        } finally {
            in.close();
        }
    }

    private static byte[] readFully(InputStream in) throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream(1 << 20);
        byte[] buf = new byte[16 * 1024];
        int n;
        while ((n = in.read(buf)) != -1) {
            bos.write(buf, 0, n);
        }
        return bos.toByteArray();
    }

    private void setStatus(String text) {
        statusView.setText(text);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
