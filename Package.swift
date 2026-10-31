// swift-tools-version:6.0
// The render library (src/render/) as a SwiftPM package for the Remote app's strip
// preview. See docs/rendering.md.
import PackageDescription

let package = Package(
    name: "ShinerRender",
    platforms: [.iOS(.v17), .macOS(.v14), .visionOS(.v2)],
    products: [
        .library(name: "ShinerRender", targets: ["ShinerRender"]),
    ],
    targets: [
        .target(
            name: "ShinerRender",
            path: "src/render",
            publicHeadersPath: "include",
            cxxSettings: [.headerSearchPath("host")]
        ),
    ],
    cxxLanguageStandard: .cxx11
)
