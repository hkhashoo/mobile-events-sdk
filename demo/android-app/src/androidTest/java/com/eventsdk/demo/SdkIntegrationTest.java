package com.eventsdk.demo;

import static org.junit.Assert.*;

import android.content.Context;
import android.os.StrictMode;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import com.eventsdk.EventSDK;
import com.eventsdk.EventSDKConfig;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

@RunWith(AndroidJUnit4.class)
public class SdkIntegrationTest {

    private Context context;

    @Before
    public void setUp() {
        context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        // Allow network on test thread (not main thread, so StrictMode is fine)
        StrictMode.setThreadPolicy(
            new StrictMode.ThreadPolicy.Builder().permitAll().build()
        );
    }

    @After
    public void tearDown() {
        EventSDK.shutdown();
    }

    // ── helpers ───────────────────────────────────────────────────────────────

    private EventSDKConfig baseConfig() {
        EventSDKConfig cfg = new EventSDKConfig(
            "http://localhost/unused",
            context.getFilesDir().getAbsolutePath()
        );
        cfg.batchSize        = 5;
        cfg.maxQueueCapacity = 100;
        return cfg;
    }

    private static boolean httpPost(String url, String body) {
        try {
            HttpURLConnection conn = (HttpURLConnection) new URL(url).openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setDoOutput(true);
            byte[] bytes = body.getBytes(StandardCharsets.UTF_8);
            conn.setFixedLengthStreamingMode(bytes.length);
            try (OutputStream os = conn.getOutputStream()) { os.write(bytes); }
            int code = conn.getResponseCode();
            return code >= 200 && code < 300;
        } catch (Exception e) {
            return false;
        }
    }

    // ── tests ─────────────────────────────────────────────────────────────────

    @Test
    public void logEvent_increases_queue_size() {
        EventSDK.init(baseConfig());
        EventSDK.setTransport((url, body) -> true);

        assertEquals(0, EventSDK.queueSize());
        EventSDK.logEvent("tap", "{}");
        assertEquals(1, EventSDK.queueSize());
        EventSDK.logEvent("purchase", "{\"price\":9.99}");
        assertEquals(2, EventSDK.queueSize());
    }

    @Test
    public void manual_flush_drains_queue() {
        EventSDK.init(baseConfig());

        AtomicInteger postCount = new AtomicInteger(0);
        EventSDK.setTransport((url, body) -> {
            postCount.incrementAndGet();
            return true;
        });

        EventSDK.logEvent("e1", "{}");
        EventSDK.logEvent("e2", "{}");
        EventSDK.flush();

        assertEquals("queue must be empty after flush", 0, EventSDK.queueSize());
        assertEquals("transport called exactly once", 1, postCount.get());
    }

    @Test
    public void flush_with_no_transport_returns_events_to_queue() {
        EventSDK.init(baseConfig());
        // intentionally no setTransport call
        EventSDK.logEvent("e1", "{}");
        EventSDK.flush();
        assertEquals("event must be returned when no transport set", 1, EventSDK.queueSize());
    }

    @Test
    public void autoFlush_fires_at_threshold() {
        EventSDKConfig cfg = baseConfig();
        cfg.autoFlush          = true;
        cfg.autoFlushThreshold = 3;
        EventSDK.init(cfg);

        AtomicInteger postCount = new AtomicInteger(0);
        EventSDK.setTransport((url, body) -> {
            postCount.incrementAndGet();
            return true;
        });

        EventSDK.logEvent("e1", "{}");
        EventSDK.logEvent("e2", "{}");
        assertEquals("no flush below threshold", 0, postCount.get());

        EventSDK.logEvent("e3", "{}");
        assertEquals("auto-flush at threshold=3", 1, postCount.get());
        assertEquals("queue empty after auto-flush", 0, EventSDK.queueSize());
    }

    @Test
    public void timer_flush_fires_within_interval() throws InterruptedException {
        EventSDKConfig cfg = baseConfig();
        cfg.flushIntervalSeconds = 2;
        EventSDK.init(cfg);

        CountDownLatch latch = new CountDownLatch(1);
        EventSDK.setTransport((url, body) -> {
            latch.countDown();
            return true;
        });

        EventSDK.logEvent("timed_event", "{\"source\":\"timer_test\"}");

        boolean fired = latch.await(5, TimeUnit.SECONDS);
        assertTrue("timer did not flush within 5 seconds", fired);
        assertEquals("queue empty after timer flush", 0, EventSDK.queueSize());
    }

    @Test
    public void real_http_flush_posts_to_endpoint() throws InterruptedException {
        EventSDKConfig cfg = new EventSDKConfig(
            BuildConfig.FLUSH_URL,
            context.getFilesDir().getAbsolutePath()
        );
        cfg.batchSize = 2;
        EventSDK.init(cfg);

        CountDownLatch latch    = new CountDownLatch(1);
        AtomicBoolean httpOk    = new AtomicBoolean(false);

        EventSDK.setTransport((url, body) -> {
            boolean ok = httpPost(url, body);
            httpOk.set(ok);
            latch.countDown();
            return ok;
        });

        EventSDK.logEvent("integration_test",   "{\"platform\":\"android\",\"test\":true}");
        EventSDK.logEvent("integration_test_2", "{\"platform\":\"android\",\"test\":true}");
        EventSDK.flush();

        latch.await(10, TimeUnit.SECONDS);
        assertTrue("HTTP POST to endpoint failed", httpOk.get());
        assertEquals("queue empty after real HTTP flush", 0, EventSDK.queueSize());
    }
}
