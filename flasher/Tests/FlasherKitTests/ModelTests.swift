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
    static let s3Port = SerialDevice(path: "/dev/cu.usbmodem1401", vendorID: 0x303A, productID: 0x1001,
                                     productName: "USB JTAG/serial debug unit", serialNumber: "E8:F6:0A:98:1D:D8")
    static let atomPort = SerialDevice(path: "/dev/cu.usbserial-5552B1C9E1", vendorID: 0x1A86, productID: 0x55D4, productName: "USB Single Serial")
    static let keyboard = SerialDevice(path: "/dev/cu.usbmodem1212201", vendorID: 0x37D1, productID: 0x64, productName: "Naya Create Left")
    static let s3Chip = ChipInfo(chip: "esp32s3", description: "ESP32-S3 (QFN56) (revision v0.2)", mac: "e8:f6:0a:98:1d:d8")
    static let atomChip = ChipInfo(chip: "esp32", description: "ESP32-PICO-D4 (revision v1.1)", mac: "4c:75:25:a1:b2:c3")
    static let s3Board = latest.board(id: "atoms3-lite")!
    static let atomBoard = latest.board(id: "atom-lite")!

    @MainActor static func model(runner: EsptoolRunner = ReplayRunner([]), memory: BoardMemory = InMemoryBoardMemory()) -> FlasherModel {
        let snapshot = CatalogSnapshot(builds: builds, freshness: .online(checkedAt: now.addingTimeInterval(-120)))
        let model = FlasherModel(source: FixedSource(snapshot: snapshot), flasher: CoreFlasher(runner: runner), memory: memory, now: { now })
        model.catalog = snapshot
        return model
    }

    /// Adds `device` without identifying it, in the given state.
    @MainActor @discardableResult
    static func add(_ device: SerialDevice, to model: FlasherModel, _ core: CoreSession.CoreState = .none) -> CoreSession {
        let session = CoreSession(device: device, app: model)
        session.core = core
        model.devices.append(device)
        model.sessions[device.path] = session
        return session
    }

    @MainActor static func detail(_ status: FirmwareStatus, chip: ChipInfo = s3Chip, port: SerialDevice = s3Port) -> (FlasherModel, CoreSession) {
        let model = model()
        let session = add(port, to: model, .identified(CoreIdentity(chip: chip, status: status)))
        return (model, session)
    }

    @MainActor static var lists: [(String, FlasherModel)] {
        let welcome = model()

        let two = model()
        add(s3Port, to: two, .identified(CoreIdentity(chip: s3Chip, status: .known(build: latest, board: s3Board, buildsBehind: 0))))
        add(atomPort, to: two, .identified(CoreIdentity(chip: atomChip, status: .known(build: builds[2], board: atomBoard, buildsBehind: 2))))
        add(keyboard, to: two)

        let empty = model()
        add(keyboard, to: empty)
        empty.otherDevicesExpanded = true

        return [("welcome", welcome), ("picker-two-cores", two), ("picker-empty-other-devices", empty)]
    }

    @MainActor static var details: [(String, FlasherModel, CoreSession)] {
        var result: [(String, FlasherModel, CoreSession)] = []
        func add(_ name: String, _ pair: (FlasherModel, CoreSession), _ configure: (CoreSession) -> Void = { _ in }) {
            configure(pair.1)
            result.append((name, pair.0, pair.1))
        }
        add("detail-install", detail(.unknown(fingerprint: String(repeating: "12", count: 32))))
        add("detail-needs-board", detail(.noFirmware, chip: atomChip, port: atomPort))
        add("detail-update", detail(.known(build: builds[2], board: s3Board, buildsBehind: 2)))
        add("detail-up-to-date", detail(.known(build: latest, board: s3Board, buildsBehind: 0)))
        add("detail-working-40", detail(.known(build: builds[2], board: s3Board, buildsBehind: 2))) { $0.activity = .flashing(progress: 0.4) }
        add("detail-success", detail(.known(build: latest, board: s3Board, buildsBehind: 0))) { $0.installed = latest }
        add("detail-error", detail(.known(build: builds[2], board: s3Board, buildsBehind: 2))) { session in
            let error = OperationError(step: .flash, port: s3Port.path, failure: .portBusy, transcript: Fixture.text("failure-port-busy-handwritten.txt"))
            session.failure = .init(title: "Installing didn't work.", hint: error.failure.message(port: error.port), details: error.transcript)
            session.detailsExpanded = true
        }
        add("detail-advanced", detail(.known(build: latest, board: s3Board, buildsBehind: 0))) { session in
            session.advancedExpanded = true
            session.log = Fixture.text("identify-shinercore-s3.txt").split(whereSeparator: \.isNewline).map(String.init)
        }
        return result
    }
}

@MainActor @Suite struct ModelTests {
    @Test func identifiesOnlyKnownVendorsAndDropsUnpluggedDevices() async throws {
        let runner = ReplayRunner([.init(transcript: Fixture.text("identify-blank-s3.txt"), fingerprint: Data(repeating: 0x12, count: 32))])
        let model = States.model(runner: runner)
        model.devicesChanged([States.keyboard, States.s3Port])
        let s3 = try #require(model.sessions[States.s3Port.path])
        while s3.isBusy { try await Task.sleep(for: .milliseconds(10)) }
        #expect(runner.invocations.count == 1)
        #expect(runner.invocations[0].contains(States.s3Port.path))
        #expect(model.sessions[States.keyboard.path]?.core == CoreSession.CoreState.none)
        #expect(model.cores.map(\.id) == [States.s3Port.path])
        #expect(model.otherDevices.map(\.id) == [States.keyboard.path])
        #expect(s3.summary == "Something else is installed")
        #expect(s3.primary == .install)

        model.open(s3)
        model.devicesChanged([States.keyboard])
        #expect(model.sessions[States.s3Port.path] == nil)
        #expect(model.path.isEmpty)
    }

    @Test func identifiesAgainWhenAPortReturnsAfterVanishingMidOperation() async throws {
        let (released, release) = AsyncStream.makeStream(of: Void.self)
        let blank = ReplayRunner.Step(transcript: Fixture.text("identify-blank-s3.txt"), fingerprint: Data(repeating: 0x12, count: 32))
        var held = blank
        held.gate = { for await _ in released { return } }
        let runner = ReplayRunner([held, blank])
        let model = States.model(runner: runner)
        model.devicesChanged([States.s3Port])
        let session = try #require(model.sessions[States.s3Port.path])
        model.devicesChanged([])
        model.devicesChanged([States.s3Port])
        #expect(model.sessions[States.s3Port.path] === session)
        release.yield()
        while runner.invocations.count < 2 || session.isBusy { try await Task.sleep(for: .milliseconds(10)) }
        #expect(session.summary == "Something else is installed")
    }

    @Test func dropsASessionWhosePortIsStillGoneWhenItsOperationEnds() async throws {
        let (released, release) = AsyncStream.makeStream(of: Void.self)
        var held = ReplayRunner.Step(transcript: Fixture.text("identify-blank-s3.txt"), fingerprint: Data(repeating: 0x12, count: 32))
        held.gate = { for await _ in released { return } }
        let model = States.model(runner: ReplayRunner([held]))
        model.devicesChanged([States.s3Port])
        let session = try #require(model.sessions[States.s3Port.path])
        model.devicesChanged([])
        #expect(model.sessions[States.s3Port.path] === session)
        release.yield()
        while session.isBusy { try await Task.sleep(for: .milliseconds(10)) }
        #expect(model.sessions[States.s3Port.path] == nil)
    }

    @Test func judgesTheFingerprintAgainWhenTheCatalogArrivesLate() async throws {
        let memory = InMemoryBoardMemory()
        let runner = ReplayRunner([.init(transcript: Fixture.text("identify-shinercore-s3.txt"), fingerprint: Data(hexString: States.s3Board.elfSha256))])
        let model = States.model(runner: runner, memory: memory)
        let catalog = model.catalog
        model.catalog = nil
        model.devicesChanged([States.s3Port])
        let session = try #require(model.sessions[States.s3Port.path])
        while session.isBusy { try await Task.sleep(for: .milliseconds(10)) }
        #expect(session.summary == "Something else is installed")
        model.catalog = catalog
        session.rejudge()
        #expect(session.summary == "ShinerCore, up to date")
        #expect(memory.boards["e8:f6:0a:98:1d:d8"] == "atoms3-lite")
    }

    @Test func esp32NeedsABoardChoiceAndRemembersIt() throws {
        let memory = InMemoryBoardMemory()
        let model = States.model(memory: memory)
        let session = States.add(States.atomPort, to: model, .identified(CoreIdentity(chip: States.atomChip, status: .noFirmware)))
        #expect(session.primary == .chooseBoard)
        #expect(!session.canFlash)
        #expect(session.boardSelection?.options.map(\.id) == ["atom-lite", "atom-echo", "stickc-plus"])
        session.chooseBoard("atom-echo")
        #expect(session.primary == .install)
        #expect(model.title(for: session) == "M5Atom Echo")
        #expect(memory.boards["4c:75:25:a1:b2:c3"] == "atom-echo")
    }

    @Test func primaryActionFollowsTheRunningBuild() {
        #expect(States.detail(.known(build: States.latest, board: States.s3Board, buildsBehind: 0)).1.primary == .upToDate)
        #expect(States.detail(.known(build: States.builds[1], board: States.s3Board, buildsBehind: 1)).1.primary == .update)
        #expect(States.detail(.noFirmware).1.primary == .install)
        let (_, older) = States.detail(.known(build: States.latest, board: States.s3Board, buildsBehind: 0))
        older.selectedBuildID = States.builds[2].id
        #expect(older.primary == .install)
    }

    @Test func twinCoresGetTheEndOfTheirMAC() {
        let model = States.model()
        let a = States.add(States.s3Port, to: model, .identified(CoreIdentity(chip: States.s3Chip, status: .noFirmware)))
        var otherPort = States.s3Port
        otherPort.path = "/dev/cu.usbmodem1402"
        var otherChip = States.s3Chip
        otherChip.mac = "e8:f6:0a:98:aa:bb"
        let b = States.add(otherPort, to: model, .identified(CoreIdentity(chip: otherChip, status: .noFirmware)))
        #expect(model.title(for: a) == "M5AtomS3 Lite 1d:d8")
        #expect(model.title(for: b) == "M5AtomS3 Lite aa:bb")
    }

    @Test func confirmsFactoryResetAndBoardMismatch() throws {
        let echo = try #require(States.latest.board(id: "atom-echo"))
        let (_, session) = States.detail(.known(build: States.latest, board: echo, buildsBehind: 0), chip: States.atomChip, port: States.atomPort)
        session.chooseBoard("atom-lite")
        session.factoryReset = true
        session.requestFlash()
        let confirmation = try #require(session.confirmation)
        #expect(confirmation.message.contains("Factory reset also erases"))
        #expect(confirmation.message.contains("M5Atom Echo"))
        #expect(session.activity == .idle)
    }

    @Test func installsThenVerifiesByCheckingAgain() async throws {
        let runner = ReplayRunner([
            .init(transcript: Fixture.text("write-flash-s3.txt")),
            .init(transcript: Fixture.text("identify-shinercore-s3.txt"), fingerprint: Data(hexString: States.s3Board.elfSha256)),
        ])
        let model = States.model(runner: runner)
        let session = States.add(States.s3Port, to: model, .identified(CoreIdentity(chip: States.s3Chip, status: .unknown(fingerprint: ""))))
        session.requestFlash()
        while session.isBusy { try await Task.sleep(for: .milliseconds(10)) }
        #expect(session.failure == nil)
        #expect(session.primary == .installed(States.latest))
        #expect(runner.invocations.count == 2)
        #expect(runner.invocations[0].contains("write-flash"))
        #expect(!runner.invocations[0].contains("--erase-all"))
        session.installed = nil
        #expect(session.primary == .upToDate)
    }
}

/// Renders the main UI states, light and dark, to /tmp/flasher-shots/<state>-<appearance>.png.
/// Set FLASHER_SHOTS=1 to run. Uses NSHostingView rather than ImageRenderer, which draws
/// AppKit-backed controls (pickers, toggles, progress bars) as placeholders.
@MainActor @Suite struct Shots {
    @Test(.enabled(if: ProcessInfo.processInfo.environment["FLASHER_SHOTS"] != nil))
    func renderMainStates() throws {
        _ = NSApplication.shared
        let directory = URL(fileURLWithPath: "/tmp/flasher-shots", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        var views: [(String, AnyView)] = States.lists.map { ($0.0, AnyView(CoreListContent(model: $0.1))) }
        views += States.details.map { ($0.0, AnyView(CoreDetailContent(session: $0.2, title: $0.1.title(for: $0.2)))) }
        views.append(("how-it-works", AnyView(HowItWorksSheet(boards: BoardShop.boards(in: States.latest)))))
        for (name, view) in views {
            for (appearance, scheme) in [(NSAppearance.Name.aqua, ColorScheme.light), (.darkAqua, .dark)] {
                let root = view.frame(width: 520).background(Theme.page).tint(Theme.accent)
                    .environment(\.colorScheme, scheme).environment(\.controlActiveState, .key)
                let hosting = NSHostingView(rootView: root)
                hosting.frame.size = hosting.fittingSize
                let window = NSWindow(contentRect: hosting.frame, styleMask: [.titled], backing: .buffered, defer: false)
                window.appearance = NSAppearance(named: appearance)
                window.contentView = hosting
                hosting.layoutSubtreeIfNeeded()
                let size = hosting.bounds.size
                let bitmap = try #require(NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * 2), pixelsHigh: Int(size.height * 2),
                                                           bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                                           colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0), "rendering \(name)")
                bitmap.size = size
                hosting.cacheDisplay(in: hosting.bounds, to: bitmap)
                let png = try #require(bitmap.representation(using: .png, properties: [:]))
                try png.write(to: directory.appending(path: "\(name)-\(scheme == .light ? "light" : "dark").png"))
            }
        }
    }
}
