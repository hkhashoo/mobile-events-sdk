// EventSDK.mm — Objective-C++ wrapper around the C++ core.
// The .mm extension lets us #include C++ headers directly — no JNI bridge needed.

#import "EventSDK.h"

#include <chrono>
#include <memory>
#include <mutex>

#include "Config.h"
#include "EventQueue.h"
#include "EventStore.h"
#include "FlushManager.h"

// ── Singleton state ───────────────────────────────────────────────────────────

struct IOSSDKState {
    eventsdk::Config                         config;
    std::unique_ptr<eventsdk::EventQueue>    queue;
    std::unique_ptr<eventsdk::EventStore>    store;
    std::unique_ptr<eventsdk::FlushManager>  flushManager;
};

static std::unique_ptr<IOSSDKState> gState;
static std::mutex                   gStateMutex;

// ── EventSDKConfig ────────────────────────────────────────────────────────────

@implementation EventSDKConfig

- (instancetype)initWithEndpoint:(NSString *)endpoint storageDir:(NSString *)storageDir {
    self = [super init];
    if (self) {
        _endpoint             = [endpoint copy];
        _storageDir           = [storageDir copy];
        _batchSize            = 20;
        _maxQueueCapacity     = 1000;
        _autoFlush            = NO;
        _autoFlushThreshold   = 0;
        _flushIntervalSeconds = 0;
    }
    return self;
}

@end

// ── EventSDK ─────────────────────────────────────────────────────────────────

@implementation EventSDK

+ (void)startWithConfig:(EventSDKConfig *)config {
    std::lock_guard<std::mutex> lock(gStateMutex);

    auto state = std::make_unique<IOSSDKState>();
    state->config.endpoint             = config.endpoint.UTF8String;
    state->config.batchSize            = static_cast<std::size_t>(config.batchSize);
    state->config.maxQueueCapacity     = static_cast<std::size_t>(config.maxQueueCapacity);
    state->config.storageDir           = config.storageDir.UTF8String;
    state->config.autoFlush            = config.autoFlush == YES;
    state->config.autoFlushThreshold   = static_cast<std::size_t>(config.autoFlushThreshold);
    state->config.flushIntervalSeconds = static_cast<int>(config.flushIntervalSeconds);

    state->queue        = std::make_unique<eventsdk::EventQueue>(state->config.maxQueueCapacity);
    state->store        = std::make_unique<eventsdk::EventStore>(state->config.storageDir);
    state->flushManager = std::make_unique<eventsdk::FlushManager>(*state->queue, state->config);

    gState = std::move(state);
}

+ (void)setTransport:(nullable EventSDKTransport)transport {
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gState) return;

    if (!transport) {
        gState->flushManager->setTransport(nullptr);
        return;
    }

    // Copy the block — ARC manages retain/release across the C++ lambda lifetime.
    EventSDKTransport transportCopy = [transport copy];

    gState->flushManager->setTransport(
        [transportCopy](const std::string& url, const std::string& body) -> bool {
            NSString *nsUrl  = [NSString stringWithUTF8String:url.c_str()];
            NSString *nsBody = [NSString stringWithUTF8String:body.c_str()];
            return transportCopy(nsUrl, nsBody) == YES;
        }
    );

    gState->flushManager->startTimer();
}

+ (void)logEvent:(NSString *)name payload:(NSString *)payloadJSON {
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gState) return;

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    eventsdk::Event e;
    e.name        = name.UTF8String;
    e.payload     = payloadJSON.UTF8String;
    e.timestampMs = static_cast<int64_t>(nowMs);

    gState->flushManager->push(std::move(e));
}

+ (void)flush {
    [self flushWithCompletion:nil];
}

+ (void)flushWithCompletion:(nullable EventSDKFlushCompletion)completion {
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gState) {
        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completion(NO, @"EventSDK not initialized");
            });
        }
        return;
    }

    if (completion) {
        gState->flushManager->flush([completion](bool ok, const std::string& err) {
            NSString *nsErr = ok ? nil : [NSString stringWithUTF8String:err.c_str()];
            // Completion always delivered on main queue — safe for UI updates.
            dispatch_async(dispatch_get_main_queue(), ^{
                completion(ok, nsErr);
            });
        });
    } else {
        gState->flushManager->flush();
    }
}

+ (NSUInteger)queueSize {
    std::lock_guard<std::mutex> lock(gStateMutex);
    return gState ? static_cast<NSUInteger>(gState->flushManager->pendingCount()) : 0;
}

+ (void)shutdown {
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gState) return;
    gState->flushManager->stopTimer();
    gState.reset();
}

@end
