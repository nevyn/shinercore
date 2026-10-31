import AppKit
import Foundation
import SwiftUI
import Testing
@testable import FlasherKit
@testable import ShinerCoreFlasher

/// A source with fixed builds and no network.
struct FixedSource: FirmwareSource {
    var snapshot: CatalogSnapshot
    func refresh() async -> CatalogSnapshot { snapshot }
    func pieces(for build: FirmwareBuild, board: Board) async throws -> [URL] {
        board.parts.map { URL(fileURLWithPath: "/c/\($0.file)") }
    }
}

enum States {
    static let now = Date(timeIntervalSince1970: 1_793_469_600)  // 2026-10-31
    static let latest = Fixture.manifest
    static let builds = [latest] + (1...3).map { Fixture.olderBuild(commit: "\($0)\($0)\($0)\($0)\($0)\($0)\($0)000000000000000000000000000000000", daysEarlier: Double($0)) }
    static let s3Port = SerialDevice(path: "/dev/cu.usbmodem1101", vendorID: 0x303A, productID: 0x1001, productName: "M5STACK_ATOMS3", serialNumber: "DC5475C8E944")
    static let atomPort = SerialDevice(path: "/dev/cu.usbserial-5552B1C9E1", vendorID: 0x1A86, productID: 0x55D4, productName: "USB Single Serial")
    static let s3Chip = ChipInfo(chip: "esp32s3", description: "ESP32-S3 (QFN56) (revision v0.2)", mac: "e8:f6:0a:98:1d:d8")
    static let atomChip = ChipInfo(chip: "esp32", description: "ESP32-PICO-D4 (revision v1.1)", mac: "4c:75:25:a1:b2:c3")

    @MainActor static func model(freshness: Freshness = .online(checkedAt: now.addingTimeInterval(-120)), update: FlasherRelease? = nil,
                      runner: EsptoolRunner = ReplayRunner([]), memory: BoardMemory = InMemoryBoardMemory()) -> FlasherModel {
        let snapshot = CatalogSnapshot(builds: builds, freshness: freshness, flasherUpdate: update)
        let model = FlasherModel(source: FixedSource(snapshot: snapshot), flasher: CoreFlasher(runner: runner), memory: memory, now: { now })
        model.catalog = snapshot
        return model
    }

    @MainActor static func identified(_ chip: ChipInfo, _ status: FirmwareStatus, port: SerialDevice = s3Port) -> FlasherModel {
        let model = model()
        model.devices = [port]
        model.selectedPath = port.path
        model.core = .identified(CoreIdentity(chip: chip, status: status))
        return model
    }

    @MainActor static var all: [(String, FlasherModel)] {
        let s3Board = latest.board(id: "atoms3-lite")!
        let empty = model()

        let identifying = model()
        identifying.devices = [s3Port]
        identifying.selectedPath = s3Port.path
        identifying.core = .identifying
        identifying.activity = .identifying

        let needsBoard = identified(atomChip, .noFirmware, port: atomPort)
        let upToDate = identified(s3Chip, .known(build: latest, board: s3Board, buildsBehind: 0))
        let behind = identified(s3Chip, .known(build: builds[2], board: s3Board, buildsBehind: 2))
        let unknown = identified(s3Chip, .unknown(fingerprint: String(repeating: "12", count: 32)))

        let flashing = identified(s3Chip, .known(build: builds[2], board: s3Board, buildsBehind: 2))
        flashing.activity = .flashing(progress: 0.4)
        flashing.log = ["Writing at 0x0004a000 [============>                 ]  40.0% 254.00kB/634.77kB [00:03] "]

        let failure = identified(s3Chip, .known(build: builds[2], board: s3Board, buildsBehind: 2))
        let error = OperationError(step: .flash, port: s3Port.path, failure: .portBusy, transcript: Fixture.text("failure-port-busy-handwritten.txt"))
        failure.failure = .init(message: error.localizedDescription, details: error.transcript)

        let offline = identified(s3Chip, .known(build: latest, board: s3Board, buildsBehind: 0))
        offline.catalog?.freshness = .offline(cachedAt: now.addingTimeInterval(-86400 * 2), reason: "The Internet connection appears to be offline.")

        let update = identified(s3Chip, .known(build: latest, board: s3Board, buildsBehind: 0))
        update.catalog?.flasherUpdate = FlasherRelease(version: "0.2.0", page: URL(string: "https://github.com/nevyn/shinercore/releases/tag/flasher-v0.2.0")!)

        return [("empty", empty), ("identifying", identifying), ("needs-board", needsBoard), ("up-to-date", upToDate),
                ("behind", behind), ("unknown-firmware", unknown), ("flashing-40", flashing), ("failure", failure),
                ("offline", offline), ("update-available", update)]
    }
}

@MainActor @Suite struct ModelTests {
    @Test func autoSelectsOnlyKnownVendors() {
        let model = States.model()
        model.devicesChanged([SerialDevice(path: "/dev/cu.usbmodem9", vendorID: 0x1D50, productID: 1, productName: "Keyboard")])
        #expect(model.selectedPath == nil)
        #expect(model.core == .none)
    }

    @Test func esp32NeedsABoardChoiceAndRemembersIt() throws {
        let memory = InMemoryBoardMemory()
        let model = States.identified(States.atomChip, .noFirmware, port: States.atomPort)
        let fresh = FlasherModel(source: model.source, flasher: model.flasher, memory: memory, now: { States.now })
        fresh.catalog = model.catalog
        fresh.devices = model.devices
        fresh.selectedPath = model.selectedPath
        fresh.core = model.core
        #expect(fresh.board == nil)
        #expect(!fresh.canFlash)
        #expect(fresh.boardSelection?.options.map(\.id) == ["atom-lite", "atom-echo", "stickc-plus"])
        fresh.chooseBoard("atom-echo")
        #expect(fresh.board?.id == "atom-echo")
        #expect(fresh.canFlash)
        #expect(memory.boards["4c:75:25:a1:b2:c3"] == "atom-echo")
    }

    @Test func confirmsFactoryResetAndBoardMismatch() throws {
        let echo = try #require(States.latest.board(id: "atom-echo"))
        let model = States.identified(States.atomChip, .known(build: States.latest, board: echo, buildsBehind: 0), port: States.atomPort)
        model.chooseBoard("atom-lite")
        model.factoryReset = true
        model.requestFlash()
        let confirmation = try #require(model.confirmation)
        #expect(confirmation.message.contains("Factory reset erases"))
        #expect(confirmation.message.contains("M5Atom Echo"))
        #expect(model.activity == .idle)
    }

    @Test func flashesThenVerifiesByIdentifyingAgain() async throws {
        let s3 = try #require(States.latest.board(id: "atoms3-lite"))
        let runner = ReplayRunner([
            .init(transcript: Fixture.text("write-flash-s3.txt")),
            .init(transcript: Fixture.text("identify-shinercore-s3.txt"), fingerprint: Data(hexString: s3.elfSha256)),
        ])
        let model = States.model(runner: runner)
        model.devices = [States.s3Port]
        model.selectedPath = States.s3Port.path
        model.core = .identified(CoreIdentity(chip: States.s3Chip, status: .unknown(fingerprint: "")))
        model.requestFlash()
        while model.isBusy { try await Task.sleep(for: .milliseconds(10)) }
        #expect(model.failure == nil)
        #expect(model.notice == "Done. The core runs firmware build 1ac850e for M5AtomS3 Lite.")
        #expect(runner.invocations.count == 2)
        #expect(runner.invocations[0].contains("write-flash"))
        #expect(!runner.invocations[0].contains("--erase-all"))
    }
}

/// Renders every main window state to /tmp/flasher-shots/<state>.png. Set FLASHER_SHOTS=1 to run.
/// Uses NSHostingView rather than ImageRenderer, which draws AppKit-backed controls (pickers,
/// toggles, progress bars) as placeholders.
@MainActor @Suite struct Screenshots {
    @Test(.enabled(if: ProcessInfo.processInfo.environment["FLASHER_SHOTS"] != nil))
    func renderMainStates() throws {
        _ = NSApplication.shared
        let directory = URL(fileURLWithPath: "/tmp/flasher-shots", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        for (name, model) in States.all {
            let view = NSHostingView(rootView: ContentView(model: model))
            view.frame.size = view.fittingSize
            let window = NSWindow(contentRect: view.frame, styleMask: [.titled], backing: .buffered, defer: false)
            window.contentView = view
            view.layoutSubtreeIfNeeded()
            let bitmap = try #require(view.bitmapImageRepForCachingDisplay(in: view.bounds), "rendering \(name)")
            view.cacheDisplay(in: view.bounds, to: bitmap)
            let png = try #require(bitmap.representation(using: .png, properties: [:]))
            try png.write(to: directory.appending(path: "\(name).png"))
        }
    }
}
