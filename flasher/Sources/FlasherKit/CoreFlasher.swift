import Foundation

/// A failed operation on one core, carrying what the user needs to act and the raw output.
public struct OperationError: Error, Equatable, LocalizedError {
    public enum Step: String, Sendable {
        case identify = "identify"
        case flash = "flash"
    }

    public var step: Step
    public var port: String
    public var failure: EsptoolFailure
    /// esptool's full output, for the details disclosure.
    public var transcript: String

    public init(step: Step, port: String, failure: EsptoolFailure, transcript: String) {
        self.step = step
        self.port = port
        self.failure = failure
        self.transcript = transcript
    }

    public var errorDescription: String? {
        "Couldn't \(step.rawValue) the core on \(port). \(failure.message(port: port))"
    }
}

/// Identifies and flashes cores through esptool.
public struct CoreFlasher: Sendable {
    let runner: EsptoolRunner
    let temporaryDirectory: URL

    public init(runner: EsptoolRunner, temporaryDirectory: URL = FileManager.default.temporaryDirectory) {
        self.runner = runner
        self.temporaryDirectory = temporaryDirectory
    }

    /// Reads the chip, MAC and fingerprint of the core on `port`. Restarts the core.
    /// - Parameter builds: Known firmware builds, newest first, to look the fingerprint up in.
    /// - Throws: ``OperationError`` with step `.identify`, or `CancellationError`.
    public func identify(port: String, builds: [FirmwareBuild],
                         onLine: @escaping @Sendable (String) -> Void = { _ in }) async throws -> CoreIdentity {
        let output = temporaryDirectory.appending(path: "shinercore-fingerprint-\(UUID().uuidString).bin")
        defer { try? FileManager.default.removeItem(at: output) }
        let run = try await runner.run(EsptoolCommand.identify(port: port, output: output), onLine: onLine)
        func fail(_ failure: EsptoolFailure) -> OperationError {
            OperationError(step: .identify, port: port, failure: failure, transcript: run.transcript)
        }
        guard run.succeeded else { throw fail(EsptoolFailure(transcript: run.transcript, status: run.status)) }
        guard let chip = ChipInfo(transcript: run.transcript) else {
            throw fail(.other("its output didn't name the chip and MAC"))
        }
        guard let fingerprint = try? Data(contentsOf: output), fingerprint.count == EsptoolCommand.fingerprintLength else {
            throw fail(.other("it didn't write the \(EsptoolCommand.fingerprintLength) fingerprint bytes to \(output.path)"))
        }
        return CoreIdentity(chip: chip, status: FirmwareStatus(fingerprint: fingerprint, builds: builds))
    }

    /// Writes `files` to the core on `port` at `board.uploadSpeed`, retrying once at
    /// ``EsptoolCommand/safeBaud`` when the failure might be due to speed.
    /// - Parameters:
    ///   - files: One verified file per part, in `board.parts` order.
    ///   - eraseAll: Factory reset: erase presets, wiring and settings too.
    ///   - onProgress: Overall progress, 0...1; restarts from 0 on the retry.
    ///   - onRetry: Called before the retry at the safe baud rate.
    /// - Throws: ``OperationError`` with step `.flash` from the last attempt, or `CancellationError`.
    public func flash(port: String, board: Board, files: [URL], eraseAll: Bool,
                      onLine: @escaping @Sendable (String) -> Void = { _ in },
                      onProgress: @escaping @Sendable (Double) -> Void = { _ in },
                      onRetry: @escaping @Sendable (Int) -> Void = { _ in }) async throws {
        var speeds = [board.uploadSpeed]
        if board.uploadSpeed != EsptoolCommand.safeBaud { speeds.append(EsptoolCommand.safeBaud) }
        for (attempt, baud) in speeds.enumerated() {
            if attempt > 0 { onRetry(baud) }
            let progress = ProgressBox(parts: board.parts)
            let arguments = EsptoolCommand.writeFlash(port: port, board: board, files: files, baud: baud, eraseAll: eraseAll)
            let run = try await runner.run(arguments) { line in
                onLine(line)
                if let value = progress.consume(line) { onProgress(value) }
            }
            if run.succeeded { return }
            let failure = EsptoolFailure(transcript: run.transcript, status: run.status)
            if attempt == speeds.count - 1 || !failure.mightBeSpeed {
                throw OperationError(step: .flash, port: port, failure: failure, transcript: run.transcript)
            }
        }
    }

    private final class ProgressBox: @unchecked Sendable {
        private let lock = NSLock()
        private var progress: FlashProgress

        init(parts: [Part]) { progress = FlashProgress(parts: parts) }

        func consume(_ line: String) -> Double? { lock.withLock { progress.consume(line) } }
    }
}
