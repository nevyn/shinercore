import FlasherKit
import Foundation
import Observation

/// Everything the window shows, and the actions behind its controls. One operation runs at a time.
@MainActor @Observable
final class FlasherModel {
    enum CoreState: Equatable {
        case none
        case identifying
        case identified(CoreIdentity)
        case failed(OperationError)
    }

    enum Activity: Equatable {
        case idle
        case identifying
        case downloading
        case flashing(progress: Double)
    }

    struct Failure: Equatable {
        var message: String
        var details: String
    }

    struct Confirmation: Equatable {
        var title: String
        var message: String
    }

    // Dependencies
    @ObservationIgnored let source: FirmwareSource
    @ObservationIgnored let flasher: CoreFlasher
    @ObservationIgnored let memory: BoardMemory
    @ObservationIgnored let now: () -> Date
    @ObservationIgnored private var task: Task<Void, Never>?

    // State
    var devices: [SerialDevice] = []
    var selectedPath: String?
    var catalog: CatalogSnapshot?
    var selectedBuildID: String?
    var core: CoreState = .none
    var chosenBoardID: String?
    var factoryReset = false
    var activity: Activity = .idle
    var notice: String?
    var failure: Failure?
    var confirmation: Confirmation?
    var cacheWarning: String?
    var log: [String] = []

    init(source: FirmwareSource, flasher: CoreFlasher, memory: BoardMemory, now: @escaping () -> Date = Date.init) {
        self.source = source
        self.flasher = flasher
        self.memory = memory
        self.now = now
    }

    // MARK: Derived state

    var isBusy: Bool { activity != .idle }
    var selectedDevice: SerialDevice? { devices.first { $0.path == selectedPath } }
    var builds: [FirmwareBuild] { catalog?.builds ?? [] }
    var selectedBuild: FirmwareBuild? { builds.first { $0.id == selectedBuildID } ?? builds.first }

    var identity: CoreIdentity? {
        if case .identified(let identity) = core { identity } else { nil }
    }

    var boardSelection: BoardSelection? {
        guard let identity, let build = selectedBuild else { return nil }
        return BoardSelection(chip: identity.chip.chip, status: identity.status,
                              remembered: memory.board(forMAC: identity.chip.mac), boards: build.boards)
    }

    var board: Board? {
        guard let selection = boardSelection else { return nil }
        return selection.options.first { $0.id == chosenBoardID } ?? selection.board
    }

    var canFlash: Bool { !isBusy && selectedDevice != nil && board != nil && selectedBuild != nil }

    // MARK: Devices

    func devicesChanged(_ devices: [SerialDevice]) {
        self.devices = devices
        if let selectedPath, !devices.contains(where: { $0.path == selectedPath }), !isBusy {
            self.selectedPath = nil
            resetCore()
        }
        // Only auto-identify ports from known USB-serial vendors; others may be keyboards or modems.
        if self.selectedPath == nil, let first = devices.first(where: \.isKnownVendor) {
            select(first.path)
        }
    }

    func select(_ path: String?) {
        guard path != selectedPath || core == .none else { return }
        selectedPath = path
        resetCore()
        if path != nil { identify() }
    }

    private func resetCore() {
        core = .none
        chosenBoardID = nil
        notice = nil
        failure = nil
    }

    // MARK: Catalog

    func refreshCatalog() {
        Task {
            let snapshot = await source.refresh()
            catalog = snapshot
            if case .identified(let identity) = core {
                // Re-judge the fingerprint against the builds that just arrived.
                core = .identified(CoreIdentity(chip: identity.chip, status: identity.status.rejudged(in: snapshot.builds)))
            }
            guard case .online = snapshot.freshness, let latest = snapshot.latest else { return }
            do {
                try await source.cacheAllBoards(of: latest)
                cacheWarning = nil
            } catch {
                cacheWarning = "Couldn't save firmware build \(latest.shortCommit) for offline use: \(error.localizedDescription)"
            }
        }
    }

    // MARK: Operations

    func identify() {
        guard let port = selectedPath, !isBusy else { return }
        start(.identifying) { model in
            model.core = .identifying
            do {
                let identity = try await model.flasher.identify(port: port, builds: model.builds, onLine: model.logLine)
                model.identified(identity)
            } catch let error as OperationError {
                model.core = .failed(error)
                model.failure = Failure(message: error.localizedDescription, details: error.transcript)
            } catch {
                model.core = .none
            }
        }
    }

    private func identified(_ identity: CoreIdentity) {
        core = .identified(identity)
        if let board = identity.status.board {
            memory.remember(board: board.id, forMAC: identity.chip.mac)
        }
    }

    func chooseBoard(_ id: String) {
        chosenBoardID = id
        if let mac = identity?.chip.mac { memory.remember(board: id, forMAC: mac) }
    }

    /// Flashes right away, or first asks for confirmation when the flash erases settings or
    /// targets a different board than the core's firmware.
    func requestFlash() {
        guard canFlash, let board else { return }
        var reasons: [String] = []
        if factoryReset {
            reasons.append("Factory reset erases the presets, wiring and settings stored on the core.")
        }
        if let running = identity?.status.board, running.id != board.id {
            reasons.append("The core runs firmware for \(running.name), but you're about to flash firmware for \(board.name). The wrong firmware won't light up the LEDs.")
        }
        if reasons.isEmpty {
            flash()
        } else {
            confirmation = Confirmation(title: "Flash \(board.name) firmware?", message: reasons.joined(separator: "\n\n"))
        }
    }

    func flash() {
        confirmation = nil
        guard canFlash, let port = selectedPath, let board, let build = selectedBuild else { return }
        let eraseAll = factoryReset
        start(.downloading) { model in
            do {
                let files = try await model.source.pieces(for: build, board: board)
                model.activity = .flashing(progress: 0)
                try await model.flasher.flash(
                    port: port, board: board, files: files, eraseAll: eraseAll,
                    onLine: model.logLine,
                    onProgress: { value in DispatchQueue.main.async { model.setProgress(value) } },
                    onRetry: { baud in DispatchQueue.main.async { model.retrying(baud: baud) } })
            } catch let error as OperationError {
                model.failure = Failure(message: error.localizedDescription, details: error.transcript)
                return
            } catch is CancellationError {
                model.notice = "Flashing was cancelled. The core won't start until it is flashed completely; flash it again."
                return
            } catch {
                model.failure = Failure(message: "Couldn't get firmware build \(build.shortCommit) ready: \(error.localizedDescription)",
                                        details: String(describing: error))
                return
            }
            model.factoryReset = false
            model.activity = .identifying
            model.core = .identifying
            do {
                let identity = try await model.flasher.identify(port: port, builds: model.builds, onLine: model.logLine)
                model.identified(identity)
                if case .known(let running, let runningBoard, _) = identity.status, running.id == build.id, runningBoard.id == board.id {
                    model.notice = "Done. The core runs firmware build \(build.shortCommit) for \(board.name)."
                } else {
                    model.failure = Failure(message: "Flashing finished, but the core doesn't report firmware build \(build.shortCommit) for \(board.name). Try flashing again.",
                                            details: model.log.joined(separator: "\n"))
                }
            } catch let error as OperationError {
                model.core = .failed(error)
                model.failure = Failure(message: "Flashing finished, but checking the result failed. \(error.localizedDescription)", details: error.transcript)
            } catch {}
        }
    }

    func cancel() { task?.cancel() }

    private func start(_ activity: Activity, _ body: @escaping (FlasherModel) async -> Void) {
        self.activity = activity
        notice = nil
        failure = nil
        log.removeAll()
        task = Task {
            await body(self)
            self.activity = .idle
        }
    }

    private func setProgress(_ value: Double) {
        if case .flashing = activity { activity = .flashing(progress: value) }
    }

    private func retrying(baud: Int) {
        notice = "Flashing at full speed failed, so the flasher is trying again at \(baud) baud."
        activity = .flashing(progress: 0)
    }

    private nonisolated var logLine: @Sendable (String) -> Void {
        { [weak self] line in DispatchQueue.main.async { self?.log.append(line) } }
    }
}

extension FirmwareStatus {
    func rejudged(in builds: [FirmwareBuild]) -> FirmwareStatus {
        switch self {
        case .noFirmware: return .noFirmware
        case .known(_, let board, _): return FirmwareStatus(fingerprint: Data(hex: board.elfSha256), builds: builds)
        case .unknown(let fingerprint): return FirmwareStatus(fingerprint: Data(hex: fingerprint), builds: builds)
        }
    }
}

extension Data {
    init(hex: String) {
        var bytes: [UInt8] = []
        var index = hex.startIndex
        while let next = hex.index(index, offsetBy: 2, limitedBy: hex.endIndex), index < hex.endIndex {
            bytes.append(UInt8(hex[index..<next], radix: 16) ?? 0)
            index = next
        }
        self.init(bytes)
    }
}
