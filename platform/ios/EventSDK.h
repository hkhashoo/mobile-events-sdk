#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// ── Delivery mode ─────────────────────────────────────────────────────────────

typedef NS_ENUM(NSInteger, EventSDKDeliveryMode) {
    EventSDKDeliveryModeAtMostOnce  = 0,  // events lost if transport fails (default)
    EventSDKDeliveryModeAtLeastOnce = 1,  // persisted before send; recovered on restart
};

// ── Config ────────────────────────────────────────────────────────────────────

@interface EventSDKConfig : NSObject

@property (nonatomic, copy)   NSString              *endpoint;
@property (nonatomic, copy)   NSString              *storageDir;
@property (nonatomic, assign) NSUInteger             batchSize;             // default 20
@property (nonatomic, assign) NSUInteger             maxQueueCapacity;      // default 1000
@property (nonatomic, assign) BOOL                   autoFlush;             // default NO
@property (nonatomic, assign) NSUInteger             autoFlushThreshold;    // 0 = use batchSize
@property (nonatomic, assign) NSInteger              flushIntervalSeconds;  // 0 = disabled
@property (nonatomic, assign) EventSDKDeliveryMode   deliveryMode;          // default AtMostOnce

- (instancetype)initWithEndpoint:(NSString *)endpoint
                      storageDir:(NSString *)storageDir NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

@end

// ── Health metrics ────────────────────────────────────────────────────────────

typedef struct {
    NSUInteger eventsQueued;
    NSUInteger eventsDropped;        // cumulative capacity evictions
    NSUInteger flushCount;
    NSUInteger transportFailures;
    double     lastFlushLatencyMs;
    NSUInteger bytesSent;
    double     queueUtilizationPct;
} EventSDKHealthMetrics;

typedef void (^EventSDKHealthCallback)(EventSDKHealthMetrics metrics);

// ── Transport + flush callback ────────────────────────────────────────────────

// Synchronous transport block — called on a background thread when the timer fires.
// Return YES on HTTP 2xx, NO otherwise.
// Use DispatchSemaphore to bridge async URLSession to the synchronous contract.
typedef BOOL (^EventSDKTransport)(NSString *url, NSString *body);

typedef void (^EventSDKFlushCompletion)(BOOL success, NSString * _Nullable error);

// ── SDK ───────────────────────────────────────────────────────────────────────

@interface EventSDK : NSObject

// Call once at app launch. Replaces any existing state.
+ (void)startWithConfig:(EventSDKConfig *)config;

// Register HTTP transport before first flush. startTimer() fires automatically.
+ (void)setTransport:(nullable EventSDKTransport)transport;

// Register health callback — called after every flush on the transport thread.
+ (void)setHealthCallback:(nullable EventSDKHealthCallback)callback;

// payloadJSON must be a valid JSON value: {}, [], "string", number, or null.
+ (void)logEvent:(NSString *)name payload:(NSString *)payloadJSON;

+ (void)flush;
+ (void)flushWithCompletion:(nullable EventSDKFlushCompletion)completion;

+ (NSUInteger)queueSize;
+ (void)shutdown;

+ (instancetype)new  NS_UNAVAILABLE;
- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
