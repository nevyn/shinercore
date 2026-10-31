import Foundation

/// Which firmware a core runs, judged by its fingerprint and firmware marker.
public enum FirmwareStatus: Equatable, Sendable {
    /// The fingerprint bytes are all `0xff`: nothing was ever flashed at `0x10000`.
    case noFirmware
    /// The fingerprint matches `board` in `build`, which is `buildsBehind` builds older than the newest.
    case known(build: FirmwareBuild, board: Board, buildsBehind: Int)
    /// ShinerCore that no firmware build lists, such as one built with arduino-cli.
    case localBuild(fingerprint: String, marker: FirmwareMarker)
    /// Not ShinerCore, or ShinerCore from before the firmware marker.
    case unknown(fingerprint: String)

    /// - Parameter builds: Newest first.
    public init(fingerprint: Data, marker: FirmwareMarker? = nil, builds: [FirmwareBuild]) {
        if fingerprint.allSatisfy({ $0 == 0xff }) {
            self = .noFirmware
            return
        }
        let hex = fingerprint.map { String(format: "%02x", $0) }.joined()
        for (index, build) in builds.enumerated() {
            if let board = build.boards.first(where: { $0.elfSha256.lowercased() == hex }) {
                self = .known(build: build, board: board, buildsBehind: index)
                return
            }
        }
        self = if let marker { .localBuild(fingerprint: hex, marker: marker) } else { .unknown(fingerprint: hex) }
    }

    /// The id of the board that the running ShinerCore was built for, if known.
    public var boardID: String? {
        switch self {
        case .known(_, let board, _): board.id
        case .localBuild(_, let marker): marker.board
        case .noFirmware, .unknown: nil
        }
    }

    /// The board that the fingerprint names, if any.
    public var board: Board? {
        if case .known(_, let board, _) = self { board } else { nil }
    }
}

/// What identifying a core found out.
public struct CoreIdentity: Equatable, Sendable {
    public var chip: ChipInfo
    public var status: FirmwareStatus

    public init(chip: ChipInfo, status: FirmwareStatus) {
        self.chip = chip
        self.status = status
    }
}

/// What ShinerCore firmware says about itself at flash `0x10120`, right after `esp_app_desc_t`
/// (FirmwareMarker.h). Builds from before the marker don't have one.
public struct FirmwareMarker: Equatable, Sendable {
    static let magic = "ShinerCore/1"
    static let length = 56

    /// The board id that the firmware was built for, as in manifests.
    public var board: String
    /// When the firmware was compiled, in the compiling machine's local time, read as this Mac's
    /// time zone; nil when the text doesn't parse.
    public var built: Date?

    public init(board: String, built: Date?) {
        self.board = board
        self.built = built
    }

    /// Nil unless `bytes` hold a marker of this layout version.
    public init?(bytes: Data) {
        guard bytes.count >= Self.length else { return nil }
        let bytes = Data(bytes.prefix(Self.length))
        func field(_ range: Range<Int>) -> String {
            String(decoding: bytes[range].prefix { $0 != 0 }, as: UTF8.self)
        }
        guard field(0..<16) == Self.magic else { return nil }
        board = field(16..<32)
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "MMM d yyyy HH:mm:ss"
        // __DATE__ pads a one-digit day with a space: "Nov  1 2026".
        built = formatter.date(from: field(32..<56).replacingOccurrences(of: "  ", with: " "))
    }
}

/// The board to flash, and the boards that the user may choose between.
public struct BoardSelection: Equatable, Sendable {
    public enum Reason: Equatable, Sendable {
        /// The board that the running ShinerCore was built for.
        case running
        case remembered, onlyBoardForChip
        /// No rule matched; Flash stays disabled until the user picks.
        case userMustChoose
    }

    public var board: Board?
    /// The boards whose chip matches the core's.
    public var options: [Board]
    public var reason: Reason

    /// Picks a board, first match wins: the running ShinerCore's board, the board remembered for this
    /// MAC, the only board with this chip; otherwise none.
    /// - Parameter boards: The boards of the firmware build that is about to be flashed.
    public init(chip: String, status: FirmwareStatus, remembered: String?, boards: [Board]) {
        options = boards.filter { $0.chip == chip }
        if let id = status.boardID, let board = options.first(where: { $0.id == id }) {
            self.board = board
            reason = .running
        } else if let remembered, let board = options.first(where: { $0.id == remembered }) {
            self.board = board
            reason = .remembered
        } else if options.count == 1 {
            board = options[0]
            reason = .onlyBoardForChip
        } else {
            board = nil
            reason = .userMustChoose
        }
    }
}

/// Remembers which board a core is, by MAC, for cores whose chip doesn't tell.
public protocol BoardMemory: AnyObject {
    func board(forMAC mac: String) -> String?
    func remember(board: String, forMAC mac: String)
}

public final class UserDefaultsBoardMemory: BoardMemory {
    private let defaults: UserDefaults
    private let key = "boardByMAC"

    public init(defaults: UserDefaults = .standard) { self.defaults = defaults }

    public func board(forMAC mac: String) -> String? {
        (defaults.dictionary(forKey: key) as? [String: String])?[mac.lowercased()]
    }

    public func remember(board: String, forMAC mac: String) {
        var map = (defaults.dictionary(forKey: key) as? [String: String]) ?? [:]
        map[mac.lowercased()] = board
        defaults.set(map, forKey: key)
    }
}
