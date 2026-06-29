# mobile-events-sdk

[![CI](https://github.com/hkhashoo/mobile-events-sdk/actions/workflows/ci.yml/badge.svg)](https://github.com/hkhashoo/mobile-events-sdk/actions/workflows/ci.yml)

Cross-platform mobile analytics SDK. C++17 core with a JNI bridge for Android and an ObjC++ wrapper for iOS. Zero third-party dependencies. Pluggable HTTP transport.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                     Application Layer                         │
│         Android (Java/Kotlin)        iOS (Swift)             │
└───────────────────────┬──────────────────────┬──────────────┘
                        │ JNI                   │ ObjC++ direct
┌───────────────────────▼──────────────────────▼──────────────┐
│                   Platform Bridge Layer                       │
│     EventSDK_JNI.cpp (Android)    EventSDK.mm (iOS)         │
└───────────────────────────────┬──────────────────────────────┘
                                │ C++ #include
┌───────────────────────────────▼──────────────────────────────┐
│                     C++ Core (C++17)                          │
│  EventQueue   EventStore   FlushManager   Config             │
└───────────────────────────────┬──────────────────────────────┘
                                │ std::function<bool(url, body)>
┌───────────────────────────────▼──────────────────────────────┐
│         Pluggable Transport  (caller-supplied)                │
│  HttpURLConnection (Android)     URLSession (iOS)            │
└──────────────────────────────────────────────────────────────┘
```

## Features

- **Thread-safe queue** — bounded FIFO, drop-head eviction at capacity
- **Disk persistence** — tab-delimited append log survives process restarts
- **Batch HTTP flush** — events serialised as `{"events":[…]}` JSON
- **Auto-flush** — threshold-based (every N events) and timer-based (every N seconds)
- **Clean shutdown** — `condition_variable` wakes timer immediately, no wait for interval

See [DESIGN.md](DESIGN.md) for threading model, flush strategy, and trade-offs.

---

## Android Integration

**1. Add the SDK module to `settings.gradle`:**
```groovy
include ':sdk-android'
project(':sdk-android').projectDir = file('path/to/platform/android')
```

**2. Depend on it:**
```groovy
implementation project(':sdk-android')
```

**3. Initialise, wire transport, log events:**
```java
EventSDKConfig config = new EventSDKConfig("https://ingest.example.com/events",
                                            getFilesDir().getAbsolutePath());
config.batchSize = 20;
config.autoFlush = true;
config.autoFlushThreshold = 20;
config.flushIntervalSeconds = 30;
EventSDK.init(config);

EventSDK.setTransport((url, body) -> {
    // your HttpURLConnection / OkHttp call here
    return responseCode >= 200 && responseCode < 300;
});

EventSDK.logEvent("screen_view", "{\"screen\":\"home\"}");
```

**4. Shut down when done:**
```java
EventSDK.shutdown();   // joins timer thread, releases JNI globals
```

---

## iOS Integration

**1. Add `platform/ios` and `sdk-core/src` to your target sources** (or use the provided `project.yml` with XcodeGen).

**2. Import in your bridging header:**
```objc
#import "EventSDK.h"
```

**3. Initialise, wire transport, log events:**
```swift
let config = EventSDKConfig(
    endpoint: "https://ingest.example.com/events",
    storageDir: FileManager.default.urls(for: .documentDirectory,
                                          in: .userDomainMask)[0].path
)
config.batchSize = 20
config.autoFlush = true
EventSDK.start(with: config)

EventSDK.setTransport { url, body in
    // URLSession call; DispatchSemaphore bridges async→sync
    // (see demo/ios-app/EventSDKDemo/ContentView.swift for full example)
    return success
}

EventSDK.logEvent("screen_view", payload: "{\"screen\":\"home\"}")
```

**4. Shut down when done:**
```swift
EventSDK.shutdown()
```

---

## Building

### C++ unit tests (macOS / Linux)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/tests/event_sdk_tests
```

### Android AAR

```bash
./gradlew :sdk-android:assembleRelease
# output: platform/android/build/outputs/aar/sdk-android-release.aar
```

### iOS demo (Simulator)

```bash
cd demo/ios-app
xcodegen generate          # regenerates EventSDKDemo.xcodeproj from project.yml
xcodebuild -project EventSDKDemo.xcodeproj \
           -scheme EventSDKDemo \
           -destination 'platform=iOS Simulator,name=iPhone 16' \
           build
```

---

## Project Layout

```
sdk-core/src/          C++ core — EventQueue, EventStore, FlushManager, Config
platform/android/      Android library module (Gradle + CMake + JNI bridge)
platform/ios/          iOS ObjC++ wrapper (EventSDK.h / EventSDK.mm)
demo/android-app/      Android demo app (instrumented integration tests included)
demo/ios-app/          iOS SwiftUI demo app (XcodeGen project.yml)
tests/                 C++ unit tests (47 cases, plain assert, no framework)
```

---

## Testing

| Layer | How |
|---|---|
| C++ core | `./build/tests/event_sdk_tests` — 47 unit tests |
| Android JNI | `./gradlew :demo-android:connectedAndroidTest` — 6 instrumented tests |
| iOS | Manual via Xcode Simulator; `EventSDK.mm` covered by same C++ unit tests |
