// swift-tools-version: 6.2
// The swift-tools-version declares the minimum version of Swift required to build this package.

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
            name: "r2provisioning"),
        .testTarget(
            name: "r2provisioningTests",
            dependencies: ["r2provisioning"]),
    ]
)
