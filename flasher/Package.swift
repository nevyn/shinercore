// swift-tools-version:6.0
import PackageDescription

let package = Package(
    name: "ShinerCoreFlasher",
    platforms: [.macOS(.v14)],
    targets: [
        .target(name: "FlasherKit",
                linkerSettings: [.linkedFramework("IOKit")]),
        .executableTarget(name: "ShinerCoreFlasher", dependencies: ["FlasherKit"]),
        .testTarget(
            name: "FlasherKitTests",
            dependencies: ["FlasherKit", "ShinerCoreFlasher"],
            resources: [.copy("Fixtures")]),
    ],
    swiftLanguageModes: [.v5]
)
