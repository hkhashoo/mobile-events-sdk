#include <jni.h>
#include <android/log.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "Config.h"
#include "EventQueue.h"
#include "EventStore.h"
#include "FlushManager.h"
#include "HealthMetrics.h"

#define LOG_TAG "EventSDK"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Cached once in JNI_OnLoad. Safe to read from any thread without a lock.
static JavaVM* gJvm = nullptr;

struct SDKState {
    eventsdk::Config config;
    std::unique_ptr<eventsdk::EventQueue>    queue;
    std::unique_ptr<eventsdk::EventStore>    store;
    std::unique_ptr<eventsdk::FlushManager>  flushManager;
    jobject    transportObj        = nullptr;   // JNI global ref
    jmethodID  transportMethodId   = nullptr;
    jobject    healthListenerObj   = nullptr;   // JNI global ref
    jmethodID  healthListenerMid   = nullptr;
};

static std::unique_ptr<SDKState> gState;
static std::mutex                gStateMutex;

// ── JNI_OnLoad ───────────────────────────────────────────────────────────────

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    gJvm = vm;
    return JNI_VERSION_1_6;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string jstringToStd(JNIEnv* env, jstring jstr) {
    if (!jstr) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

static void throwRuntime(JNIEnv* env, const char* msg) {
    jclass cls = env->FindClass("java/lang/RuntimeException");
    if (cls) env->ThrowNew(cls, msg);
}

// Attach current thread to the JVM if not already attached.
// Returns true if we attached (caller must detach).
static bool attachIfNeeded(JNIEnv** outEnv) {
    jint res = gJvm->GetEnv(reinterpret_cast<void**>(outEnv), JNI_VERSION_1_6);
    if (res == JNI_OK)        return false;
    if (res == JNI_EDETACHED) {
        gJvm->AttachCurrentThread(outEnv, nullptr);
        return true;
    }
    return false;
}

// ── Native implementations ───────────────────────────────────────────────────

extern "C" {

JNIEXPORT void JNICALL
Java_com_eventsdk_EventSDK_nativeInit(JNIEnv* env, jclass /*clazz*/,
    jstring endpoint, jint batchSize, jint maxQueueCapacity, jstring storageDir,
    jboolean autoFlush, jint autoFlushThreshold, jint flushIntervalSeconds,
    jboolean atLeastOnce)
{
    std::lock_guard<std::mutex> lock(gStateMutex);

    auto state = std::make_unique<SDKState>();
    state->config.endpoint             = jstringToStd(env, endpoint);
    state->config.batchSize            = static_cast<std::size_t>(batchSize);
    state->config.maxQueueCapacity     = static_cast<std::size_t>(maxQueueCapacity);
    state->config.storageDir           = jstringToStd(env, storageDir);
    state->config.autoFlush            = autoFlush == JNI_TRUE;
    state->config.autoFlushThreshold   = static_cast<std::size_t>(autoFlushThreshold);
    state->config.flushIntervalSeconds = static_cast<int>(flushIntervalSeconds);
    state->config.deliveryMode = (atLeastOnce == JNI_TRUE)
        ? eventsdk::DeliveryMode::AtLeastOnce
        : eventsdk::DeliveryMode::AtMostOnce;

    state->queue        = std::make_unique<eventsdk::EventQueue>(state->config.maxQueueCapacity);
    state->store        = std::make_unique<eventsdk::EventStore>(state->config.storageDir);
    state->flushManager = std::make_unique<eventsdk::FlushManager>(*state->queue, state->config);
    state->flushManager->setStore(state->store.get());
    state->flushManager->loadPersistedEvents();

    gState = std::move(state);
    LOGD("init: endpoint=%s batchSize=%d atLeastOnce=%d",
         gState->config.endpoint.c_str(), (int)batchSize, (int)(atLeastOnce == JNI_TRUE));
}

JNIEXPORT void JNICALL
Java_com_eventsdk_EventSDK_nativeSetTransport(JNIEnv* env, jclass /*clazz*/, jobject transport)
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gState) { throwRuntime(env, "EventSDK not initialized"); return; }

    if (gState->transportObj) {
        env->DeleteGlobalRef(gState->transportObj);
        gState->transportObj = nullptr;
    }

    if (!transport) {
        gState->flushManager->setTransport(nullptr);
        return;
    }

    // Global ref keeps the Java transport object alive across JNI calls
    gState->transportObj = env->NewGlobalRef(transport);

    // Cache method ID once — cheaper than GetMethodID on every flush
    jclass cls = env->GetObjectClass(transport);
    gState->transportMethodId = env->GetMethodID(cls, "post",
        "(Ljava/lang/String;Ljava/lang/String;)Z");

    if (!gState->transportMethodId) {
        throwRuntime(env, "Transport must implement: boolean post(String url, String body)");
        return;
    }

    jobject   ref      = gState->transportObj;
    jmethodID methodId = gState->transportMethodId;

    gState->flushManager->startTimer();

    gState->flushManager->setTransport(
        [ref, methodId](const std::string& url, const std::string& body) -> bool {
            JNIEnv* callEnv  = nullptr;
            bool    attached = attachIfNeeded(&callEnv);

            jstring jUrl  = callEnv->NewStringUTF(url.c_str());
            jstring jBody = callEnv->NewStringUTF(body.c_str());

            jboolean ok = callEnv->CallBooleanMethod(ref, methodId, jUrl, jBody);

            callEnv->DeleteLocalRef(jUrl);
            callEnv->DeleteLocalRef(jBody);

            if (attached) gJvm->DetachCurrentThread();
            return ok == JNI_TRUE;
        }
    );
}

JNIEXPORT void JNICALL
Java_com_eventsdk_EventSDK_nativeSetHealthListener(JNIEnv* env, jclass /*clazz*/, jobject listener)
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gState) { throwRuntime(env, "EventSDK not initialized"); return; }

    if (gState->healthListenerObj) {
        env->DeleteGlobalRef(gState->healthListenerObj);
        gState->healthListenerObj = nullptr;
    }

    if (!listener) {
        gState->flushManager->setHealthCallback(nullptr);
        return;
    }

    gState->healthListenerObj = env->NewGlobalRef(listener);
    jclass cls = env->GetObjectClass(listener);
    // signature: void onMetrics(long,long,long,long,double,long,double)
    gState->healthListenerMid = env->GetMethodID(cls, "onMetrics", "(JJJJDJD)V");

    if (!gState->healthListenerMid) {
        throwRuntime(env, "HealthListener must implement onMetrics(long,long,long,long,double,long,double)");
        return;
    }

    jobject   ref = gState->healthListenerObj;
    jmethodID mid = gState->healthListenerMid;

    gState->flushManager->setHealthCallback(
        [ref, mid](const eventsdk::HealthMetrics& m) {
            JNIEnv* callEnv  = nullptr;
            bool    attached = attachIfNeeded(&callEnv);

            callEnv->CallVoidMethod(ref, mid,
                (jlong)m.eventsQueued,
                (jlong)m.eventsDropped,
                (jlong)m.flushCount,
                (jlong)m.transportFailures,
                (jdouble)m.lastFlushLatencyMs,
                (jlong)m.bytesSent,
                (jdouble)m.queueUtilizationPct);

            if (attached) gJvm->DetachCurrentThread();
        }
    );
}

JNIEXPORT void JNICALL
Java_com_eventsdk_EventSDK_nativeLogEvent(JNIEnv* env, jclass /*clazz*/,
    jstring name, jstring payloadJson)
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gState) { throwRuntime(env, "EventSDK not initialized"); return; }

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    eventsdk::Event e;
    e.name        = jstringToStd(env, name);
    e.payload     = jstringToStd(env, payloadJson);
    e.timestampMs = static_cast<int64_t>(nowMs);

    gState->flushManager->push(std::move(e));
}

JNIEXPORT void JNICALL
Java_com_eventsdk_EventSDK_nativeFlush(JNIEnv* env, jclass /*clazz*/)
{
    // Lock held for the full flush.
    // Known limitation: logEvent() blocks during HTTP round-trip.
    // Fix: shared_ptr<SDKState> + lock only around state access, not transport call.
    // Left as-is for demo; documented in DESIGN.md.
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gState) { throwRuntime(env, "EventSDK not initialized"); return; }
    gState->flushManager->flush();
}

JNIEXPORT jint JNICALL
Java_com_eventsdk_EventSDK_nativeQueueSize(JNIEnv* /*env*/, jclass /*clazz*/)
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    return gState ? static_cast<jint>(gState->flushManager->pendingCount()) : 0;
}

JNIEXPORT void JNICALL
Java_com_eventsdk_EventSDK_nativeShutdown(JNIEnv* env, jclass /*clazz*/)
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gState) return;
    gState->flushManager->stopTimer();
    if (gState->transportObj) {
        env->DeleteGlobalRef(gState->transportObj);
        gState->transportObj = nullptr;
    }
    if (gState->healthListenerObj) {
        env->DeleteGlobalRef(gState->healthListenerObj);
        gState->healthListenerObj = nullptr;
    }
    gState.reset();
    LOGD("shutdown complete");
}

} // extern "C"
