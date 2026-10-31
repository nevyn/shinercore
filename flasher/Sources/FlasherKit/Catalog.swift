import CryptoKit
import Foundation

/// What the flasher knows about available firmware builds at one moment.
public struct CatalogSnapshot: Equatable, Sendable {
    /// Newest first, by manifest `date`.
    public var builds: [FirmwareBuild]
    public var freshness: Freshness
    /// A flasher release newer than the running one, if there is one.
    public var flasherUpdate: FlasherRelease?

    public init(builds: [FirmwareBuild], freshness: Freshness, flasherUpdate: FlasherRelease? = nil) {
        self.builds = builds
        self.freshness = freshness
        self.flasherUpdate = flasherUpdate
    }

    public var latest: FirmwareBuild? { builds.first }
}

public enum Freshness: Equatable, Sendable {
    /// GitHub answered at this time.
    case online(checkedAt: Date)
    /// GitHub didn't answer; the builds come from the cache written at `cachedAt` (nil: nothing cached).
    case offline(cachedAt: Date?, reason: String)
    /// The builds come from a local directory (`--firmware-dir`).
    case localDirectory(URL)
}

public struct FlasherRelease: Equatable, Sendable {
    public var version: String
    /// The GitHub release page, which links the download.
    public var page: URL

    public init(version: String, page: URL) {
        self.version = version
        self.page = page
    }
}

/// Supplies firmware builds and their verified flash pieces.
public protocol FirmwareSource: Sendable {
    /// Fetches the newest catalog, falling back to the cache when offline. Never throws; the
    /// snapshot's ``Freshness`` says where the builds came from.
    func refresh() async -> CatalogSnapshot

    /// Returns local files for `board`'s parts in manifest order, downloading any that aren't
    /// cached. Every file's SHA-256 is checked against the manifest on each call.
    /// - Throws: ``CatalogError`` naming the failing file, or a transport error.
    func pieces(for build: FirmwareBuild, board: Board) async throws -> [URL]
}

extension FirmwareSource {
    /// Downloads and verifies the pieces of every board in `build`, so that it can be flashed offline.
    public func cacheAllBoards(of build: FirmwareBuild) async throws {
        for board in build.boards { _ = try await pieces(for: build, board: board) }
    }
}

public enum CatalogError: Error, Equatable, LocalizedError {
    case pieceUnavailable(file: String, build: String)
    case checksumMismatch(file: String, expected: String, actual: String)

    public var errorDescription: String? {
        switch self {
        case .pieceUnavailable(let file, let build):
            "\(file) of firmware build \(build) isn't downloaded yet, and the flasher can't reach GitHub. Connect to the internet, or pick the latest firmware build."
        case .checksumMismatch(let file, let expected, let actual):
            "\(file) is damaged: its SHA-256 is \(actual), but the manifest says \(expected). Try again to download it anew."
        }
    }
}

func sha256Hex(_ data: Data) -> String {
    SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
}

func sha256Hex(of file: URL) -> String? {
    (try? Data(contentsOf: file)).map(sha256Hex)
}

/// Firmware builds from nevyn/shinercore's GitHub releases, cached for offline use.
///
/// Cache layout under `cacheDirectory`: `catalog.json` (every known manifest and its asset URLs)
/// and `builds/<commit>/<file>` (flash pieces).
public struct GitHubCatalog: FirmwareSource {
    public static let releasesURL = URL(string: "https://api.github.com/repos/nevyn/shinercore/releases?per_page=100")!

    let http: HTTPClient
    let cacheDirectory: URL
    let currentVersion: String
    let now: @Sendable () -> Date

    /// - Parameters:
    ///   - currentVersion: The running flasher's CFBundleShortVersionString, compared against
    ///     `flasher-v<semver>` releases.
    public init(http: HTTPClient, cacheDirectory: URL, currentVersion: String, now: @escaping @Sendable () -> Date = { Date() }) {
        self.http = http
        self.cacheDirectory = cacheDirectory
        self.currentVersion = currentVersion
        self.now = now
    }

    public func refresh() async -> CatalogSnapshot {
        let cached = loadCache()
        do {
            let releases = try JSONDecoder().decode([GitHubRelease].self, from: try await http.get(Self.releasesURL))
            var entries: [CacheEntry] = []
            for release in releases where release.tag_name.hasPrefix("firmware-") && !release.draft {
                let assets = Dictionary(release.assets.map { ($0.name, $0.browser_download_url) }, uniquingKeysWith: { a, _ in a })
                if let known = cached?.entries.first(where: { $0.tag == release.tag_name }) {
                    entries.append(CacheEntry(tag: known.tag, build: known.build, assets: assets))
                    continue
                }
                // One unreadable release must not hide the others.
                guard let manifestURL = assets["manifest.json"],
                      let build = try? FirmwareBuild.decode(try await http.get(manifestURL)) else { continue }
                entries.append(CacheEntry(tag: release.tag_name, build: build, assets: assets))
            }
            entries.sort { $0.build.date > $1.build.date }
            let checkedAt = now()
            try? saveCache(Cache(checkedAt: checkedAt, entries: entries))
            return CatalogSnapshot(builds: entries.map(\.build), freshness: .online(checkedAt: checkedAt),
                                   flasherUpdate: newestFlasher(in: releases))
        } catch {
            return CatalogSnapshot(builds: cached?.entries.map(\.build) ?? [],
                                   freshness: .offline(cachedAt: cached?.checkedAt, reason: error.localizedDescription))
        }
    }

    public func pieces(for build: FirmwareBuild, board: Board) async throws -> [URL] {
        let directory = cacheDirectory.appending(path: "builds/\(build.commit)", directoryHint: .isDirectory)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let assets = loadCache()?.entries.first { $0.build.commit == build.commit }?.assets ?? [:]
        var files: [URL] = []
        for part in board.parts {
            let file = directory.appending(path: part.file)
            if sha256Hex(of: file) != part.sha256 {
                guard let url = assets[part.file] else {
                    throw CatalogError.pieceUnavailable(file: part.file, build: build.shortCommit)
                }
                let data = try await http.get(url)
                let actual = sha256Hex(data)
                guard actual == part.sha256 else {
                    throw CatalogError.checksumMismatch(file: part.file, expected: part.sha256, actual: actual)
                }
                try data.write(to: file, options: .atomic)
            }
            files.append(file)
        }
        return files
    }

    private func newestFlasher(in releases: [GitHubRelease]) -> FlasherRelease? {
        let current = SemanticVersion(currentVersion) ?? SemanticVersion(0, 0, 0)
        return releases
            .filter { !$0.prerelease && !$0.draft && $0.tag_name.hasPrefix("flasher-v") }
            .compactMap { release in
                SemanticVersion(String(release.tag_name.dropFirst("flasher-v".count))).map { ($0, release) }
            }
            .filter { $0.0 > current }
            .max { $0.0 < $1.0 }
            .map { FlasherRelease(version: $0.0.description, page: $0.1.html_url) }
    }

    private var cacheFile: URL { cacheDirectory.appending(path: "catalog.json") }

    private func loadCache() -> Cache? {
        (try? Data(contentsOf: cacheFile)).flatMap { try? JSONDecoder.manifest.decode(Cache.self, from: $0) }
    }

    private func saveCache(_ cache: Cache) throws {
        try FileManager.default.createDirectory(at: cacheDirectory, withIntermediateDirectories: true)
        try JSONEncoder.manifest.encode(cache).write(to: cacheFile, options: .atomic)
    }

    private struct Cache: Codable {
        var checkedAt: Date
        var entries: [CacheEntry]
    }

    private struct CacheEntry: Codable {
        var tag: String
        var build: FirmwareBuild
        var assets: [String: URL]
    }

    private struct GitHubRelease: Decodable {
        struct Asset: Decodable {
            var name: String
            var browser_download_url: URL
        }
        var tag_name: String
        var prerelease: Bool
        var draft: Bool
        var html_url: URL
        var assets: [Asset]
    }
}

/// A single firmware build read from a directory holding `manifest.json` and its pieces, as
/// `ci/build-firmware.sh` writes it.
public struct LocalDirectoryCatalog: FirmwareSource {
    let directory: URL

    public init(directory: URL) { self.directory = directory }

    public func refresh() async -> CatalogSnapshot {
        do {
            let build = try FirmwareBuild.decode(try Data(contentsOf: directory.appending(path: "manifest.json")))
            return CatalogSnapshot(builds: [build], freshness: .localDirectory(directory))
        } catch {
            return CatalogSnapshot(builds: [], freshness: .offline(cachedAt: nil, reason: "\(directory.path)/manifest.json: \(error.localizedDescription)"))
        }
    }

    public func pieces(for build: FirmwareBuild, board: Board) async throws -> [URL] {
        try board.parts.map { part in
            let file = directory.appending(path: part.file)
            guard let actual = sha256Hex(of: file) else {
                throw CatalogError.pieceUnavailable(file: part.file, build: build.shortCommit)
            }
            guard actual == part.sha256 else {
                throw CatalogError.checksumMismatch(file: part.file, expected: part.sha256, actual: actual)
            }
            return file
        }
    }
}

struct SemanticVersion: Comparable, CustomStringConvertible {
    var parts: [Int]

    init(_ major: Int, _ minor: Int, _ patch: Int) { parts = [major, minor, patch] }

    /// Parses `major.minor.patch`; anything else is nil.
    init?(_ string: String) {
        let parts = string.split(separator: ".", omittingEmptySubsequences: false).map { Int($0) }
        guard parts.count == 3, parts.allSatisfy({ $0 != nil && $0! >= 0 }) else { return nil }
        self.parts = parts.map { $0! }
    }

    static func < (a: Self, b: Self) -> Bool { a.parts.lexicographicallyPrecedes(b.parts) }
    var description: String { parts.map(String.init).joined(separator: ".") }
}
