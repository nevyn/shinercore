import Foundation
import Testing
@testable import FlasherKit

@Suite struct EsptoolOutputTests {
    @Test func parsesChipAndMACOfAnESP32S3() throws {
        let chip = try #require(ChipInfo(transcript: Fixture.text("identify-esp32s3.txt")))
        #expect(chip == ChipInfo(chip: "esp32s3", description: "ESP32-S3 (QFN56) (revision v0.2)", mac: "dc:54:75:c8:e9:44"))
    }

    @Test func parsesChipAndMACOfAnESP32() throws {
        let chip = try #require(ChipInfo(transcript: Fixture.text("identify-esp32.txt")))
        #expect(chip.chip == "esp32")
        #expect(chip.mac == "4c:75:25:a1:b2:c3")
    }

    @Test func classifiesKnownFailures() {
        #expect(EsptoolFailure(transcript: Fixture.text("failure-port-busy.txt"), status: 2) == .portBusy)
        #expect(EsptoolFailure(transcript: Fixture.text("failure-port-gone.txt"), status: 2) == .portGone)
        #expect(EsptoolFailure(transcript: Fixture.text("failure-no-response.txt"), status: 2) == .noResponse)
        #expect(EsptoolFailure(transcript: "A fatal error occurred: MD5 of file does not match data in flash!", status: 2)
                == .other("MD5 of file does not match data in flash!"))
        #expect(EsptoolFailure(transcript: "", status: -9) == .other("esptool exited with status -9"))
    }

    @Test func busyPortMessageNamesThePort() {
        #expect(EsptoolFailure.portBusy.message(port: "/dev/cu.usbmodem1101")
                == "Another app is using /dev/cu.usbmodem1101; quit the Arduino IDE's serial monitor or other serial apps and try again.")
    }

    @Test func buildsTheIdentifyCommand() {
        #expect(EsptoolCommand.identify(port: "/dev/cu.x", output: URL(fileURLWithPath: "/tmp/f.bin"))
                == ["--port", "/dev/cu.x", "--baud", "115200", "read-flash", "0x100b0", "32", "/tmp/f.bin"])
    }

    @Test func writesEveryPartAtItsOwnOffset() throws {
        let board = try #require(Fixture.manifest.board(id: "atom-lite"))
        let files = board.parts.map { URL(fileURLWithPath: "/c/\($0.file)") }
        #expect(EsptoolCommand.writeFlash(port: "/dev/cu.x", board: board, files: files, baud: 1500000, eraseAll: false) == [
            "--chip", "esp32", "--port", "/dev/cu.x", "--baud", "1500000", "--before", "default-reset", "--after", "hard-reset",
            "write-flash", "-z", "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "4MB",
            "0x1000", "/c/atom-lite-bootloader.bin", "0x8000", "/c/atom-lite-partitions.bin",
            "0xe000", "/c/atom-lite-boot_app0.bin", "0x10000", "/c/atom-lite-app.bin",
        ])
        let erasing = EsptoolCommand.writeFlash(port: "/dev/cu.x", board: board, files: files, baud: 115200, eraseAll: true)
        #expect(erasing.contains("--erase-all"))
        #expect(erasing.firstIndex(of: "--erase-all")! < erasing.firstIndex(of: "0x1000")!)
    }

    @Test func progressIsWeightedByPartSizeAndMonotonic() throws {
        let board = try #require(Fixture.manifest.board(id: "atoms3-lite"))
        var progress = FlashProgress(parts: board.parts)
        var values: [Double] = []
        var afterBootApp0: Double?
        for line in Fixture.text("write-flash-atoms3-lite.txt").split(whereSeparator: \.isNewline).map(String.init) {
            if let value = progress.consume(line) { values.append(value) }
            if line.hasPrefix("Wrote 8192 bytes") { afterBootApp0 = values.last }
        }
        #expect(values == values.sorted())
        #expect(values.last == 1)
        let small = Double(15104 + 3072 + 8192) / Double(board.totalSize)
        #expect(abs(try #require(afterBootApp0) - small) < 0.001)
        #expect(values.contains { $0 > 0.4 && $0 < 0.6 })
    }

    @Test func splitsLinesAtCarriageReturns() {
        var splitter = LineSplitter()
        #expect(splitter.append(Data("a\r b\n\nc".utf8)) == ["a", " b"])
        #expect(splitter.finish() == ["c"])
    }
}

@Suite struct IdentificationTests {
    let builds = [Fixture.manifest, Fixture.olderBuild(commit: "bbbbbbb000000000000000000000000000000000", daysEarlier: 2),
                  Fixture.olderBuild(commit: "ccccccc000000000000000000000000000000000", daysEarlier: 4)]

    @Test func blankFlashMeansNoFirmware() {
        #expect(FirmwareStatus(fingerprint: Data(repeating: 0xff, count: 32), builds: builds) == .noFirmware)
    }

    @Test func findsTheBoardAndHowFarBehind() throws {
        let s3 = try #require(builds[0].board(id: "atoms3-lite"))
        #expect(FirmwareStatus(fingerprint: Data(hexString: s3.elfSha256), builds: builds) == .known(build: builds[0], board: s3, buildsBehind: 0))
        let old = FirmwareStatus(fingerprint: Data(repeating: 0xcc, count: 32), builds: builds)
        guard case .known(let build, _, 2) = old else { Issue.record("expected 2 behind, got \(old)"); return }
        #expect(build.shortCommit == "ccccccc")
    }

    @Test func otherFingerprintsAreUnknown() {
        #expect(FirmwareStatus(fingerprint: Data(repeating: 0x12, count: 32), builds: builds) == .unknown(fingerprint: String(repeating: "12", count: 32)))
    }

    @Test func boardSelectionPrefersFingerprintThenMemoryThenChip() throws {
        let boards = builds[0].boards
        let echo = try #require(builds[0].board(id: "atom-echo"))
        let fromFingerprint = BoardSelection(chip: "esp32", status: .known(build: builds[0], board: echo, buildsBehind: 0),
                                             remembered: "atom-lite", boards: boards)
        #expect(fromFingerprint.board?.id == "atom-echo")
        #expect(fromFingerprint.reason == .fingerprint)

        let remembered = BoardSelection(chip: "esp32", status: .noFirmware, remembered: "stickc-plus", boards: boards)
        #expect(remembered.board?.id == "stickc-plus")
        #expect(remembered.reason == .remembered)

        let s3 = BoardSelection(chip: "esp32s3", status: .unknown(fingerprint: ""), remembered: "atom-lite", boards: boards)
        #expect(s3.board?.id == "atoms3-lite")
        #expect(s3.options.map(\.id) == ["atoms3-lite"])

        let esp32 = BoardSelection(chip: "esp32", status: .noFirmware, remembered: nil, boards: boards)
        #expect(esp32.board == nil)
        #expect(esp32.reason == .userMustChoose)
        #expect(esp32.options.map(\.id) == ["atom-lite", "atom-echo", "stickc-plus"])
    }

    @Test func sortsKnownVendorsFirst() {
        let devices = SerialDevice.sorted([
            SerialDevice(path: "/dev/cu.usbmodem1", vendorID: 0x1D50, productID: 1, productName: "Keyboard"),
            SerialDevice(path: "/dev/cu.usbmodem2", vendorID: 0x303A, productID: 0x1001),
            SerialDevice(path: "/dev/cu.usbserial-1", vendorID: 0x1A86, productID: 0x55D4),
        ])
        #expect(devices.map(\.path) == ["/dev/cu.usbmodem2", "/dev/cu.usbserial-1", "/dev/cu.usbmodem1"])
    }
}

@Suite struct CoreFlasherTests {
    let builds = [Fixture.manifest]

    @Test func identifiesAnAtomS3RunningTheLatestBuild() async throws {
        let s3 = try #require(builds[0].board(id: "atoms3-lite"))
        let runner = ReplayRunner([.init(transcript: Fixture.text("identify-esp32s3.txt"), fingerprint: Data(hexString: s3.elfSha256))])
        let identity = try await CoreFlasher(runner: runner).identify(port: "/dev/cu.usbmodem1101", builds: builds)
        #expect(identity.chip.mac == "dc:54:75:c8:e9:44")
        #expect(identity.status == .known(build: builds[0], board: s3, buildsBehind: 0))
        #expect(runner.invocations.first?.prefix(5) == ["--port", "/dev/cu.usbmodem1101", "--baud", "115200", "read-flash"])
    }

    @Test func identifyFailureNamesThePortAndKeepsTheOutput() async throws {
        let runner = ReplayRunner([.init(transcript: Fixture.text("failure-port-busy.txt"), status: 2)])
        do {
            _ = try await CoreFlasher(runner: runner).identify(port: "/dev/cu.usbmodem1101", builds: builds)
            Issue.record("expected a failure")
        } catch let error as OperationError {
            #expect(error.step == .identify)
            #expect(error.failure == .portBusy)
            #expect(error.localizedDescription.hasPrefix("Couldn't identify the core on /dev/cu.usbmodem1101. Another app is using /dev/cu.usbmodem1101"))
            #expect(error.transcript.contains("Errno 35"))
        }
    }

    @Test func retriesASlowFlashAt115200() async throws {
        let board = try #require(builds[0].board(id: "atom-lite"))
        let runner = ReplayRunner([
            .init(transcript: "A fatal error occurred: Serial data stream stopped: Possible serial noise or corruption.", status: 2),
            .init(transcript: Fixture.text("write-flash-atoms3-lite.txt")),
        ])
        let retries = Locked<[Int]>([])
        try await CoreFlasher(runner: runner).flash(port: "/dev/cu.usbserial-1", board: board,
                                                    files: board.parts.map { URL(fileURLWithPath: "/c/\($0.file)") },
                                                    eraseAll: false, onRetry: { baud in retries.mutate { $0.append(baud) } })
        #expect(retries.value == [115200])
        #expect(runner.invocations.map { $0[$0.firstIndex(of: "--baud")! + 1] } == ["1500000", "115200"])
    }

    @Test func doesNotRetryABusyPort() async throws {
        let board = try #require(builds[0].board(id: "atom-lite"))
        let runner = ReplayRunner([.init(transcript: Fixture.text("failure-port-busy.txt"), status: 2)])
        await #expect(throws: OperationError.self) {
            try await CoreFlasher(runner: runner).flash(port: "/dev/cu.usbserial-1", board: board,
                                                        files: board.parts.map { URL(fileURLWithPath: "/c/\($0.file)") }, eraseAll: false)
        }
        #expect(runner.invocations.count == 1)
    }
}

final class Locked<Value>: @unchecked Sendable {
    private let lock = NSLock()
    private var stored: Value
    init(_ value: Value) { stored = value }
    var value: Value { lock.withLock { stored } }
    func mutate(_ body: (inout Value) -> Void) { lock.withLock { body(&stored) } }
}
