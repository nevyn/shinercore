import Foundation

/// One firmware build: the flash pieces for every board built from one commit, as described by
/// its `manifest.json` (docs/firmware-builds.md). Unknown fields are ignored.
public struct FirmwareBuild: Codable, Hashable, Sendable, Identifiable {
    public static let supportedSchema = 1

    public var schema: Int
    /// The full 40-character commit sha.
    public var commit: String
    /// The commit date. Firmware builds are ordered by it, newest first.
    public var date: Date
    /// The commit subject.
    public var subject: String
    public var boards: [Board]

    public var id: String { commit }
    /// The 7-character commit that names the firmware build in tags and in the UI.
    public var shortCommit: String { String(commit.prefix(7)) }

    public func board(id: String) -> Board? { boards.first { $0.id == id } }

    /// Decodes a manifest, rejecting schemas other than ``supportedSchema``.
    /// - Throws: ``ManifestError``, or `DecodingError` when the JSON doesn't match the schema.
    public static func decode(_ data: Data) throws -> FirmwareBuild {
        let build = try JSONDecoder.manifest.decode(FirmwareBuild.self, from: data)
        guard build.schema == supportedSchema else { throw ManifestError.unsupportedSchema(build.schema) }
        return build
    }
}

public enum ManifestError: Error, Equatable, LocalizedError {
    case unsupportedSchema(Int)

    public var errorDescription: String? {
        switch self {
        case .unsupportedSchema(let schema):
            "This firmware build uses manifest schema \(schema), which this flasher doesn't understand. Update the flasher."
        }
    }
}

/// The flash layout of one board within a firmware build.
public struct Board: Codable, Hashable, Sendable, Identifiable {
    /// The board id, such as `atom-echo`.
    public var id: String
    /// The M5Stack model name printed on the case, such as `M5Atom Echo`.
    public var name: String
    /// The chip as esptool names it in `--chip`: `esp32` or `esp32s3`.
    public var chip: String
    /// The fingerprint that a core running this board's firmware carries at flash `0x100b0`.
    public var elfSha256: String
    public var flashMode: String
    public var flashFreq: String
    public var flashSize: String
    /// The baud rate that `write-flash` starts at.
    public var uploadSpeed: Int
    /// The flash pieces, each written at its own offset.
    public var parts: [Part]

    public var totalSize: Int { parts.reduce(0) { $0 + $1.size } }
}

/// One flash piece of a board.
public struct Part: Codable, Hashable, Sendable {
    /// The flash address that the piece is written to.
    public var offset: Int
    /// The release asset name, such as `atom-lite-app.bin`.
    public var file: String
    public var size: Int
    /// Lowercase hex SHA-256 of the file, checked before every flash.
    public var sha256: String
}

extension JSONDecoder {
    static var manifest: JSONDecoder {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return decoder
    }
}

extension JSONEncoder {
    static var manifest: JSONEncoder {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return encoder
    }
}
