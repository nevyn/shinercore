import Foundation

/// esptool command lines. Flashing writes each part at its own offset and never a merged
/// image, which would erase NVS (docs/firmware-builds.md).
public enum EsptoolCommand {
    /// The baud rate for identifying and for the write retry: the CH552 bridge fails reads at 460800.
    public static let safeBaud = 115200
    /// Where the fingerprint lives: `esp_app_desc_t.app_elf_sha256` of the app image at `0x10000`.
    public static let fingerprintAddress = 0x100b0
    public static let fingerprintLength = 32

    /// Reads the fingerprint into `output`. Resets the core into its bootloader and back.
    public static func identify(port: String, output: URL) -> [String] {
        ["--port", port, "--baud", String(safeBaud), "read-flash",
         String(format: "0x%x", fingerprintAddress), String(fingerprintLength), output.path]
    }

    /// Writes `files` (one per part, in `board.parts` order) at the parts' offsets.
    /// - Parameter eraseAll: Erase the whole flash first, which wipes presets, wiring and settings.
    public static func writeFlash(port: String, board: Board, files: [URL], baud: Int, eraseAll: Bool) -> [String] {
        precondition(files.count == board.parts.count, "one file per part")
        var arguments = ["--chip", board.chip, "--port", port, "--baud", String(baud),
                         "--before", "default-reset", "--after", "hard-reset",
                         "write-flash", "-z", "--flash-mode", board.flashMode,
                         "--flash-freq", board.flashFreq, "--flash-size", board.flashSize]
        if eraseAll { arguments.append("--erase-all") }
        for (part, file) in zip(board.parts, files) {
            arguments += [String(format: "0x%x", part.offset), file.path]
        }
        return arguments
    }
}

/// The chip facts that esptool prints after connecting.
public struct ChipInfo: Equatable, Sendable {
    /// The chip as manifests name it: `esp32`, `esp32s3`.
    public var chip: String
    /// esptool's description, such as `ESP32-S3 (QFN56) (revision v0.2)`.
    public var description: String
    /// The base MAC, lowercase and colon-separated.
    public var mac: String

    public init(chip: String, description: String, mac: String) {
        self.chip = chip
        self.description = description
        self.mac = mac
    }

    /// Parses the `Connected to <chip> on <port>:` banner, `Chip type:` and the first `MAC:` line.
    public init?(transcript: String) {
        var chipName: String?
        var description: String?
        var mac: String?
        for line in transcript.split(whereSeparator: \.isNewline).map(String.init) {
            if chipName == nil, let match = line.firstMatch(of: #/^Connected to (\S+) on /#) {
                chipName = String(match.1)
            } else if description == nil, let match = line.firstMatch(of: #/^Chip type:\s+(.+)$/#) {
                description = String(match.1).trimmingCharacters(in: .whitespaces)
            } else if mac == nil, let match = line.firstMatch(of: #/^MAC:\s+((?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2})\s*$/#) {
                mac = String(match.1).lowercased()
            }
        }
        guard let chipName, let mac else { return nil }
        self.init(chip: chipName.lowercased().replacingOccurrences(of: "-", with: ""),
                  description: description ?? chipName, mac: mac)
    }
}

/// Why an esptool run failed, in terms the user can act on.
public enum EsptoolFailure: Equatable, Sendable {
    /// Another process holds the serial port.
    case portBusy
    /// The serial port disappeared (unplugged).
    case portGone
    /// The port opened, but no ROM bootloader answered.
    case noResponse
    /// Anything else; carries esptool's fatal error line, or the exit status.
    case other(String)

    /// Classifies a failed run's transcript.
    public init(transcript: String, status: Int32) {
        if transcript.contains("Could not open") || transcript.contains("Could not exclusively lock") {
            self = transcript.contains("Errno 2") || transcript.contains("No such file") ? .portGone : .portBusy
        } else if transcript.contains("Failed to connect to") || transcript.contains("No serial data received")
                    || transcript.contains("Wrong boot mode detected") {
            self = .noResponse
        } else if transcript.contains("Device not configured") || transcript.contains("Errno 6") {
            self = .portGone
        } else if let line = transcript.split(whereSeparator: \.isNewline).last(where: { $0.contains("A fatal error occurred") }) {
            self = .other(line.replacingOccurrences(of: "A fatal error occurred: ", with: "").trimmingCharacters(in: .whitespaces))
        } else {
            self = .other("esptool exited with status \(status)")
        }
    }

    /// Whether a slower baud rate could help.
    public var mightBeSpeed: Bool {
        switch self {
        case .portBusy, .portGone: false
        case .noResponse, .other: true
        }
    }

    public func message(port: String) -> String {
        switch self {
        case .portBusy:
            "Another app is using \(port); quit the Arduino IDE's serial monitor or other serial apps and try again."
        case .portGone:
            "\(port) went away. Plug the core back in and try again."
        case .noResponse:
            "The core didn't answer; try another USB cable (some are charge-only) or another port."
        case .other(let detail):
            "esptool reported: \(detail)"
        }
    }
}

/// Turns `write-flash` output into overall progress, weighting each part by its size.
public struct FlashProgress {
    private let parts: [Part]
    private let total: Double
    private var current = 0
    private var fraction = 0.0
    private var reported = 0.0

    public init(parts: [Part]) {
        self.parts = parts
        total = Double(max(1, parts.reduce(0) { $0 + $1.size }))
    }

    /// Consumes one output line; returns the overall progress (0...1) when the line moved it.
    public mutating func consume(_ line: String) -> Double? {
        if let match = line.firstMatch(of: #/Flash will be erased from 0x([0-9a-fA-F]+)/#) {
            select(address: match.1)
        } else if let match = line.firstMatch(of: #/^Wrote \d+ bytes.* at 0x([0-9a-fA-F]+)/#) {
            select(address: match.1)
            fraction = 1
        } else if line.contains("%"), let percent = line.firstMatch(of: #/(\d{1,3}(?:\.\d+)?)\s*%/#) {
            // esptool prints the address after the block, so a part's last line names the next part's offset.
            if let address = line.firstMatch(of: #/Writing at 0x([0-9a-fA-F]+)/#) { select(address: address.1, endExclusive: true) }
            fraction = min(1, (Double(percent.1) ?? 0) / 100)
        } else {
            return nil
        }
        let before = parts.prefix(current).reduce(0) { $0 + $1.size }
        let overall = min(1, (Double(before) + fraction * Double(parts.isEmpty ? 0 : parts[current].size)) / total)
        guard overall > reported else { return nil }
        reported = overall
        return overall
    }

    private mutating func select(address: Substring, endExclusive: Bool = false) {
        guard let address = Int(address, radix: 16).map({ endExclusive ? $0 - 1 : $0 }),
              let index = parts.lastIndex(where: { $0.offset <= address }) else { return }
        if index != current {
            if index > current { fraction = 0 }
            current = index
        }
    }
}
