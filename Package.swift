// swift-tools-version: 6.2
// Root manifest so the iOS client SDK can be consumed by repository URL
// (SwiftPM only looks for Package.swift at the repository root). It mirrors
// client-sdks/r2provisioning-ios/Package.swift, which stays the manifest for
// working on the SDK and its example app; keep the two in step.
import PackageDescription

let package = Package(
    name: "r2provisioning",
    platforms: [
        .macOS(.v26), .iOS(.v15),
    ],
    products: [
        .library(
            name: "r2provisioning",
            targets: ["r2provisioning"]),
    ],
    targets: [
        .target(
            name: "r2provisioning",
            path: "client-sdks/r2provisioning-ios/Sources/r2provisioning"),
        .testTarget(
            name: "r2provisioningTests",
            dependencies: ["r2provisioning"],
            path: "client-sdks/r2provisioning-ios/Tests/r2provisioningTests"),
    ]
)
