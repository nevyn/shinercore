import AppKit
import FlasherKit
import SwiftUI

/// Launch arguments:
/// * `--firmware-dir DIR`: use the single firmware build in DIR (as `ci/build-firmware.sh` writes
///   it) instead of GitHub.
/// * `--background-window`: never take focus; park a small window behind others in the
///   bottom-right corner. For running test builds while someone works on the same Mac.
@main
struct ShinerCoreFlasherApp: App {
    @NSApplicationDelegateAdaptor private var delegate: AppDelegate
    @State private var model = FlasherApp.makeModel()

    var body: some Scene {
        Window("ShinerCore Flasher", id: "main") {
            ContentView(model: model)
                .task {
                    model.refreshCatalog()
                    FlasherApp.monitor = SerialPortMonitor { [model] devices in model.devicesChanged(devices) }
                }
        }
        .windowResizability(.contentSize)
    }
}

@MainActor
enum FlasherApp {
    static var monitor: SerialPortMonitor?
    static let arguments = ProcessInfo.processInfo.arguments
    static var backgroundWindow: Bool { arguments.contains("--background-window") }
    static var version: String { Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.0.0" }

    static func makeModel() -> FlasherModel {
        let source: FirmwareSource
        if let index = arguments.firstIndex(of: "--firmware-dir"), arguments.indices.contains(index + 1) {
            source = LocalDirectoryCatalog(directory: URL(fileURLWithPath: arguments[index + 1], isDirectory: true))
        } else {
            let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            source = GitHubCatalog(http: URLSessionHTTPClient(userAgent: "ShinerCoreFlasher/\(version)"),
                                   cacheDirectory: support.appending(path: "ShinerCoreFlasher", directoryHint: .isDirectory),
                                   currentVersion: version)
        }
        return FlasherModel(source: source, flasher: CoreFlasher(runner: ProcessEsptoolRunner.bundled()),
                            memory: UserDefaultsBoardMemory())
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationWillFinishLaunching(_ notification: Notification) {
        if FlasherApp.backgroundWindow { NSApp.setActivationPolicy(.accessory) }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        guard FlasherApp.backgroundWindow else { return }
        // SwiftUI creates the window after launch finishes.
        DispatchQueue.main.async {
            guard let window = NSApp.windows.first(where: { $0.canBecomeMain }),
                  let screen = window.screen ?? NSScreen.main else { return }
            let size = window.frame.size
            let visible = screen.visibleFrame
            window.setFrameOrigin(NSPoint(x: visible.maxX - size.width, y: visible.minY))
            window.orderBack(nil)
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
