package com.eventsdk;

public class EventSDKConfig {
    public final String endpoint;
    public final String storageDir;
    public int     batchSize            = 20;
    public int     maxQueueCapacity     = 1000;
    public boolean autoFlush            = false;
    public int     autoFlushThreshold   = 0;    // 0 = use batchSize
    public int     flushIntervalSeconds = 0;    // 0 = disabled
    public boolean atLeastOnce          = false; // true = persist before send, recover on restart

    public EventSDKConfig(String endpoint, String storageDir) {
        this.endpoint = endpoint;
        this.storageDir = storageDir;
    }
}
