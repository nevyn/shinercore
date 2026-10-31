import Foundation
@testable import FlasherKit

// Fixtures/*-s3.txt are esptool v5.6.0 captures from an AtomS3 Lite. *-handwritten.txt follow
// esptool's source; no ESP32 (CH552) board was attached to capture them.

enum Fixture {
    /// Firmware marker bytes laid out as FirmwareMarker.h does.
    static func marker(board: String, built: String) -> Data {
        func field(_ text: String, _ size: Int) -> Data { Data(text.utf8) + Data(count: size - text.utf8.count) }
        return field("ShinerCore/1", 16) + field(board, 16) + field(built, 24)
    }

    static func url(_ name: String) -> URL {
        Bundle.module.url(forResource: "Fixtures/\(name)", withExtension: nil)!
    }

    static func data(_ name: String) -> Data { try! Data(contentsOf: url(name)) }
    static func text(_ name: String) -> String { String(decoding: data(name), as: UTF8.self) }
    static var manifest: FirmwareBuild { try! FirmwareBuild.decode(data("manifest.json")) }

    /// The fixture manifest with every part replaced by a small generated file, so that piece
    /// downloads can be tested without the real firmware.
    static let syntheticBuild: (build: FirmwareBuild, files: [String: Data]) = {
        var build = manifest
        var files: [String: Data] = [:]
        for b in build.boards.indices {
            for p in build.boards[b].parts.indices {
                let data = Data("\(build.boards[b].parts[p].file) contents".utf8)
                files[build.boards[b].parts[p].file] = data
                build.boards[b].parts[p].size = data.count
                build.boards[b].parts[p].sha256 = sha256Hex(data)
            }
        }
        return (build, files)
    }()

    /// A copy of the fixture manifest from an older commit, with `elfSha256` values changed to match.
    static func olderBuild(commit: String, daysEarlier: Double, from build: FirmwareBuild = manifest) -> FirmwareBuild {
        var older = build
        older.commit = commit
        older.date = build.date.addingTimeInterval(-daysEarlier * 86400)
        older.subject = "Older commit \(commit.prefix(7))"
        for index in older.boards.indices { older.boards[index].elfSha256 = String(repeating: String(commit.prefix(2)), count: 32) }
        return older
    }
}

func temporaryDirectory() -> URL {
    let url = FileManager.default.temporaryDirectory.appending(path: "FlasherKitTests-\(UUID().uuidString)", directoryHint: .isDirectory)
    try! FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    return url
}

/// Serves canned responses by URL and records every request.
final class FakeHTTP: HTTPClient, @unchecked Sendable {
    private let lock = NSLock()
    private var responses: [URL: Data]
    private(set) var requests: [URL] = []
    var offline = false

    init(_ responses: [URL: Data] = [:]) { self.responses = responses }

    func set(_ url: URL, _ data: Data) { lock.withLock { responses[url] = data } }

    func get(_ url: URL) async throws -> Data {
        try lock.withLock {
            requests.append(url)
            if offline { throw URLError(.notConnectedToInternet) }
            guard let data = responses[url] else { throw HTTPError.status(url, 404) }
            return data
        }
    }
}

/// Replays canned esptool runs in order, writing the fingerprint file for `read-flash`.
final class ReplayRunner: EsptoolRunner, @unchecked Sendable {
    struct Step {
        var transcript: String
        var status: Int32 = 0
        var fingerprint: Data?
        /// The firmware marker's bytes, written after the fingerprint at `EsptoolCommand.markerOffset`.
        var marker: Data?
        /// Awaited before the step answers, to hold the operation in flight.
        var gate: (@Sendable () async -> Void)?
    }

    private let lock = NSLock()
    private var steps: [Step]
    private(set) var invocations: [[String]] = []

    init(_ steps: [Step]) { self.steps = steps }

    func run(_ arguments: [String], onLine: @escaping @Sendable (String) -> Void) async throws -> EsptoolRun {
        let step = lock.withLock {
            invocations.append(arguments)
            return steps.removeFirst()
        }
        await step.gate?()
        if let fingerprint = step.fingerprint, let output = arguments.last {
            var read = Data(repeating: 0xff, count: EsptoolCommand.identifyLength)
            read.replaceSubrange(0..<fingerprint.count, with: fingerprint)
            if let marker = step.marker {
                read.replaceSubrange(EsptoolCommand.markerOffset..<EsptoolCommand.markerOffset + marker.count, with: marker)
            }
            try read.write(to: URL(fileURLWithPath: output))
        }
        var splitter = LineSplitter()
        (splitter.append(Data(step.transcript.utf8)) + splitter.finish()).forEach(onLine)
        return EsptoolRun(status: step.status, transcript: step.transcript)
    }
}

final class InMemoryBoardMemory: BoardMemory {
    var boards: [String: String] = [:]
    func board(forMAC mac: String) -> String? { boards[mac] }
    func remember(board: String, forMAC mac: String) { boards[mac] = board }
}

extension Data {
    init(hexString: String) {
        var bytes: [UInt8] = []
        var index = hexString.startIndex
        while index < hexString.endIndex {
            let next = hexString.index(index, offsetBy: 2)
            bytes.append(UInt8(hexString[index..<next], radix: 16)!)
            index = next
        }
        self.init(bytes)
    }
}
