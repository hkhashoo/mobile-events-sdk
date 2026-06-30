import SwiftUI
import OSLog
import UIKit

private let logger = Logger(subsystem: "com.eventsdk.demo", category: "EventSDK")

// ── ViewModel ─────────────────────────────────────────────────────────────────

@MainActor
final class DemoViewModel: ObservableObject {
    @Published var log = "[ready]"
    private var eventCount = 0

    func initSDK() {
        let docsDir = FileManager.default.urls(for: .documentDirectory,
                                               in: .userDomainMask)[0].path
        let config = EventSDKConfig(
            endpoint: "https://webhook.site/45eacb0e-3fe8-44ef-af86-0ffd04dba99f",
            storageDir: docsDir
        )
        config.batchSize            = 3
        config.autoFlush            = true
        config.autoFlushThreshold   = 3
        config.flushIntervalSeconds = 30

        EventSDK.start(with: config)

        EventSDK.setHealthCallback { [weak self] m in
            DispatchQueue.main.async {
                self?.appendLog(String(format:
                    "health: queued=%lu dropped=%lu flushes=%lu failures=%lu latency=%.1fms sent=%luB util=%.0f%%",
                    m.eventsQueued, m.eventsDropped, m.flushCount, m.transportFailures,
                    m.lastFlushLatencyMs, m.bytesSent, m.queueUtilizationPct))
            }
        }

        // Transport is called on a background thread when the timer fires.
        // DispatchSemaphore bridges async URLSession to the synchronous C++ contract.
        EventSDK.setTransport { [weak self] url, body in
            guard let nsUrl = URL(string: url as String) else { return false }
            var request    = URLRequest(url: nsUrl)
            request.httpMethod = "POST"
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.httpBody   = (body as String).data(using: .utf8)

            var success = false
            let sem     = DispatchSemaphore(value: 0)

            URLSession.shared.dataTask(with: request) { _, response, error in
                if let http = response as? HTTPURLResponse {
                    success = (200..<300).contains(http.statusCode)
                    logger.debug("← HTTP \(http.statusCode)")
                    DispatchQueue.main.async {
                        self?.appendLog("← HTTP \(http.statusCode)")
                    }
                } else if let error {
                    logger.error("transport error: \(error.localizedDescription)")
                }
                sem.signal()
            }.resume()

            sem.wait()
            return success
        }

        appendLog("SDK initialized  batchSize=3  autoFlush=true  timer=30s")
    }

    func logEvent() {
        eventCount += 1
        let device = UIDevice.current
        let payload = "{"
            + "\"count\":\(eventCount)"
            + ",\"device\":\"\(device.model)\""
            + ",\"os\":\"\(device.systemName) \(device.systemVersion)\""
            + ",\"name\":\"\(device.name)\""
            + "}"
        EventSDK.logEvent("button_tap", payload: payload)
        appendLog("logEvent  payload=\(payload)  queueSize=\(EventSDK.queueSize())")
    }

    func flush() {
        let pending = EventSDK.queueSize()
        guard pending > 0 else { appendLog("queue empty, nothing to flush"); return }
        appendLog("flushing \(pending) events →")
        EventSDK.flush { [weak self] success, error in
            // Delivered on main queue by EventSDK.mm
            self?.appendLog(success ? "flush ok" : "flush failed: \(error ?? "")")
        }
    }

    func showQueueSize() {
        appendLog("queueSize = \(EventSDK.queueSize())")
    }

    func appendLog(_ msg: String) {
        log += "\n" + msg
    }

    deinit {
        EventSDK.shutdown()
    }
}

// ── View ─────────────────────────────────────────────────────────────────────

struct ContentView: View {
    @StateObject private var vm = DemoViewModel()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("EventSDK Demo")
                .font(.title2).bold()
                .padding(.bottom, 8)

            Button("Log Event", action: vm.logEvent)
                .buttonStyle(.borderedProminent)
                .frame(maxWidth: .infinity)

            Button("Flush to Server", action: vm.flush)
                .buttonStyle(.bordered)
                .frame(maxWidth: .infinity)

            Button("Queue Size", action: vm.showQueueSize)
                .buttonStyle(.bordered)
                .frame(maxWidth: .infinity)

            ScrollView {
                Text(vm.log)
                    .font(.system(.caption, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(8)
            }
            .background(Color(.systemGray6))
            .cornerRadius(8)
        }
        .padding()
        .onAppear { vm.initSDK() }
    }
}
