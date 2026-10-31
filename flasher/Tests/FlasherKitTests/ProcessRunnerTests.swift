import Foundation
import Testing
@testable import FlasherKit

@Suite struct ProcessRunnerTests {
    let shell = ProcessEsptoolRunner(executable: URL(fileURLWithPath: "/bin/sh"))

    @Test func streamsLinesAndReportsTheExitStatus() async throws {
        let lines = Locked<[String]>([])
        let run = try await shell.run(["-c", "printf 'one\\rtwo\\n'; echo three >&2; exit 3"]) { line in lines.mutate { $0.append(line) } }
        #expect(run.status == 3)
        #expect(lines.value == ["one", "two", "three"])
        #expect(run.transcript.contains("three"))
    }

    @Test func cancellingTerminatesTheProcess() async throws {
        let started = Date()
        let task = Task { try await shell.run(["-c", "sleep 20"]) { _ in } }
        try await Task.sleep(for: .milliseconds(200))
        task.cancel()
        await #expect(throws: CancellationError.self) { try await task.value }
        #expect(Date().timeIntervalSince(started) < 5)
    }
}
