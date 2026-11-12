package io.github.edo9300.edopro;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class CrashReporter {
    private static final String TAG = "AniDimensionCrash";
    private static final String REPORT_URL = "http://66.92.168.136:8080/api/report_crash";
    private static boolean initialized = false;

    public static void init(final Context context) {
        if (initialized) return;
        initialized = true;

        final Thread.UncaughtExceptionHandler defaultHandler = Thread.getDefaultUncaughtExceptionHandler();

        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread thread, Throwable throwable) {
                try {
                    String report = buildCrashReport(thread, throwable);
                    Log.e(TAG, report);
                    
                    // 1. Guardar en almacenamiento interno
                    saveToFile(new File(context.getFilesDir(), "AniDimension_crash.txt"), report);

                    // 2. Guardar en la carpeta Download publica accesible por el usuario
                    try {
                        File downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
                        if (downloadDir != null) {
                            if (!downloadDir.exists()) downloadDir.mkdirs();
                            saveToFile(new File(downloadDir, "AniDimension_crash.txt"), report);
                        }
                    } catch (Throwable ignored) {}

                    // 3. Enviar reporte por HTTP POST sincrono con timeout corto
                    sendReportToServer(report);

                } catch (Throwable t) {
                    Log.e(TAG, "Error handling crash: " + t.getMessage());
                }

                if (defaultHandler != null) {
                    defaultHandler.uncaughtException(thread, throwable);
                } else {
                    System.exit(1);
                }
            }
        });
    }

    public static void report(Throwable throwable, String contextMessage) {
        try {
            String report = "=== MANUAL REPORT: " + contextMessage + " ===\n" + buildCrashReport(Thread.currentThread(), throwable);
            Log.e(TAG, report);
            sendReportToServerAsync(report);
        } catch (Throwable ignored) {}
    }

    private static String buildCrashReport(Thread thread, Throwable throwable) {
        StringWriter sw = new StringWriter();
        PrintWriter pw = new PrintWriter(sw);
        throwable.printStackTrace(pw);
        String stackTrace = sw.toString();

        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);
        String timestamp = sdf.format(new Date());

        StringBuilder sb = new StringBuilder();
        sb.append("================ ANI DIMENSION ANDROID CRASH ================\n");
        sb.append("Timestamp: ").append(timestamp).append("\n");
        sb.append("App Version Code: ").append(BuildConfig.VERSION_CODE).append("\n");
        sb.append("App Version Name: ").append(BuildConfig.VERSION_NAME).append("\n");
        sb.append("Device Manufacturer: ").append(Build.MANUFACTURER).append("\n");
        sb.append("Device Model: ").append(Build.MODEL).append("\n");
        sb.append("Android Release: ").append(Build.VERSION.RELEASE).append("\n");
        sb.append("SDK Version: ").append(Build.VERSION.SDK_INT).append("\n");
        sb.append("Thread: ").append(thread != null ? thread.getName() : "unknown").append("\n");
        sb.append("Exception: ").append(throwable.getClass().getName()).append(": ").append(throwable.getMessage()).append("\n");
        sb.append("Stack Trace:\n").append(stackTrace).append("\n");
        sb.append("=============================================================\n");
        return sb.toString();
    }

    private static void saveToFile(File file, String text) {
        try {
            File parent = file.getParentFile();
            if (parent != null && !parent.exists()) parent.mkdirs();
            FileOutputStream fos = new FileOutputStream(file, false);
            fos.write(text.getBytes(StandardCharsets.UTF_8));
            fos.flush();
            fos.close();
            Log.i(TAG, "Crash report saved to: " + file.getAbsolutePath());
        } catch (Throwable t) {
            Log.e(TAG, "Failed to write crash to file: " + t.getMessage());
        }
    }

    private static void sendReportToServer(String report) {
        try {
            URL url = new URL(REPORT_URL);
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("POST");
            conn.setConnectTimeout(4000);
            conn.setReadTimeout(4000);
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "text/plain; charset=utf-8");

            byte[] bytes = report.getBytes(StandardCharsets.UTF_8);
            conn.setFixedLengthStreamingMode(bytes.length);

            OutputStream os = conn.getOutputStream();
            os.write(bytes);
            os.flush();
            os.close();

            int code = conn.getResponseCode();
            Log.i(TAG, "Crash report HTTP status: " + code);
            conn.disconnect();
        } catch (Throwable t) {
            Log.w(TAG, "Failed to upload crash report: " + t.getMessage());
        }
    }

    private static void sendReportToServerAsync(final String report) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                sendReportToServer(report);
            }
        }).start();
    }
}

