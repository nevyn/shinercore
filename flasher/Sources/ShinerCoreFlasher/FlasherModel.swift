import FlasherKit
import Foundation
import Observation

/// The app's state: plugged-in serial devices, one ``CoreSession`` per device, and the catalog.
@MainActor @Observable
final class FlasherModel {
    @ObservationIgnored let source: FirmwareSource
    @ObservationIgnored let flasher: CoreFlasher
    @ObservationIgnored let memory: BoardMemory
    @ObservationIgnored let now: () -> Date

    var devices: [SerialDevice] = []
    var sessions: [String: CoreSession] = [:]
    /// Ports that vanished while their session was busy.
    private var departed: Set<String> = []
    var catalog: CatalogSnapshot?
    var cacheWarning: String?
    /// The navigation stack: device paths whose detail is open.
    var path: [String] = []
    var showingHowItWorks = false
    var otherDevicesExpanded = false

    init(source: FirmwareSource, flasher: CoreFlasher, memory: BoardMemory, now: @escaping () -> Date = Date.init) {
        self.source = source
        self.flasher = flasher
        self.memory = memory
        self.now = now
    }

    var builds: [FirmwareBuild] { catalog?.builds ?? [] }
    /// Ports from known USB-serial vendors, in device order.
    var cores: [CoreSession] { devices.filter(\.isKnownVendor).compactMap { sessions[$0.path] } }
    /// Other USB serial ports (keyboards, modems...), identified only on request.
    var otherDevices: [CoreSession] { devices.filter { !$0.isKnownVendor }.compactMap { sessions[$0.path] } }

    /// The session's title, with the end of its MAC or USB serial number when another core has the same title.
    func title(for session: CoreSession) -> String {
        let title = session.baseTitle
        let twins = sessions.values.filter { $0 !== session && $0.baseTitle == title && devices.contains($0.device) }
        guard !twins.isEmpty, let suffix = session.identity?.chip.mac ?? session.device.serialNumber else { return title }
        return "\(title) \(suffix.suffix(5))"
    }

    func devicesChanged(_ devices: [SerialDevice]) {
        self.devices = devices
        for (path, session) in sessions where !devices.contains(where: { $0.path == path }) {
            if session.isBusy { departed.insert(path) } else { sessions[path] = nil }
        }
        path.removeAll { sessions[$0] == nil }
        for device in devices where sessions[device.path] == nil {
            let session = CoreSession(device: device, app: self)
            sessions[device.path] = session
            if device.isKnownVendor { session.identify() }
        }
    }

    func open(_ session: CoreSession) { path = [session.id] }

    /// A session whose port vanished mid-operation describes a core that may have changed since
    /// (a half-written install), so it identifies again if the port is back, or goes away.
    func operationFinished(_ session: CoreSession) {
        guard departed.remove(session.device.path) != nil else { return }
        if devices.contains(where: { $0.path == session.device.path }) {
            session.identify()
        } else {
            sessions[session.device.path] = nil
            path.removeAll { sessions[$0] == nil }
        }
    }

    func refreshCatalog() {
        Task {
            let snapshot = await source.refresh()
            catalog = snapshot
            for session in sessions.values { session.rejudge() }
            guard case .online = snapshot.freshness, let latest = snapshot.latest else { return }
            do {
                try await source.cacheAllBoards(of: latest)
                cacheWarning = nil
            } catch {
                cacheWarning = "Couldn't save ShinerCore \(latest.shortCommit) for offline use: \(error.localizedDescription)"
            }
        }
    }
}
