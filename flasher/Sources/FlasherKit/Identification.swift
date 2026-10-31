import Foundation

/// Which firmware a core runs, judged by its fingerprint.
public enum FirmwareStatus: Equatable, Sendable {
    /// The fingerprint bytes are all `0xff`: nothing was ever flashed at `0x10000`.
    case noFirmware
    /// The fingerprint matches `board` in `build`, which is `buildsBehind` builds older than the newest.
    case known(build: FirmwareBuild, board: Board, buildsBehind: Int)
    /// A local build, or not ShinerCore at all.
    case unknown(fingerprint: String)

    /// - Parameter builds: Newest first.
    public init(fingerprint: Data, builds: [FirmwareBuild]) {
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
        self = .unknown(fingerprint: hex)
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

/// The board to flash, and the boards that the user may choose between.
public struct BoardSelection: Equatable, Sendable {
    public enum Reason: Equatable, Sendable {
        case fingerprint, remembered, onlyBoardForChip
        /// No rule matched; Flash stays disabled until the user picks.
        case userMustChoose
    }

    public var board: Board?
    /// The boards whose chip matches the core's.
    public var options: [Board]
    public var reason: Reason

    /// Picks a board, first match wins: the fingerprint's board, the board remembered for this
    /// MAC, the only board with this chip; otherwise none.
    /// - Parameter boards: The boards of the firmware build that is about to be flashed.
    public init(chip: String, status: FirmwareStatus, remembered: String?, boards: [Board]) {
        options = boards.filter { $0.chip == chip }
        if let id = status.board?.id, let board = options.first(where: { $0.id == id }) {
            self.board = board
            reason = .fingerprint
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
