package com.eventsdk.demo;

import android.os.Bundle;
import android.os.StrictMode;
import android.util.Log;
import android.widget.Button;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.eventsdk.EventSDK;
import com.eventsdk.EventSDKConfig;

import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

public class MainActivity extends AppCompatActivity {

    private static final String TAG = "EventSDKDemo";

    private TextView  logView;
    private ScrollView scrollView;
    private int eventCount = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        logView    = findViewById(R.id.logView);
        scrollView = findViewById(R.id.scrollView);

        // Demo only: allow network on main thread to keep the code readable.
        // Production: use WorkManager / ExecutorService for flush().
        StrictMode.setThreadPolicy(
            new StrictMode.ThreadPolicy.Builder().permitAll().build()
        );

        initSdk();
        wireButtons();
    }

    private void initSdk() {
        EventSDKConfig config = new EventSDKConfig(
            "https://httpbin.org/post",          // public echo endpoint
            getFilesDir().getAbsolutePath()
        );
        config.batchSize        = 3;
        config.maxQueueCapacity = 100;

        EventSDK.init(config);

        EventSDK.setTransport((url, body) -> {
            try {
                HttpURLConnection conn = (HttpURLConnection) new URL(url).openConnection();
                conn.setRequestMethod("POST");
                conn.setRequestProperty("Content-Type", "application/json");
                conn.setDoOutput(true);

                byte[] bytes = body.getBytes(StandardCharsets.UTF_8);
                conn.setFixedLengthStreamingMode(bytes.length);
                try (OutputStream os = conn.getOutputStream()) {
                    os.write(bytes);
                }

                int code = conn.getResponseCode();
                log("← HTTP " + code + " (" + body.length() + " bytes sent)");
                return code >= 200 && code < 300;

            } catch (Exception e) {
                log("✗ transport error: " + e.getMessage());
                return false;
            }
        });

        log("SDK initialized  batchSize=3  storageDir=" + getFilesDir());
    }

    private void wireButtons() {
        Button btnLog   = findViewById(R.id.btnLog);
        Button btnFlush = findViewById(R.id.btnFlush);
        Button btnSize  = findViewById(R.id.btnSize);

        btnLog.setOnClickListener(v -> {
            String name    = "button_tap";
            String payload = "{\"count\":" + (++eventCount) + "}";
            EventSDK.logEvent(name, payload);
            log("logEvent  name=" + name + "  payload=" + payload
                + "  queueSize=" + EventSDK.queueSize());
        });

        btnFlush.setOnClickListener(v -> {
            int pending = EventSDK.queueSize();
            if (pending == 0) { log("queue empty, nothing to flush"); return; }
            log("flushing " + pending + " events →");
            EventSDK.flush();
        });

        btnSize.setOnClickListener(v -> {
            log("queueSize = " + EventSDK.queueSize());
        });
    }

    private void log(String msg) {
        Log.d(TAG, msg);
        logView.append("\n" + msg);
        scrollView.post(() -> scrollView.fullScroll(ScrollView.FOCUS_DOWN));
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        EventSDK.shutdown();
    }
}
