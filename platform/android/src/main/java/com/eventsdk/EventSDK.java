package com.eventsdk;

public final class EventSDK {

    static {
        System.loadLibrary("eventsdk_jni");
    }

    // Platform-supplied HTTP transport. post() must be safe to call from any thread.
    public interface Transport {
        boolean post(String url, String body);
    }

    // Emitted after every flush. All fields are cumulative since SDK init.
    public interface HealthListener {
        void onMetrics(long eventsQueued, long eventsDropped,
                       long flushCount,  long transportFailures,
                       double lastFlushLatencyMs, long bytesSent,
                       double queueUtilizationPct);
    }

    public static void init(EventSDKConfig config) {
        nativeInit(
            config.endpoint,
            config.batchSize,
            config.maxQueueCapacity,
            config.storageDir,
            config.autoFlush,
            config.autoFlushThreshold,
            config.flushIntervalSeconds,
            config.atLeastOnce
        );
    }

    public static void setTransport(Transport transport) {
        nativeSetTransport(transport);
    }

    // Register a listener to receive health metrics after every flush.
    public static void setHealthListener(HealthListener listener) {
        nativeSetHealthListener(listener);
    }

    // payloadJson must be a valid JSON value: object, array, string, number, or null.
    public static void logEvent(String name, String payloadJson) {
        nativeLogEvent(name, payloadJson);
    }

    public static void flush() {
        nativeFlush();
    }

    // Call from Activity.onTrimMemory(TRIM_MEMORY_RUNNING_CRITICAL) to flush under pressure.
    public static void onMemoryPressure() {
        nativeFlush();
    }

    public static int queueSize() {
        return nativeQueueSize();
    }

    public static void shutdown() {
        nativeShutdown();
    }

    private EventSDK() {}

    private static native void nativeInit(String endpoint, int batchSize,
                                          int maxQueueCapacity, String storageDir,
                                          boolean autoFlush, int autoFlushThreshold,
                                          int flushIntervalSeconds, boolean atLeastOnce);
    private static native void nativeSetTransport(Transport transport);
    private static native void nativeSetHealthListener(HealthListener listener);
    private static native void nativeLogEvent(String name, String payloadJson);
    private static native void nativeFlush();
    private static native int  nativeQueueSize();
    private static native void nativeShutdown();
}
