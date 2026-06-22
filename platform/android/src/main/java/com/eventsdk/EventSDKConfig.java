package com.eventsdk;

public class EventSDKConfig {
    public final String endpoint;
    public final String storageDir;
    public int batchSize = 20;
    public int maxQueueCapacity = 1000;

    public EventSDKConfig(String endpoint, String storageDir) {
        this.endpoint = endpoint;
        this.storageDir = storageDir;
    }
}
