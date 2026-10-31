import Foundation
import Testing
@testable import FlasherKit

/// Flashes a real AtomS3 Lite with the esptool bundled in dist/ShinerCoreFlasher.app (run
/// scripts/build-app.sh first) and the firmware build in /tmp/shinerfw-dist.
/// Run with FLASHER_HW_PORT=/dev/cu.usbmodemXXXX.
@Suite(.enabled(if: ProcessInfo.processInfo.environment["FLASHER_HW_PORT"] != nil), .serialized)
struct Hardware {
    let port = ProcessInfo.processInfo.environment["FLASHER_HW_PORT"] ?? ""
    let app = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appending(path: "../../dist/ShinerCoreFlasher.app").standardized

    @Test func identifyFlashAndIdentifyAgain() async throws {
        let bundle = try #require(Bundle(url: app), "\(app.path) is missing; run scripts/build-app.sh")
        let flasher = CoreFlasher(runner: ProcessEsptoolRunner.bundled(in: bundle))
        let source = LocalDirectoryCatalog(directory: URL(fileURLWithPath: "/tmp/shinerfw-dist", isDirectory: true))
        let build = try #require(await source.refresh().latest, "/tmp/shinerfw-dist holds no firmware build")
        let board = try #require(build.board(id: "atoms3-lite"))

        let before = try await flasher.identify(port: port, builds: [build])
        #expect(before.chip.chip == "esp32s3")

        let progress = Locked<[Double]>([])
        try await flasher.flash(port: port, board: board, files: try await source.pieces(for: build, board: board), eraseAll: false,
                                onProgress: { value in progress.mutate { $0.append(value) } })
        #expect(progress.value == progress.value.sorted())
        #expect(progress.value.last == 1)

        let after = try await flasher.identify(port: port, builds: [build])
        #expect(after.chip.mac == before.chip.mac)
        #expect(after.status == .known(build: build, board: board, buildsBehind: 0))
    }
}
