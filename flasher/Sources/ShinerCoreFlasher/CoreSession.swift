import FlasherKit
import Foundation
import Observation

/// One plugged-in serial device and what the flasher knows and does with it. One operation runs at a time.
@MainActor @Observable
final class CoreSession: Identifiable {
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
        case verifying
    }

    /// What the hero of the detail view offers.
    enum Primary: Equatable {
        case working(label: String, progress: Double?, cancellable: Bool)
        case installed(FirmwareBuild)
        case check
        case chooseBoard
        case install
        case update
        case upToDate
    }

    struct Failure: Equatable {
        /// A friendly sentence: what didn't work.
        var title: String
        /// What to do about it.
        var hint: String
        /// esptool's output.
        var details: String
    }

    struct Confirmation: Equatable {
        var title: String
        var message: String
    }

    let device: SerialDevice
    @ObservationIgnored let app: FlasherModel
    @ObservationIgnored private var task: Task<Void, Never>?

    var core: CoreState = .none
    var activity: Activity = .idle
    var chosenBoardID: String?
    var selectedBuildID: String?
    var factoryReset = false
    /// Set after a verified install, until the user dismisses the success state.
    var installed: FirmwareBuild?
    var notice: String?
    var failure: Failure?
    var confirmation: Confirmation?
    var log: [String] = []
    var advancedExpanded = false
    var detailsExpanded = false

    init(device: SerialDevice, app: FlasherModel) {
        self.device = device
        self.app = app
    }

    nonisolated var id: String { device.path }

    // MARK: Derived state

    var isBusy: Bool { activity != .idle }

    var identity: CoreIdentity? {
        if case .identified(let identity) = core { identity } else { nil }
    }

    var selectedBuild: FirmwareBuild? { app.builds.first { $0.id == selectedBuildID } ?? app.builds.first }

    var boardSelection: BoardSelection? {
        guard let identity, let build = selectedBuild else { return nil }
        return BoardSelection(chip: identity.chip.chip, status: identity.status,
                              remembered: app.memory.board(forMAC: identity.chip.mac), boards: build.boards)
    }

    var board: Board? {
        guard let selection = boardSelection else { return nil }
        return selection.options.first { $0.id == chosenBoardID } ?? selection.board
    }

    var canFlash: Bool { !isBusy && board != nil && selectedBuild != nil }

    /// The board name once known, else the USB product name.
    var baseTitle: String { board?.name ?? identity?.status.board?.name ?? device.productName ?? "Unknown core" }

    var primary: Primary {
        switch activity {
        case .identifying: return .working(label: "Checking the core", progress: nil, cancellable: false)
        case .downloading: return .working(label: "Downloading ShinerCore", progress: nil, cancellable: true)
        case .flashing(let progress) where progress == 0: return .working(label: "Restarting the core", progress: 0, cancellable: true)
        case .flashing(let progress): return .working(label: "Installing \(Int(progress * 100)) %", progress: progress, cancellable: true)
        case .verifying: return .working(label: "Checking", progress: 1, cancellable: false)
        case .idle: return idlePrimary
        }
    }

    /// What the hero offers once nothing is running.
    var idlePrimary: Primary {
        if let installed { return .installed(installed) }
        guard let identity else { return .check }
        guard let board, let build = selectedBuild else { return .chooseBoard }
        if case .known(let running, let runningBoard, _) = identity.status, runningBoard.id == board.id {
            if running.id == build.id { return .upToDate }
            if running.date < build.date { return .update }
        }
        return .install
    }

    /// One line for the core list.
    var summary: String {
        switch core {
        case .none: return "Not checked yet"
        case .identifying: return "Checking…"
        case .failed: return "Couldn't check this core"
        case .identified(let identity):
            switch identity.status {
            case .noFirmware: return "Empty"
            case .unknown: return "Something else is installed"
            case .known(_, _, 0): return "ShinerCore, up to date"
            case .known: return "ShinerCore, update available"
            }
        }
    }

    // MARK: Operations

    func identify() {
        guard !isBusy else { return }
        start(.identifying) { session in
            session.core = .identifying
            do {
                session.identified(try await session.app.flasher.identify(port: session.device.path, builds: session.app.builds,
                                                                           onLine: session.logLine))
            } catch let error as OperationError {
                session.core = .failed(error)
                session.failure = Failure(title: "Couldn't check this core.", hint: error.failure.message(port: error.port), details: error.transcript)
            } catch {
                session.core = .none
            }
        }
    }

    func rejudge(in builds: [FirmwareBuild]) {
        if let identity { core = .identified(CoreIdentity(chip: identity.chip, status: identity.status.rejudged(in: builds))) }
    }

    func chooseBoard(_ id: String) {
        chosenBoardID = id
        if let mac = identity?.chip.mac { app.memory.remember(board: id, forMAC: mac) }
    }

    /// Installs right away, or first asks when the install erases settings or targets a different
    /// board than the one whose firmware the core runs.
    func requestFlash() {
        guard canFlash, let board else { return }
        var reasons: [String] = []
        if factoryReset {
            reasons.append("Factory reset also erases the presets, wiring and settings stored on the core.")
        }
        if let running = identity?.status.board, running.id != board.id {
            reasons.append("This core runs ShinerCore for \(running.name), but you chose \(board.name). ShinerCore for the wrong model won't light the LEDs.")
        }
        if reasons.isEmpty {
            flash()
        } else {
            confirmation = Confirmation(title: "Install ShinerCore for \(board.name)?", message: reasons.joined(separator: "\n\n"))
        }
    }

    func flash() {
        confirmation = nil
        guard canFlash, let board, let build = selectedBuild else { return }
        let port = device.path
        let eraseAll = factoryReset
        start(.downloading) { session in
            do {
                let files = try await session.app.source.pieces(for: build, board: board)
                session.activity = .flashing(progress: 0)
                try await session.app.flasher.flash(
                    port: port, board: board, files: files, eraseAll: eraseAll,
                    onLine: session.logLine,
                    onProgress: { value in DispatchQueue.main.async { session.setProgress(value) } },
                    onRetry: { baud in DispatchQueue.main.async { session.retrying(baud: baud) } })
            } catch let error as OperationError {
                session.failure = Failure(title: "Installing didn't work.", hint: error.failure.message(port: port), details: error.transcript)
                return
            } catch is CancellationError {
                session.notice = "Installing was cancelled. The core won't start until ShinerCore is installed completely, so install it again."
                return
            } catch {
                session.failure = Failure(title: "Couldn't get ShinerCore \(build.shortCommit) ready.", hint: error.localizedDescription,
                                          details: String(describing: error))
                return
            }
            session.factoryReset = false
            session.activity = .verifying
            do {
                let identity = try await session.app.flasher.identify(port: port, builds: session.app.builds, onLine: session.logLine)
                session.identified(identity)
                if case .known(let running, let runningBoard, _) = identity.status, running.id == build.id, runningBoard.id == board.id {
                    session.installed = build
                } else {
                    session.failure = Failure(title: "Installing finished, but the core doesn't report ShinerCore \(build.shortCommit) for \(board.name).",
                                              hint: "Try installing again.", details: session.log.joined(separator: "\n"))
                }
            } catch let error as OperationError {
                session.core = .failed(error)
                session.failure = Failure(title: "Installing finished, but checking the core didn't work.",
                                          hint: error.failure.message(port: port), details: error.transcript)
            } catch {}
        }
    }

    func cancel() { task?.cancel() }

    private func identified(_ identity: CoreIdentity) {
        core = .identified(identity)
        if let board = identity.status.board { app.memory.remember(board: board.id, forMAC: identity.chip.mac) }
    }

    private func start(_ activity: Activity, _ body: @escaping (CoreSession) async -> Void) {
        self.activity = activity
        notice = nil
        failure = nil
        installed = nil
        detailsExpanded = false
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
        notice = "Installing at full speed didn't work, so the flasher is trying again more slowly (\(baud) baud)."
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

    /// The fingerprint in hex, if the core has one.
    var fingerprint: String? {
        switch self {
        case .noFirmware: nil
        case .known(_, let board, _): board.elfSha256
        case .unknown(let fingerprint): fingerprint
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
