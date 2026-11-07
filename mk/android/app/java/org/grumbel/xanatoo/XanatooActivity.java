// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
package org.grumbel.xanatoo;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.util.Log;
import org.libsdl.app.SDLActivity;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

public class XanatooActivity extends SDLActivity {
    private static final String MANIFEST = "https://xanatoo.github.io/downloads/android-update.json";
    private static final String DOWNLOAD = "https://xanatoo.github.io/#download";
    private static final long INTERVAL = 24L * 60 * 60 * 1000;
    private static final int PERMISSION_REQUEST = 702;
    private String pendingVersion;
    private volatile boolean autoUpdates;
    private volatile int updateGeneration;

    // Called by the native preference system, including once after loading
    // view.conf. No network request is made until the user enables it.
    public void setAutoUpdates(boolean enabled) {
        autoUpdates = enabled;
        int generation = ++updateGeneration;
        if (!enabled) {
            runOnUiThread(() -> {
                pendingVersion = null;
                ((NotificationManager) getSystemService(NOTIFICATION_SERVICE)).cancel(1);
            });
            return;
        }
        long last = getPreferences(MODE_PRIVATE).getLong("update_check", 0);
        long now = System.currentTimeMillis();
        if (now >= last && now - last < INTERVAL) return;
        new Thread(() -> checkUpdate(generation)).start();
    }

    private void checkUpdate(int generation) {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(MANIFEST).openConnection();
            connection.setConnectTimeout(10000);
            connection.setReadTimeout(10000);
            connection.setUseCaches(false);
            connection.setRequestProperty("Cache-Control", "no-cache");
            if (connection.getResponseCode() != 200) return;
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (InputStream input = connection.getInputStream()) {
                byte[] buffer = new byte[1024];
                int n;
                while ((n = input.read(buffer)) != -1) {
                    bytes.write(buffer, 0, n);
                    if (bytes.size() > 8192) return;
                }
            }
            JSONObject manifest = new JSONObject(bytes.toString("UTF-8"));
            long code = manifest.getLong("versionCode");
            String version = manifest.getString("versionName");
            int installed = getPackageManager().getPackageInfo(getPackageName(), 0).versionCode;
            if (!autoUpdates || generation != updateGeneration) return;
            getPreferences(MODE_PRIVATE).edit().putLong("update_check", System.currentTimeMillis()).apply();
            if (code > installed && version.length() > 0 && version.length() <= 100) {
                runOnUiThread(() -> {
                    if (autoUpdates && generation == updateGeneration) offerUpdate(version);
                });
            }
        } catch (Exception e) {
            Log.w("xanatoo", "Update check failed", e);
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    private void offerUpdate(String version) {
        if (!autoUpdates || isFinishing()) return;
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            pendingVersion = version;
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, PERMISSION_REQUEST);
            return;
        }
        showUpdate(version);
    }

    @Override public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == PERMISSION_REQUEST && pendingVersion != null) {
            if (autoUpdates && results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) showUpdate(pendingVersion);
            pendingVersion = null;
        }
    }

    private void showUpdate(String version) {
        NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        String channel = "updates";
        if (Build.VERSION.SDK_INT >= 26)
            manager.createNotificationChannel(new NotificationChannel(channel, "Updates", NotificationManager.IMPORTANCE_DEFAULT));
        Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(DOWNLOAD));
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 23) flags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent action = PendingIntent.getActivity(this, 0, intent, flags);
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, channel) : new Notification.Builder(this);
        manager.notify(1, builder.setSmallIcon(android.R.drawable.stat_sys_download_done)
                .setContentTitle("xanatoo update available")
                .setContentText("Version " + version + " is available. Tap to download.")
                .setContentIntent(action).setAutoCancel(true).build());
    }
}
