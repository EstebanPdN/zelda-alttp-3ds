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

    // Text/font data pulled from a translated ROM the user picked, waiting for
    // the US ROM so the assets can be built with the translation on top.
    private TranslationExtractor.Language pendingTranslation;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        CrashLog.install(this);

        try {
            if (assetsReady()) {
                launchGame();
                return;
            }
            buildUi();
        } catch (Throwable e) {
            // issue #19: an exception here used to be an instant silent close.
            // Show the trace instead so it can end up in a bug report.
            Log.e(TAG, "Startup failed", e);
            CrashLog.report(this, "setup", e);
            showStartupError(e);
        }
    }

    /** Bare-bones error screen for a startup failure; kept free of anything
     *  that could itself fail (no custom assets, no second display). */
    private void showStartupError(Throwable e) {
        java.io.StringWriter trace = new java.io.StringWriter();
        e.printStackTrace(new java.io.PrintWriter(trace));

        TextView text = new TextView(this);
        text.setText("The app failed to start. Please report this at "
                + "github.com/samyost1/zelda3-android — a copy of the error was "
                + "saved to Android/data/com.dishii.zelda3/files/crash.txt\n\n" + trace);
        text.setTextColor(Color.WHITE);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        text.setTypeface(android.graphics.Typeface.MONOSPACE);
        int pad = dp(24);
        text.setPadding(pad, pad, pad, pad);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor("#101018"));
        scroll.addView(text);
        setContentView(scroll);
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
            try {
                startActivity(intent, options.toBundle());
                finish();
                return;
            } catch (RuntimeException e) {
                // some firmwares refuse app launches on the secondary display;
                // fall through to a normal launch rather than dying at startup
                Log.w(TAG, "Launch on display " + display + " refused", e);
            }
        }
        startActivity(intent);
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
                + "assets — the ROM itself is never copied or kept.\n\n"
                + "Playing a fan translation? Select the translated ROM first; "
                + "you'll be asked for the original US ROM afterwards.");
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
                String error = null, prompt = null;
                try {
                    prompt = extract(romUri);
                } catch (BpsPatcher.BpsException e) {
                    error = e.getMessage();
                } catch (TranslationExtractor.RomException e) {
                    error = e.getMessage();
                } catch (IOException e) {
                    Log.e(TAG, "ROM extraction failed", e);
                    error = "Couldn't read that file: " + e.getMessage();
                } catch (Exception e) {
                    Log.e(TAG, "ROM extraction failed", e);
                    error = "Something went wrong: " + e.getMessage();
                }
                final String resultError = error, resultPrompt = prompt;
                main.post(new Runnable() {
                    @Override
                    public void run() {
                        working = false;
                        spinner.setVisibility(View.GONE);
                        selectButton.setEnabled(true);
                        if (resultError != null) {
                            setStatus(resultError);
                        } else if (resultPrompt != null) {
                            setPrompt(resultPrompt);
                        } else {
                            launchGame();
                        }
                    }
                });
            }
        }, "rom-extract").start();
    }

    /**
     * Read the ROM, apply the bundled patch, and write out the assets file.
     * A ROM that isn't the US one is treated as a translation romhack: its
     * text is stashed and a prompt (the return value, null when the assets
     * were written) asks for the US ROM, which everything else is built from.
     */
    private String extract(Uri romUri)
            throws IOException, BpsPatcher.BpsException, TranslationExtractor.RomException {
        File dir = getExternalFilesDir(null);
        if (dir == null) {
            throw new IOException("external storage unavailable");
        }
        dir.mkdirs();

        byte[] rom = readUri(romUri);
        rom = stripCopierHeader(rom);

        byte[] bps = readAsset(BUNDLED_BPS);
        if (!BpsPatcher.matchesSource(rom, bps)) {
            pendingTranslation = TranslationExtractor.extract(rom);
            return "Found " + pendingTranslation.displayName + ".\n\n"
                    + "Now select your original (unpatched) US ROM so the rest "
                    + "of the game data can be built.";
        }
        byte[] dat = BpsPatcher.apply(rom, bps);
        if (pendingTranslation != null) {
            dat = TranslationExtractor.addLanguage(dat, pendingTranslation);
        }

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

        if (pendingTranslation != null) {
            // The assets are already on disk at this point, so a failure to
            // write the ini must not fail the whole setup - the game still
            // runs, just in English until Language is set by hand.
            try {
                setIniLanguage(dir, pendingTranslation.code);
            } catch (IOException e) {
                Log.e(TAG, "extracted the translation but couldn't set Language in zelda3.ini", e);
            }
            pendingTranslation = null;
        }
        return null;
    }

    /**
     * Point the game's `Language =` ini setting at the language that was just
     * added to the assets file. The ini normally gets created on first game
     * launch (see MainActivity), so seed it from the bundled default here.
     */
    private void setIniLanguage(File dir, String code) throws IOException {
        File ini = new File(dir, "zelda3.ini");
        String text;
        if (ini.isFile()) {
            byte[] raw = new byte[(int) ini.length()];
            java.io.FileInputStream fis = new java.io.FileInputStream(ini);
            try {
                new java.io.DataInputStream(fis).readFully(raw);
            } finally {
                fis.close();
            }
            text = new String(raw, "UTF-8");
        } else {
            text = new String(readAsset("zelda3.ini"), "UTF-8");
        }

        String line = "Language = " + code;
        String[] lines = text.split("\n", -1);
        boolean hasKey = false;
        for (String l : lines) {
            String t = l.trim();
            if (t.toLowerCase().startsWith("language")
                    && t.substring("language".length()).trim().startsWith("=")) {
                hasKey = true;
            }
        }
        StringBuilder sb = new StringBuilder(text.length() + 32);
        boolean done = false;
        for (String l : lines) {
            String t = l.trim();
            if (!done && hasKey && t.toLowerCase().startsWith("language")
                    && t.substring("language".length()).trim().startsWith("=")) {
                l = line;
                done = true;
            } else if (!done && !hasKey && t.equalsIgnoreCase("[General]")) {
                // No Language key yet: put one right below the section header.
                l = l + "\n" + line;
                done = true;
            }
            sb.append(l).append("\n");
        }
        sb.setLength(sb.length() - 1);  // undo the extra newline from the last split part
        if (!done) {
            sb.append("\n[General]\n").append(line).append("\n");
        }
        FileOutputStream fos = new FileOutputStream(ini);
        try {
            fos.write(sb.toString().getBytes("UTF-8"));
        } finally {
            fos.close();
        }
        Log.i(TAG, "Set " + line + " in " + ini);
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
        statusView.setTextColor(Color.parseColor("#FF8A80"));
        statusView.setText(text);
    }

    /** Like setStatus but for the next-step prompt, so it doesn't read as an error. */
    private void setPrompt(String text) {
        statusView.setTextColor(Color.parseColor("#B9F6CA"));
        statusView.setText(text);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
