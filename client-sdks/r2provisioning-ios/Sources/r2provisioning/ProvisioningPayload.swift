//
//  ProvisioningPayload.swift
//  r2provisioning
//
//  Wire format of `POST /provision` (little-endian):
//    u8 ssidLen (1...32) | ssid | u8 pwLen (0...64) | password | u16 dataLen | data
//

import Foundation

public enum R2ProvisioningError: Error {
    /// SSID is empty, longer than 32 UTF-8 bytes, or contains NUL.
    case invalidSSID
    /// Password is longer than 64 UTF-8 bytes or contains NUL.
    case invalidPassword
    /// Custom data exceeds the device's `maxDataSize`.
    case dataTooLarge(max: Int)
    /// The device rejected the payload (400 malformed, 409 attempt already running, 413 too large).
    case httpStatus(Int)
    /// The device could not join the network with the given credentials. Nothing was
    /// stored and the device stays in hotspot mode, so `send` can be called again.
    /// `reason` is "ssid_not_found", "connect_failed", "timeout" or "storage_failed".
    case joinFailed(reason: String)
    /// The device did not report a join result in time.
    case joinTimeout
    /// The response was not an HTTP response.
    case invalidResponse
    /// Network failure (device not reachable, timeout, ...).
    case transport(any Error)
}

public enum R2ProvisioningPayload {
    public static let maxSSIDLength = 32
    public static let maxPasswordLength = 64

    /// Encode credentials and custom data as a `POST /provision` body.
    public static func encode(
        ssid: String,
        password: String,
        data: Data,
        maxDataSize: Int = Int(UInt16.max)
    ) throws(R2ProvisioningError) -> Data {
        let ssidBytes = Array(ssid.utf8)
        let passwordBytes = Array(password.utf8)
        guard !ssidBytes.isEmpty, ssidBytes.count <= maxSSIDLength, !ssidBytes.contains(0) else {
            throw .invalidSSID
        }
        guard passwordBytes.count <= maxPasswordLength, !passwordBytes.contains(0) else {
            throw .invalidPassword
        }
        let limit = min(maxDataSize, Int(UInt16.max))
        guard data.count <= limit else {
            throw .dataTooLarge(max: limit)
        }

        var out = Data(capacity: 4 + ssidBytes.count + passwordBytes.count + data.count)
        out.append(UInt8(ssidBytes.count))
        out.append(contentsOf: ssidBytes)
        out.append(UInt8(passwordBytes.count))
        out.append(contentsOf: passwordBytes)
        out.append(UInt8(data.count & 0xFF))
        out.append(UInt8(data.count >> 8))
        out.append(data)
        return out
    }
}

/// Progress of a provisioning attempt, as reported by the device.
public enum R2JoinState: String, Decodable, Sendable {
    /// Waiting for credentials.
    case idle
    /// Trying the received credentials.
    case joining
    /// Joined and stored; the device restarts shortly.
    case joined
    /// The last attempt failed; see `R2DeviceInfo.error`.
    case failed
}

/// Device information returned by `GET /ping`.
public struct R2DeviceInfo: Decodable, Equatable, Sendable {
    public let name: String
    public let version: String
    /// Last three MAC bytes as hex, e.g. "A1B2C3".
    public let id: String
    /// Largest custom data the device accepts, in bytes.
    public let maxDataSize: Int
    public let state: R2JoinState
    /// Failure reason when `state == .failed`.
    public let error: String?
}
