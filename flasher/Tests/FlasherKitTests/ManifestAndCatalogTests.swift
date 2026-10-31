import Foundation
import Testing
@testable import FlasherKit

@Suite struct ManifestTests {
    @Test func decodesTheManifestThatCIWrites() throws {
        let build = Fixture.manifest
        #expect(build.shortCommit == "1ac850e")
        #expect(build.boards.map(\.id) == ["atom-lite", "atom-echo", "atoms3-lite", "stickc-plus"])
        let s3 = try #require(build.board(id: "atoms3-lite"))
        #expect(s3.chip == "esp32s3")
        #expect(s3.uploadSpeed == 921600)
        #expect(s3.parts.map(\.offset) == [0x0, 0x8000, 0xe000, 0x10000])
        #expect(build.date == ISO8601DateFormatter().date(from: "2026-10-28T16:45:28Z"))
    }

    @Test func ignoresUnknownFieldsAndRejectsOtherSchemas() throws {
        var json = try JSONSerialization.jsonObject(with: Fixture.data("manifest.json")) as! [String: Any]
        json["futureField"] = ["anything": 1]
        #expect(throws: Never.self) { try FirmwareBuild.decode(JSONSerialization.data(withJSONObject: json)) }
        json["schema"] = 2
        #expect(throws: ManifestError.unsupportedSchema(2)) { try FirmwareBuild.decode(JSONSerialization.data(withJSONObject: json)) }
    }
}

@Suite struct CatalogTests {
    let latest = Fixture.syntheticBuild.build
    let older = Fixture.olderBuild(commit: "aaaaaaa0000000000000000000000000000000000", daysEarlier: 3)
    let asset = URL(string: "https://example.test/assets")!

    func release(_ tag: String, prerelease: Bool = true, draft: Bool = false, assets: [String] = []) -> [String: Any] {
        ["tag_name": tag, "prerelease": prerelease, "draft": draft,
         "html_url": "https://github.com/nevyn/shinercore/releases/tag/\(tag)",
         "assets": assets.map { ["name": $0, "browser_download_url": asset.appending(path: "\(tag)/\($0)").absoluteString] }]
    }

    func http(flasherTags: [(String, Bool)] = []) throws -> FakeHTTP {
        let latestFiles = ["manifest.json"] + latest.boards.flatMap { $0.parts.map(\.file) }
        var releases: [[String: Any]] = [
            release("firmware-aaaaaaa", assets: ["manifest.json"]),
            release("firmware-1ac850e", assets: latestFiles),
            release("firmware-broken", assets: ["manifest.json"]),
            release("firmware-drafty", draft: true, assets: ["manifest.json"]),
        ]
        releases += flasherTags.map { release($0.0, prerelease: $0.1) }
        let http = FakeHTTP([
            GitHubCatalog.releasesURL: try JSONSerialization.data(withJSONObject: releases),
            asset.appending(path: "firmware-aaaaaaa/manifest.json"): try JSONEncoder.manifest.encode(older),
            asset.appending(path: "firmware-1ac850e/manifest.json"): try JSONEncoder.manifest.encode(latest),
            asset.appending(path: "firmware-broken/manifest.json"): Data("{\"schema\": 1}".utf8),
        ])
        for (file, data) in Fixture.syntheticBuild.files {
            http.set(asset.appending(path: "firmware-1ac850e/\(file)"), data)
        }
        return http
    }

    @Test func ordersFirmwareBuildsNewestFirstAndSkipsBrokenAndDraftReleases() async throws {
        let now = Date(timeIntervalSince1970: 1_800_000_000)
        let catalog = GitHubCatalog(http: try http(), cacheDirectory: temporaryDirectory(), currentVersion: "0.1.0", now: { now })
        let snapshot = await catalog.refresh()
        #expect(snapshot.builds.map(\.shortCommit) == ["1ac850e", "aaaaaaa"])
        #expect(snapshot.freshness == .online(checkedAt: now))
        #expect(snapshot.flasherUpdate == nil)
    }

    @Test func offersTheNewestFullFlasherReleaseNewerThanTheRunningOne() async throws {
        let tags = [("flasher-v0.2.0", false), ("flasher-v0.10.0", false), ("flasher-v0.11.0", true), ("flasher-vbogus", false)]
        let older = await GitHubCatalog(http: try http(flasherTags: tags), cacheDirectory: temporaryDirectory(), currentVersion: "0.1.0").refresh()
        #expect(older.flasherUpdate?.version == "0.10.0")
        #expect(older.flasherUpdate?.page.absoluteString == "https://github.com/nevyn/shinercore/releases/tag/flasher-v0.10.0")
        let current = await GitHubCatalog(http: try http(flasherTags: tags), cacheDirectory: temporaryDirectory(), currentVersion: "0.10.0").refresh()
        #expect(current.flasherUpdate == nil)
    }

    @Test func fallsBackToTheCacheWhenOfflineAndReusesCachedManifests() async throws {
        let cache = temporaryDirectory()
        let checkedAt = Date(timeIntervalSince1970: 1_800_000_000)
        let http = try http()
        _ = await GitHubCatalog(http: http, cacheDirectory: cache, currentVersion: "0.1.0", now: { checkedAt }).refresh()

        let again = await GitHubCatalog(http: http, cacheDirectory: cache, currentVersion: "0.1.0").refresh()
        #expect(again.builds.count == 2)
        #expect(http.requests.filter { $0.lastPathComponent == "manifest.json" && !$0.path.contains("broken") }.count == 2)

        http.offline = true
        let offline = await GitHubCatalog(http: http, cacheDirectory: cache, currentVersion: "0.1.0").refresh()
        #expect(offline.builds.map(\.shortCommit) == ["1ac850e", "aaaaaaa"])
        guard case .offline(let cachedAt, _) = offline.freshness else { Issue.record("expected offline"); return }
        #expect(cachedAt != nil)
    }

    @Test func offlineWithoutCacheSaysSo() async throws {
        let http = FakeHTTP()
        http.offline = true
        let snapshot = await GitHubCatalog(http: http, cacheDirectory: temporaryDirectory(), currentVersion: "0.1.0").refresh()
        #expect(snapshot.builds.isEmpty)
        guard case .offline(nil, _) = snapshot.freshness else { Issue.record("expected offline without cache"); return }
    }

    @Test func downloadsVerifiesAndCachesPieces() async throws {
        let http = try http()
        let catalog = GitHubCatalog(http: http, cacheDirectory: temporaryDirectory(), currentVersion: "0.1.0")
        _ = await catalog.refresh()
        let board = try #require(latest.board(id: "atoms3-lite"))
        let files = try await catalog.pieces(for: latest, board: board)
        #expect(files.map(\.lastPathComponent) == board.parts.map(\.file))
        let requestsBefore = http.requests.count
        http.offline = true
        _ = try await catalog.pieces(for: latest, board: board)
        #expect(http.requests.count == requestsBefore)
        http.offline = false
        try await catalog.cacheAllBoards(of: latest)
    }

    @Test func rejectsAPieceWhoseChecksumDiffers() async throws {
        let http = try http()
        let catalog = GitHubCatalog(http: http, cacheDirectory: temporaryDirectory(), currentVersion: "0.1.0")
        _ = await catalog.refresh()
        let board = try #require(latest.board(id: "atom-lite"))
        http.set(asset.appending(path: "firmware-1ac850e/atom-lite-app.bin"), Data("not firmware".utf8))
        await #expect(throws: CatalogError.checksumMismatch(file: "atom-lite-app.bin", expected: board.parts[3].sha256,
                                                            actual: sha256Hex(Data("not firmware".utf8)))) {
            try await catalog.pieces(for: latest, board: board)
        }
    }

    @Test func olderBuildPiecesNeedTheNetwork() async throws {
        let http = try http()
        let catalog = GitHubCatalog(http: http, cacheDirectory: temporaryDirectory(), currentVersion: "0.1.0")
        _ = await catalog.refresh()
        await #expect(throws: (any Error).self) { try await catalog.pieces(for: older, board: older.boards[0]) }
    }

    @Test func readsALocalFirmwareBuildDirectory() async throws {
        let directory = temporaryDirectory()
        try JSONEncoder.manifest.encode(latest).write(to: directory.appending(path: "manifest.json"))
        for (file, data) in Fixture.syntheticBuild.files { try data.write(to: directory.appending(path: file)) }
        let catalog = LocalDirectoryCatalog(directory: directory)
        let snapshot = await catalog.refresh()
        #expect(snapshot.builds == [latest])
        #expect(snapshot.freshness == .localDirectory(directory))
        let board = try #require(latest.board(id: "atoms3-lite"))
        let files = try await catalog.pieces(for: latest, board: board)
        #expect(files.first == directory.appending(path: "atoms3-lite-bootloader.bin"))
        try Data("tampered".utf8).write(to: directory.appending(path: "atoms3-lite-app.bin"))
        await #expect(throws: CatalogError.self) { try await catalog.pieces(for: latest, board: board) }
    }

    @Test func comparesSemanticVersionsNumerically() {
        #expect(SemanticVersion("0.10.0")! > SemanticVersion("0.9.9")!)
        #expect(SemanticVersion("1.0") == nil)
        #expect(SemanticVersion("1.0.0-beta") == nil)
    }
}
