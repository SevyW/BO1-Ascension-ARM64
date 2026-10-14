package org.bo1port.zombies;

import android.content.Intent;
import android.os.Bundle;
import android.view.Display;
import android.view.WindowManager;

import java.io.File;
import java.io.RandomAccessFile;

import org.libsdl.app.SDLActivity;

/**
 * The app's only activity: SDL's, with the two native libraries it loads itself. libmain.so is the loader stub
 * (src/native/android/bo1_boot.c); the engine, libbo1.so, is not listed because the stub loads it at an address
 * below 4 GB, which System.loadLibrary cannot do.
 *
 * restartApp and shareLog are called by the engine (src/native/native_extras.cpp, through JNI).
 */
public class Bo1Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    /** The display's mode with the highest refresh rate at the current resolution (a 120 Hz panel: its 120 Hz mode). */
    private Display.Mode fastestMode() {
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode current = display.getMode();
        Display.Mode best = current;
        for (Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() == current.getPhysicalWidth() && mode.getPhysicalHeight() == current.getPhysicalHeight()
                    && mode.getRefreshRate() > best.getRefreshRate()) {
                best = mode;
            }
        }
        return best;
    }

    /**
     * Asks for that mode: without a request Android may run a game's window at 60 Hz on a faster panel.
     */
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            WindowManager.LayoutParams params = getWindow().getAttributes();
            params.preferredDisplayModeId = fastestMode().getModeId();
            getWindow().setAttributes(params);
        } catch (Throwable ignored) {
            // the system's choice stays
        }
    }

    /** For the loader stub: the refresh rate above, which a first start uses as the frame rate limit. */
    @Override
    protected String[] getArguments() {
        int refresh = 60;
        try {
            refresh = Math.round(fastestMode().getRefreshRate());
        } catch (Throwable ignored) {
            // 60
        }
        return new String[] { "--refresh=" + refresh, "--rates=" + supportedRates() };
    }

    /** The display's refresh rates at the current resolution, ascending: "60,90,120". */
    private String supportedRates() {
        java.util.TreeSet<Integer> rates = new java.util.TreeSet<Integer>();
        try {
            Display display = getWindowManager().getDefaultDisplay();
            Display.Mode current = display.getMode();
            for (Display.Mode mode : display.getSupportedModes()) {
                if (mode.getPhysicalWidth() == current.getPhysicalWidth() && mode.getPhysicalHeight() == current.getPhysicalHeight()) {
                    rates.add(Math.round(mode.getRefreshRate()));
                }
            }
        } catch (Throwable ignored) {
            // 60 below
        }
        if (rates.isEmpty()) {
            rates.add(60);
        }
        StringBuilder text = new StringBuilder();
        for (int rate : rates) {
            if (text.length() > 0) {
                text.append(',');
            }
            text.append(rate);
        }
        return text.toString();
    }

    /**
     * The engine's refresh rate setting (r_displayRefresh): asks for the display mode with that rate at the
     * current resolution, and tells the compositor the window's frame rate.
     */
    public void setRefreshRate(final String hz) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    int wanted = Integer.parseInt(hz);
                    Display display = getWindowManager().getDefaultDisplay();
                    Display.Mode current = display.getMode();
                    Display.Mode best = null;
                    for (Display.Mode mode : display.getSupportedModes()) {
                        if (mode.getPhysicalWidth() != current.getPhysicalWidth() || mode.getPhysicalHeight() != current.getPhysicalHeight()) {
                            continue;
                        }
                        if (best == null || Math.abs(mode.getRefreshRate() - wanted) < Math.abs(best.getRefreshRate() - wanted)) {
                            best = mode;
                        }
                    }
                    if (best != null) {
                        WindowManager.LayoutParams params = getWindow().getAttributes();
                        params.preferredDisplayModeId = best.getModeId();
                        params.preferredRefreshRate = best.getRefreshRate();
                        getWindow().setAttributes(params);
                    }
                } catch (Throwable ignored) {
                    // the current mode stays
                }
            }
        });
    }

    /**
     * Starts the app again in a new process: the engine switches between its Zombies and Multiplayer modes, or to
     * another mod, by starting over with other arguments (next_args.txt, read by the loader stub).
     */
    public void restartApp() {
        Intent intent = Intent.makeRestartActivityTask(getComponentName());
        startActivity(intent);
        Runtime.getRuntime().exit(0);
    }

    /**
     * The end of the log file (at most 120 000 bytes: an intent cannot carry much more) as text through the
     * system's share sheet.
     */
    public void shareLog(final String path) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                String text;
                try {
                    RandomAccessFile file = new RandomAccessFile(new File(path), "r");
                    long length = file.length();
                    int count = (int) Math.min(length, 120000L);
                    byte[] bytes = new byte[count];
                    file.seek(length - count);
                    file.readFully(bytes);
                    file.close();
                    text = new String(bytes, "UTF-8");
                } catch (Exception e) {
                    text = "The log could not be read: " + e;
                }
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType("text/plain");
                send.putExtra(Intent.EXTRA_SUBJECT, "Black Ops port log");
                send.putExtra(Intent.EXTRA_TEXT, text);
                startActivity(Intent.createChooser(send, "Send log"));
            }
        });
    }
}
