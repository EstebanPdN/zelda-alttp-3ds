
package com.dishii.zelda3;
import org.libsdl.app.SDLActivity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.view.Display;
import android.view.WindowManager;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.FileOutputStream;
import android.util.Log;

//This class is the main SDLActivity and just sets up a bunch of default files
public class MainActivity extends SDLActivity {

    private static final String TAG = "Zelda3SecondScreen";

    private SecondScreenPresentation secondScreen;
    private DisplayManager displayManager;

    private final DisplayManager.DisplayListener displayListener =
            new DisplayManager.DisplayListener() {
        @Override
        public void onDisplayAdded(int displayId) {
            showSecondScreenIfPresent();
        }

        @Override
        public void onDisplayRemoved(int displayId) {
            if (secondScreen != null && secondScreen.getDisplay().getDisplayId() == displayId) {
                dismissSecondScreen();
            }
        }

        @Override
        public void onDisplayChanged(int displayId) {}
    };

    // Debug: `adb shell am broadcast -a com.dishii.zelda3.DUMP` writes the
    // second screen's current frame to the app's external files dir.
    private final BroadcastReceiver dumpReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (secondScreen != null) {
                secondScreen.dumpToFile(new File(getExternalFilesDir(null), "second_screen.png"));
            }
            byte[] b = new byte[256];
            try {
                GameState.readSram(b);
                Log.i(TAG, String.format("pendants=0x%02x crystals=0x%02x", b[0x74], b[0x7A]));
            } catch (UnsatisfiedLinkError ignored) {}
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        displayManager = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        displayManager.registerDisplayListener(displayListener, null);
        showSecondScreenIfPresent();

        IntentFilter dumpFilter = new IntentFilter("com.dishii.zelda3.DUMP");
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(dumpReceiver, dumpFilter, 2 /* Context.RECEIVER_EXPORTED */);
        } else {
            registerReceiver(dumpReceiver, dumpFilter);
        }

        // Check if external storage is available
        if (isExternalStorageWritable()) {
            // Get the root directory of the external storage
            File externalDir = getExternalFilesDir(null);

            if (externalDir != null) {

                // Create a file object for the config file
                File configFile = new File(externalDir, "zelda3.ini");

                File datNotice = new File(externalDir, "PLACE zelda3_assets.dat HERE");

                File saves_folder = new File(externalDir+ File.separator + "saves");

                File saves_ref_folder = new File(saves_folder + File.separator + "ref");

                // Check if the folder doesn't exist, then create it
                saves_folder.mkdirs();

                saves_ref_folder.mkdirs();


                //copy reference saves and config to external data dir so user can change if needed.

                try {
                    AssetCopyUtil.copyAssetsToExternal(this, "saves/ref", getExternalFilesDir(null).getAbsolutePath() + "/saves/ref");
                    datNotice.createNewFile();
                    if (configFile.createNewFile()) {
                        InputStream inputStream;
                        try {
                            inputStream = getAssets().open("zelda3.ini");  // Replace with your actual asset file name
                        } catch (IOException e) {
                            e.printStackTrace();
                            return;
                        }
                        // Write configuration data to configFile
                        writeDataToFile(configFile,inputStream);
                    }
                } catch (IOException e) {
                    e.printStackTrace();
                }

            }
        }
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    // Show the companion Presentation on the first non-default display
    // (the Ayn Thor's bottom screen, or an emulator's simulated display).
    private void showSecondScreenIfPresent() {
        if (secondScreen != null) {
            return;
        }
        for (Display display : displayManager.getDisplays()) {
            if (display.getDisplayId() == Display.DEFAULT_DISPLAY) {
                continue;
            }
            try {
                secondScreen = new SecondScreenPresentation(this, display);
                secondScreen.show();
                Log.i(TAG, "Showing second screen on display " + display.getDisplayId()
                        + " (" + display.getName() + ")");
            } catch (WindowManager.InvalidDisplayException e) {
                Log.w(TAG, "Display " + display.getDisplayId() + " rejected Presentation", e);
                secondScreen = null;
                continue;
            }
            return;
        }
    }

    private void dismissSecondScreen() {
        if (secondScreen != null) {
            secondScreen.dismiss();
            secondScreen = null;
        }
    }

    @Override
    protected void onDestroy() {
        try {
            unregisterReceiver(dumpReceiver);
        } catch (IllegalArgumentException ignored) {}
        dismissSecondScreen();
        if (displayManager != null) {
            displayManager.unregisterDisplayListener(displayListener);
        }
        super.onDestroy();
    }

    private void writeDataToFile(File file,InputStream inputStream) {
        try {
            // Copy the content from the asset InputStream to the target file
            FileOutputStream outputStream = new FileOutputStream(file);
            byte[] buffer = new byte[1024];
            int length;
            while ((length = inputStream.read(buffer)) > 0) {
                outputStream.write(buffer, 0, length);
            }
            outputStream.close();
            inputStream.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
    // Check if external storage is available and writable
    private boolean isExternalStorageWritable() {
        String state = Environment.getExternalStorageState();
        return Environment.MEDIA_MOUNTED.equals(state);
    }
}
