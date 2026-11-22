//
//  R2ProvisioningClient.swift
//  r2provisioning
//
//  Talks to an r2provisioning device in hotspot mode.
//
//  The app must join the device's hotspot ("r2-XXXXXX") itself, either in
//  Settings or with `NEHotspotConfiguration`. The app also needs these
//  Info.plist entries for plain HTTP on the local network:
//    NSAppTransportSecurity > NSAllowsLocalNetworking = YES
//    NSLocalNetworkUsageDescription = "<why>"
//

import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
#if canImport(Combine)
import Combine
#endif

@MainActor
public final class R2ProvisioningClient {
    /// True while the last ping succeeded. Pinging runs only between `start()` and
    /// `stop()`, and stops for good once `send` succeeds.
    public private(set) var isConnected = false {
        willSet { if newValue != isConnected { notifyChange() } }
    }

    /// True once the device has joined the provisioned network with the sent
    /// credentials and stored them. The device then restarts and leaves the hotspot.
    public private(set) var isProvisioned = false {
        willSet { if newValue != isProvisioned { notifyChange() } }
    }

    /// Info from the most recent successful ping.
    public private(set) var deviceInfo: R2DeviceInfo? {
        willSet { if newValue != deviceInfo { notifyChange() } }
    }

    /// True between `start()` and `stop()` (or a successful `send`).
    public var isStarted: Bool { pollTask != nil }

    public let baseURL: URL

    private let pollInterval: TimeInterval
    private let joinTimeout: TimeInterval
    private let session: URLSession
    private var pollTask: Task<Void, Never>?

    private static let pingTimeout: TimeInterval = 2
    private static let sendTimeout: TimeInterval = 10

    /// - Parameters:
    ///   - host: Device address. The ESP soft-AP default is 192.168.4.1.
    ///   - port: Provisioning HTTP port.
    ///   - pollInterval: Seconds between pings.
    ///   - joinTimeout: How long `send` waits for the device to report a join result.
    ///     It should exceed the firmware's `joinTimeoutMs` (default 20 s).
    public init(
        host: String = "192.168.4.1",
        port: Int = 80,
        pollInterval: TimeInterval = 1.0,
        joinTimeout: TimeInterval = 45,
        session: URLSession = .shared
    ) {
        var components = URLComponents()
        components.scheme = "http"
        components.host = host
        components.port = port
        guard let url = components.url else {
            preconditionFailure("Invalid host/port: \(host):\(port)")
        }
        self.baseURL = url
        self.pollInterval = pollInterval
        self.joinTimeout = joinTimeout
        self.session = session
    }

    /// Start pinging the device and updating `isConnected`. This does nothing if
    /// polling is already running or the device has already been provisioned.
    public func start() {
        guard pollTask == nil, !isProvisioned else { return }
        let interval = UInt64(max(pollInterval, 0.01) * 1_000_000_000)
        pollTask = Task { [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                let info = await self.ping()
                // A stop() or a successful send() may have happened while the ping was in flight.
                guard !Task.isCancelled, !self.isProvisioned else { return }
                if let info { self.deviceInfo = info }
                self.isConnected = info != nil
                try? await Task.sleep(nanoseconds: interval)
            }
        }
    }

    /// Stop pinging. Sets `isConnected` to false.
    public func stop() {
        pollTask?.cancel()
        pollTask = nil
        isConnected = false
    }

    /// Ping the device once. Returns its info, or nil if it is unreachable.
    public func ping() async -> R2DeviceInfo? {
        var request = URLRequest(url: baseURL.appendingPathComponent("ping"))
        request.timeoutInterval = Self.pingTimeout
        request.cachePolicy = .reloadIgnoringLocalCacheData
        guard
            let (data, response) = try? await session.data(for: request),
            (response as? HTTPURLResponse)?.statusCode == 200
        else {
            return nil
        }
        return try? JSONDecoder().decode(R2DeviceInfo.self, from: data)
    }

    /// Send WiFi credentials and custom data, then wait until the device has tried them.
    ///
    /// Returns once the device has joined the network and stored the credentials. The
    /// device then restarts, and this client stops polling. If the join fails, this throws
    /// `.joinFailed`; the device stays in hotspot mode and `send` can be retried.
    /// The phone may briefly lose the hotspot while the device joins, because the hotspot
    /// moves to the router's channel. Pings that fail during the wait are tolerated.
    /// - Parameters:
    ///   - password: Pass an empty string for an open network.
    ///   - data: Opaque bytes, kept by the device in EEPROM.
    public func send(ssid: String, password: String, data: Data = Data()) async throws(R2ProvisioningError) {
        let maxDataSize = deviceInfo?.maxDataSize ?? Int(UInt16.max)
        let body = try R2ProvisioningPayload.encode(
            ssid: ssid, password: password, data: data, maxDataSize: maxDataSize)

        var request = URLRequest(url: baseURL.appendingPathComponent("provision"))
        request.httpMethod = "POST"
        request.setValue("application/octet-stream", forHTTPHeaderField: "Content-Type")
        request.httpBody = body
        request.timeoutInterval = Self.sendTimeout

        let response: URLResponse
        do {
            (_, response) = try await session.data(for: request)
        } catch {
            throw .transport(error)
        }
        guard let http = response as? HTTPURLResponse else {
            throw .invalidResponse
        }
        guard http.statusCode == 202 || http.statusCode == 200 else {
            throw .httpStatus(http.statusCode)
        }

        try await waitForJoinResult()
        isProvisioned = true
        stop()
    }

    private func waitForJoinResult() async throws(R2ProvisioningError) {
        let deadline = Date().addingTimeInterval(joinTimeout)
        let interval = UInt64(max(pollInterval, 0.01) * 1_000_000_000)
        while Date() < deadline {
            if Task.isCancelled { throw .transport(CancellationError()) }
            if let info = await ping() {
                deviceInfo = info
                switch info.state {
                case .joined:
                    return
                case .failed:
                    throw .joinFailed(reason: info.error ?? "unknown")
                case .idle, .joining:
                    break
                }
            }
            do {
                try await Task.sleep(nanoseconds: interval)
            } catch {
                throw .transport(error)
            }
        }
        throw .joinTimeout
    }

    private func notifyChange() {
        #if canImport(Combine)
        objectWillChange.send()
        #endif
    }
}

#if canImport(Combine)
extension R2ProvisioningClient: ObservableObject {}
#endif
