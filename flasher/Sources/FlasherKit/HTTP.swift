import Foundation

/// Fetches URLs. The catalog goes through this so that tests can serve fixtures.
public protocol HTTPClient: Sendable {
    /// Returns the body of a successful (2xx) GET.
    /// - Throws: ``HTTPError/status(_:_:)`` for any other status, or the transport's error.
    func get(_ url: URL) async throws -> Data
}

public enum HTTPError: Error, Equatable, LocalizedError {
    case status(URL, Int)

    public var errorDescription: String? {
        switch self {
        case .status(let url, let code): "\(url.absoluteString) answered HTTP \(code)"
        }
    }
}

public struct URLSessionHTTPClient: HTTPClient {
    let session: URLSession
    let userAgent: String

    /// - Parameter userAgent: Sent with every request; GitHub's API rejects requests without one.
    public init(userAgent: String, session: URLSession = .shared) {
        self.userAgent = userAgent
        self.session = session
    }

    public func get(_ url: URL) async throws -> Data {
        var request = URLRequest(url: url, timeoutInterval: 30)
        request.setValue(userAgent, forHTTPHeaderField: "User-Agent")
        if url.host == "api.github.com" {
            request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        }
        let (data, response) = try await session.data(for: request)
        let code = (response as? HTTPURLResponse)?.statusCode ?? 0
        guard (200..<300).contains(code) else { throw HTTPError.status(url, code) }
        return data
    }
}
