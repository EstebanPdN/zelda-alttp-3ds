package com.dishii.zelda3;

import android.content.Context;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Date;

/**
 * Last-resort crash reporter. Any uncaught exception gets written to crash.txt
 * in the app's external files dir (Android/data/com.dishii.zelda3/files) so a
 * user hitting an instant close at launch has something to attach to a bug
 * report even without adb (issue #19), then control passes to the system
 * handler so the process still dies normally.
 */
public class CrashLog {

    private static final String TAG = "Zelda3Crash";
    private static boolean installed;

    static synchronized void install(Context context) {
        if (installed) {
            return;
        }
        installed = true;
        final Context app = context.getApplicationContext();
        final Thread.UncaughtExceptionHandler previous =
                Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread thread, Throwable e) {
                Log.e(TAG, "Uncaught exception in " + thread.getName(), e);
                report(app, thread.getName(), e);
                if (previous != null) {
                    previous.uncaughtException(thread, e);
                } else {
                    android.os.Process.killProcess(android.os.Process.myPid());
                }
            }
        });
    }

    /** Append-free dump of one throwable to crash.txt; safe to call anywhere. */
    static void report(Context context, String where, Throwable e) {
        try {
            File dir = context.getExternalFilesDir(null);
            if (dir == null) {
                dir = context.getFilesDir();
            }
            PrintWriter out = new PrintWriter(new FileWriter(new File(dir, "crash.txt")));
            try {
                out.println("zelda3 crash " + new Date() + " (" + where + ")");
                out.println("device: " + Build.MANUFACTURER + " " + Build.MODEL
                        + " (" + Build.DEVICE + "), Android " + Build.VERSION.RELEASE
                        + " (API " + Build.VERSION.SDK_INT + ")");
                try {
                    out.println("version: " + context.getPackageManager()
                            .getPackageInfo(context.getPackageName(), 0).versionName);
                } catch (Exception ignored) {
                }
                e.printStackTrace(out);
            } finally {
                out.close();
            }
        } catch (Exception ignored) {
            // nothing sensible left to do this late
        }
    }
}
