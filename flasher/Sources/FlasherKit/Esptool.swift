import Foundation

/// The outcome of one esptool invocation.
public struct EsptoolRun: Equatable, Sendable {
    public var status: Int32
    /// Everything esptool printed, stdout and stderr interleaved.
    public var transcript: String

    public init(status: Int32, transcript: String) {
        self.status = status
        self.transcript = transcript
    }

    public var succeeded: Bool { status == 0 }
}

/// Runs esptool. The app uses ``ProcessEsptoolRunner``; tests replay captured transcripts.
public protocol EsptoolRunner: Sendable {
    /// Runs esptool with `arguments` and returns once it has exited.
    /// - Parameter onLine: Receives each output line as it arrives, on an arbitrary thread. A
    ///   carriage return ends a line too, so in-place progress redraws arrive one by one.
    /// - Throws: `CancellationError` when the task is cancelled, after esptool has been terminated;
    ///   or the error from launching the executable. A non-zero exit is not an error: check
    ///   ``EsptoolRun/status``.
    func run(_ arguments: [String], onLine: @escaping @Sendable (String) -> Void) async throws -> EsptoolRun
}

/// Runs an esptool executable as a subprocess.
public final class ProcessEsptoolRunner: EsptoolRunner, @unchecked Sendable {
    public let executable: URL

    public init(executable: URL) { self.executable = executable }

    /// The esptool that `scripts/build-app.sh` bundles for the running architecture.
    public static func bundled(in bundle: Bundle = .main) -> ProcessEsptoolRunner {
        #if arch(arm64)
        let arch = "arm64"
        #else
        let arch = "amd64"
        #endif
        let resources = bundle.resourceURL ?? bundle.bundleURL
        return ProcessEsptoolRunner(executable: resources.appending(path: "esptool/\(arch)/esptool"))
    }

    public func run(_ arguments: [String], onLine: @escaping @Sendable (String) -> Void) async throws -> EsptoolRun {
        let process = Process()
        process.executableURL = executable
        process.arguments = arguments
        var environment = ProcessInfo.processInfo.environment
        environment["PYTHONUNBUFFERED"] = "1"  // a piped Python stdout is block-buffered, which stalls progress
        environment["NO_COLOR"] = "1"
        process.environment = environment
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        process.standardInput = FileHandle.nullDevice

        // waitUntilExit() spins the calling thread's run loop, which on a dispatch worker can miss the exit and hang.
        let exited = DispatchSemaphore(value: 0)
        process.terminationHandler = { _ in exited.signal() }

        let state = RunState()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<EsptoolRun, Error>) in
                do {
                    try state.launch(process)
                } catch {
                    continuation.resume(throwing: error)
                    return
                }
                DispatchQueue.global(qos: .userInitiated).async {
                    var lines = LineSplitter()
                    var transcript = Data()
                    let reader = pipe.fileHandleForReading
                    while case let chunk = reader.availableData, !chunk.isEmpty {
                        transcript.append(chunk)
                        lines.append(chunk).forEach(onLine)
                    }
                    lines.finish().forEach(onLine)
                    exited.wait()
                    if state.cancelled {
                        continuation.resume(throwing: CancellationError())
                    } else {
                        continuation.resume(returning: EsptoolRun(status: process.terminationStatus,
                                                                  transcript: String(decoding: transcript, as: UTF8.self)))
                    }
                }
            }
        } onCancel: {
            state.cancel()
        }
    }

    private final class RunState: @unchecked Sendable {
        private let lock = NSLock()
        private var process: Process?
        private var isCancelled = false

        var cancelled: Bool { lock.withLock { isCancelled } }

        func launch(_ process: Process) throws {
            try lock.withLock {
                if isCancelled { throw CancellationError() }
                try process.run()
                self.process = process
            }
        }

        func cancel() {
            lock.withLock {
                isCancelled = true
                if process?.isRunning == true { process?.terminate() }
            }
        }
    }
}

/// Splits a byte stream into lines at `\n` and `\r`, dropping empty lines.
struct LineSplitter {
    private var pending = Data()

    mutating func append(_ data: Data) -> [String] {
        pending.append(data)
        var lines: [String] = []
        while let end = pending.firstIndex(where: { $0 == 0x0A || $0 == 0x0D }) {
            let line = String(decoding: pending[pending.startIndex..<end], as: UTF8.self)
            pending.removeSubrange(pending.startIndex...end)
            if !line.trimmingCharacters(in: .whitespaces).isEmpty { lines.append(line) }
        }
        return lines
    }

    mutating func finish() -> [String] {
        defer { pending.removeAll() }
        let line = String(decoding: pending, as: UTF8.self)
        return line.trimmingCharacters(in: .whitespaces).isEmpty ? [] : [line]
    }
}
