package io.github.edo9300.edopro;

import android.content.Context;
import android.util.Log;

public class WindBotBridge {
    private static boolean sInitialized = false;

    public static boolean init(Context context, String workingDirectory) {
        try {
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
        if (!sInitialized) {
            Log.w("WindBotBridge", "WindBot run requested but bridge is not initialized.");
            return;
        }
        try {
            libwindbot.windbot.WindBot.runAndroid(args);
        } catch (Throwable t) {
            Log.e("WindBotBridge", "WindBot run error: " + t.getMessage(), t);
        }
    }

    public static void addDb(String args) {
        if (!sInitialized) return;
        try {
            libwindbot.windbot.WindBot.addDatabase(args);
        } catch (Throwable t) {
            Log.e("WindBotBridge", "WindBot addDatabase error: " + t.getMessage(), t);
        }
    }
}
