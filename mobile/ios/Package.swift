// swift-tools-version: 5.9
import PackageDescription
let package = Package(
    name: "CPULearningPhone",
    platforms: [.iOS(.v17), .macOS(.v13)],
    products: [.library(name: "PhoneCore", targets: ["PhoneCore"]), .executable(name: "phone-sync", targets: ["PhoneSync"])],
    targets: [
        .target(name: "CPUBridge", publicHeadersPath: "include"),
        .target(name: "PhoneCore", dependencies: ["CPUBridge"]),
        .executableTarget(name: "PhoneSync", dependencies: ["PhoneCore"]),
        .testTarget(name: "PhoneCoreTests", dependencies: ["PhoneCore", "CPUBridge"])
    ],
    cxxLanguageStandard: .cxx20
)
