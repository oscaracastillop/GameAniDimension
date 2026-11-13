package io.github.edo9300.edopro;

import android.content.Context;
import android.util.Log;

public class WindBotBridge {
    private static volatile boolean sInitialized = false;

    public static boolean isInitialized() {
        return sInitialized;
    }

    public static synchronized boolean init(Context context, String workingDirectory) {
        if (sInitialized) return true;
        try {
            if (context == null) {
                Log.w("WindBotBridge", "Cannot initialize WindBot without context");
                return false;
            }
            if (workingDirectory == null || workingDirectory.isEmpty()) {
                workingDirectory = context.getFilesDir().getAbsolutePath();
            }

            try {
                Class<?> arpClass = Class.forName("mono.embeddinator.AndroidRuntimeProvider");
                for (java.lang.reflect.Field f : arpClass.getDeclaredFields()) {
                    if (Context.class.isAssignableFrom(f.getType())) {
                        f.setAccessible(true);
                        f.set(null, context.getApplicationContext());
                        Log.i("WindBotBridge", "Set AndroidRuntimeProvider context via field: " + f.getName());
                    }
                }
            } catch (Throwable ignored) {
            }

            try {
                mono.embeddinator.Runtime.setImplementation(new mono.embeddinator.AndroidImpl(context.getApplicationContext()));
            } catch (Throwable t) {
                Log.w("WindBotBridge", "Runtime setImplementation warning: " + t.getMessage());
            }

            libwindbot.windbot.WindBot.initAndroid(workingDirectory + "/WindBot");
            sInitialized = true;
            Log.i("WindBotBridge", "WindBot initialized successfully via bridge!");
            return true;
        } catch (Throwable t) {
            Log.e("WindBotBridge", "WindBot initialization failed: " + t.getMessage(), t);
            sInitialized = false;
            return false;
        }
    }

    public static void run(String args) {
        try {
            libwindbot.windbot.WindBot.runAndroid(args);
        } catch (Throwable t) {
            Log.e("WindBotBridge", "WindBot run error: " + t.getMessage(), t);
        }
    }

    public static void addDb(String args) {
        try {
            libwindbot.windbot.WindBot.addDatabase(args);
        } catch (Throwable t) {
            Log.e("WindBotBridge", "WindBot addDatabase error: " + t.getMessage(), t);
        }
    }
}
