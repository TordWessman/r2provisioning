import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
import Testing
@testable import r2provisioning

@Test func versionIsSet() {
    #expect(!R2Provisioning.version.isEmpty)
}

// MARK: - Payload

private func bytes(_ parts: [UInt8]...) -> [UInt8] { parts.flatMap { $0 } }

@Suite struct PayloadTests {
    @Test func encodesLayout() throws {
        let body = try R2ProvisioningPayload.encode(
            ssid: "home", password: "secret123", data: Data([0x00, 0x01, 0x02, 0xFF]))
        let expected = bytes([4], Array("home".utf8), [9], Array("secret123".utf8), [4, 0], [0x00, 0x01, 0x02, 0xFF])
        #expect(Array(body) == expected)
    }

    @Test func dataLengthIsLittleEndian() throws {
        let body = try R2ProvisioningPayload.encode(ssid: "a", password: "", data: Data(count: 300))
        #expect(body[2] == 0)        // pwLen
        #expect(body[3] == 0x2C)     // 300 & 0xFF
        #expect(body[4] == 0x01)     // 300 >> 8
        #expect(body.count == 5 + 300)
    }

    @Test func measuresUTF8Bytes() throws {
        // "å" is two bytes in UTF-8.
        let body = try R2ProvisioningPayload.encode(ssid: "å", password: "", data: Data())
        #expect(body[0] == 2)
        #expect(throws: R2ProvisioningError.self) {
            try R2ProvisioningPayload.encode(ssid: String(repeating: "å", count: 17), password: "", data: Data())
        }
    }

    @Test func rejectsInvalidInput() {
        #expect(throws: R2ProvisioningError.self) {
            try R2ProvisioningPayload.encode(ssid: "", password: "", data: Data())
        }
        #expect(throws: R2ProvisioningError.self) {
            try R2ProvisioningPayload.encode(ssid: String(repeating: "x", count: 33), password: "", data: Data())
        }
        #expect(throws: R2ProvisioningError.self) {
            try R2ProvisioningPayload.encode(ssid: "x", password: String(repeating: "p", count: 65), data: Data())
        }
        #expect(throws: R2ProvisioningError.self) {
            try R2ProvisioningPayload.encode(ssid: "x", password: "", data: Data(count: 17), maxDataSize: 16)
        }
    }
}

// MARK: - Client

/// Routes every request of a stubbed URLSession to `handler`.
final class StubURLProtocol: URLProtocol {
    struct Recorded: Sendable {
        let path: String
        let body: Data?
    }

    nonisolated(unsafe) static var handler: (@Sendable (String) -> (status: Int, body: Data)?)?
    nonisolated(unsafe) static var recorded: [Recorded] = []
    private static let lock = NSLock()

    static func reset(_ handler: @escaping @Sendable (String) -> (status: Int, body: Data)?) {
        lock.lock(); defer { lock.unlock() }
        self.handler = handler
        recorded = []
    }

    static func requests(to path: String) -> [Recorded] {
        lock.lock(); defer { lock.unlock() }
        return recorded.filter { $0.path == path }
    }

    static func session() -> URLSession {
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [StubURLProtocol.self]
        return URLSession(configuration: config)
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        let path = request.url?.path ?? ""
        Self.lock.lock()
        Self.recorded.append(Recorded(path: path, body: Self.body(of: request)))
        let handler = Self.handler
        Self.lock.unlock()

        guard let reply = handler?(path) else {
            client?.urlProtocol(self, didFailWithError: URLError(.cannotConnectToHost))
            return
        }
        let response = HTTPURLResponse(
            url: request.url!, statusCode: reply.status, httpVersion: "HTTP/1.1", headerFields: nil)!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: reply.body)
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}

    private static func body(of request: URLRequest) -> Data? {
        if let body = request.httpBody { return body }
        guard let stream = request.httpBodyStream else { return nil }
        stream.open(); defer { stream.close() }
        var data = Data()
        var buffer = [UInt8](repeating: 0, count: 1024)
        while stream.hasBytesAvailable {
            let n = stream.read(&buffer, maxLength: buffer.count)
            if n <= 0 { break }
            data.append(buffer, count: n)
        }
        return data
    }
}

private func pingJSON(state: String = "idle", error: String? = nil) -> Data {
    let errorField = error.map { #","error":"\#($0)""# } ?? ""
    return Data(#"{"name":"r2provisioning","version":"0.0.1","id":"A1B2C3","maxDataSize":16,"state":"\#(state)"\#(errorField)}"#.utf8)
}

/// Simulates the device: POST /provision → 202 and "joining"; subsequent pings
/// report `outcome` (nil = keep "joining" forever).
private func deviceStub(outcome: String?, error: String? = nil, dropPingsWhileJoining: Int = 0)
    -> @Sendable (String) -> (status: Int, body: Data)?
{
    let state = Box("idle")
    let drops = Box(dropPingsWhileJoining)
    return { path in
        switch path {
        case "/provision":
            state.value = "joining"
            return (202, Data("joining".utf8))
        case "/ping":
            if state.value == "joining" {
                if drops.value > 0 { drops.value -= 1; return nil } // phone briefly off the hotspot
                if let outcome { state.value = outcome }
            }
            return (200, pingJSON(state: state.value, error: state.value == "failed" ? error : nil))
        default:
            return nil
        }
    }
}

@MainActor
private func waitUntil(timeout: TimeInterval = 2, _ condition: () -> Bool) async -> Bool {
    let deadline = Date().addingTimeInterval(timeout)
    while Date() < deadline {
        if condition() { return true }
        try? await Task.sleep(nanoseconds: 5_000_000)
    }
    return condition()
}

@Suite(.serialized) @MainActor struct ClientTests {
    @Test func pingSuccessSetsConnected() async {
        StubURLProtocol.reset { $0 == "/ping" ? (200, pingJSON()) : nil }
        let client = R2ProvisioningClient(pollInterval: 0.01, session: StubURLProtocol.session())
        client.start()
        #expect(await waitUntil { client.isConnected })
        #expect(client.deviceInfo?.id == "A1B2C3")
        #expect(client.deviceInfo?.maxDataSize == 16)
        client.stop()
        #expect(!client.isConnected)
    }

    @Test func pingFailureClearsConnected() async {
        let reachable = Flag(true)
        StubURLProtocol.reset { path in
            path == "/ping" && reachable.value ? (200, pingJSON()) : nil
        }
        let client = R2ProvisioningClient(pollInterval: 0.01, session: StubURLProtocol.session())
        client.start()
        #expect(await waitUntil { client.isConnected })
        reachable.value = false
        #expect(await waitUntil { !client.isConnected })
        client.stop()
    }

    @Test func doesNotPollUntilStarted() async throws {
        StubURLProtocol.reset { _ in (200, pingJSON()) }
        let client = R2ProvisioningClient(pollInterval: 0.01, session: StubURLProtocol.session())
        try await Task.sleep(nanoseconds: 50_000_000)
        #expect(StubURLProtocol.requests(to: "/ping").isEmpty)
        #expect(!client.isConnected)
    }

    @Test func sendSuccessSetsProvisionedAndStopsPolling() async throws {
        StubURLProtocol.reset(deviceStub(outcome: "joined", dropPingsWhileJoining: 2))
        let client = R2ProvisioningClient(pollInterval: 0.01, session: StubURLProtocol.session())
        client.start()
        #expect(await waitUntil { client.isConnected })

        try await client.send(ssid: "home", password: "pw", data: Data([0, 1]))
        #expect(client.isProvisioned)
        #expect(client.deviceInfo?.state == .joined)
        #expect(!client.isConnected)
        #expect(!client.isStarted)

        let sent = StubURLProtocol.requests(to: "/provision")
        #expect(sent.count == 1)
        let expected = bytes([4], Array("home".utf8), [2], Array("pw".utf8), [2, 0, 0, 1])
        #expect(sent.first?.body.map { Array($0) } == expected)

        let pingsAfterSend = StubURLProtocol.requests(to: "/ping").count
        try await Task.sleep(nanoseconds: 100_000_000)
        #expect(StubURLProtocol.requests(to: "/ping").count <= pingsAfterSend + 1) // at most one in flight
        #expect(!client.isConnected)

        client.start() // no-op once provisioned
        #expect(!client.isStarted)
    }

    @Test func sendReportsJoinFailure() async {
        StubURLProtocol.reset(deviceStub(outcome: "failed", error: "connect_failed"))
        let client = R2ProvisioningClient(pollInterval: 0.01, session: StubURLProtocol.session())
        client.start()
        do {
            try await client.send(ssid: "home", password: "wrong", data: Data())
            Issue.record("expected an error")
        } catch {
            guard case .joinFailed(reason: "connect_failed") = error else {
                Issue.record("unexpected error \(error)")
                return
            }
        }
        #expect(!client.isProvisioned)
        #expect(client.isStarted) // keeps polling so the app can retry
        client.stop()
    }

    @Test func sendTimesOutWithoutJoinResult() async {
        StubURLProtocol.reset(deviceStub(outcome: nil))
        let client = R2ProvisioningClient(pollInterval: 0.01, joinTimeout: 0.1, session: StubURLProtocol.session())
        do {
            try await client.send(ssid: "home", password: "pw", data: Data())
            Issue.record("expected an error")
        } catch {
            guard case .joinTimeout = error else {
                Issue.record("unexpected error \(error)")
                return
            }
        }
        #expect(!client.isProvisioned)
    }

    @Test func sendMapsHTTPError() async {
        StubURLProtocol.reset { $0 == "/provision" ? (413, Data()) : nil }
        let client = R2ProvisioningClient(session: StubURLProtocol.session())
        do {
            try await client.send(ssid: "home", password: "", data: Data())
            Issue.record("expected an error")
        } catch {
            guard case .httpStatus(413) = error else {
                Issue.record("unexpected error \(error)")
                return
            }
        }
        #expect(!client.isProvisioned)
    }

    @Test func sendUsesDeviceMaxDataSize() async {
        StubURLProtocol.reset { $0 == "/ping" ? (200, pingJSON()) : nil }
        let client = R2ProvisioningClient(pollInterval: 0.01, session: StubURLProtocol.session())
        client.start()
        #expect(await waitUntil { client.deviceInfo != nil })
        do {
            try await client.send(ssid: "home", password: "", data: Data(count: 17))
            Issue.record("expected an error")
        } catch {
            guard case .dataTooLarge(max: 16) = error else {
                Issue.record("unexpected error \(error)")
                return
            }
        }
        #expect(StubURLProtocol.requests(to: "/provision").isEmpty)
        client.stop()
    }

    @Test func sendTransportFailure() async {
        StubURLProtocol.reset { _ in nil }
        let client = R2ProvisioningClient(session: StubURLProtocol.session())
        do {
            try await client.send(ssid: "home", password: "", data: Data())
            Issue.record("expected an error")
        } catch {
            guard case .transport = error else {
                Issue.record("unexpected error \(error)")
                return
            }
        }
    }
}

final class Box<T: Sendable>: @unchecked Sendable {
    private let lock = NSLock()
    private var _value: T
    init(_ value: T) { _value = value }
    var value: T {
        get { lock.lock(); defer { lock.unlock() }; return _value }
        set { lock.lock(); _value = newValue; lock.unlock() }
    }
}

final class Flag: @unchecked Sendable {
    private let lock = NSLock()
    private var _value: Bool
    init(_ value: Bool) { _value = value }
    var value: Bool {
        get { lock.lock(); defer { lock.unlock() }; return _value }
        set { lock.lock(); _value = newValue; lock.unlock() }
    }
}
