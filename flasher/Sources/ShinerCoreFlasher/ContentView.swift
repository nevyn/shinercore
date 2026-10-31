import FlasherKit
import SwiftUI

struct ContentView: View {
    @Bindable var model: FlasherModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            if let update = model.catalog?.flasherUpdate {
                UpdateBanner(update: update)
            }
            devicePicker
            if model.selectedDevice != nil {
                status
                boardPicker
            }
            buildPicker
            Toggle(isOn: $model.factoryReset) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Factory reset")
                    Text("Erases presets, wiring and settings.").font(.caption).foregroundStyle(.secondary)
                }
            }
            .disabled(model.isBusy)
            actions
            messages
            if !model.log.isEmpty, model.failure == nil {
                DisclosureGroup("Details") { LogView(text: model.log.joined(separator: "\n")) }
            }
            Divider()
            footer
        }
        .padding(20)
        .frame(width: 480)
        .fixedSize(horizontal: false, vertical: true)
        .alert(model.confirmation?.title ?? "", isPresented: Binding(
            get: { model.confirmation != nil }, set: { if !$0 { model.confirmation = nil } }
        )) {
            Button("Flash", role: .destructive) { model.flash() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text(model.confirmation?.message ?? "")
        }
    }

    @ViewBuilder private var devicePicker: some View {
        if model.devices.isEmpty {
            Label("Plug in a core with a USB data cable", systemImage: "cable.connector")
                .font(.headline)
                .foregroundStyle(.secondary)
        } else {
            Picker("Core", selection: Binding(get: { model.selectedPath }, set: { model.select($0) })) {
                if model.selectedPath == nil { Text("Choose a port").tag(String?.none) }
                ForEach(model.devices) { device in
                    Text(device.label).tag(String?.some(device.path))
                }
            }
            .disabled(model.isBusy)
        }
    }

    @ViewBuilder private var status: some View {
        VStack(alignment: .leading, spacing: 4) {
            switch model.core {
            case .none:
                Button("Identify") { model.identify() }.disabled(model.isBusy)
            case .identifying:
                HStack(spacing: 8) {
                    ProgressView().controlSize(.small)
                    Text("Identifying the core…")
                }
                Text("This restarts the core, so its lights go dark for a moment.")
                    .font(.caption).foregroundStyle(.secondary)
            case .identified(let identity):
                Text(identity.status.summary).font(.headline)
                Text("\(identity.chip.description) · MAC \(identity.chip.mac)")
                    .font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
            case .failed:
                HStack {
                    Text("Couldn't identify this core.").font(.headline)
                    Button("Try again") { model.identify() }.disabled(model.isBusy)
                }
            }
        }
    }

    @ViewBuilder private var boardPicker: some View {
        if let selection = model.boardSelection {
            VStack(alignment: .leading, spacing: 4) {
                Picker("Board", selection: Binding(get: { model.board?.id }, set: { if let id = $0 { model.chooseBoard(id) } })) {
                    if model.board == nil { Text("Choose…").tag(String?.none) }
                    ForEach(selection.options) { board in Text(board.name).tag(String?.some(board.id)) }
                }
                .disabled(model.isBusy)
                Text(selection.caption(chosen: model.chosenBoardID != nil))
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    @ViewBuilder private var buildPicker: some View {
        if !model.builds.isEmpty {
            Picker("Firmware build", selection: Binding(get: { model.selectedBuild?.id }, set: { model.selectedBuildID = $0 })) {
                ForEach(Array(model.builds.enumerated()), id: \.element.id) { index, build in
                    Text(build.label(latest: index == 0)).tag(String?.some(build.id))
                }
            }
            .disabled(model.isBusy)
        }
    }

    @ViewBuilder private var actions: some View {
        switch model.activity {
        case .idle, .identifying:
            Button {
                model.requestFlash()
            } label: {
                Text(model.factoryReset ? "Factory Reset and Flash" : "Flash").frame(maxWidth: .infinity)
            }
            .controlSize(.large)
            .buttonStyle(.borderedProminent)
            .disabled(!model.canFlash)
        case .downloading:
            HStack {
                ProgressView().controlSize(.small)
                Text("Downloading the firmware build…")
                Spacer()
                Button("Cancel") { model.cancel() }
            }
        case .flashing(let progress):
            HStack {
                ProgressView(value: progress) {
                    Text("Flashing… \(Int(progress * 100)) %")
                }
                Button("Cancel") { model.cancel() }
            }
        }
    }

    @ViewBuilder private var messages: some View {
        if let notice = model.notice {
            Text(notice).foregroundStyle(.secondary)
        }
        if let failure = model.failure {
            VStack(alignment: .leading, spacing: 8) {
                Label(failure.message, systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(.red)
                    .textSelection(.enabled)
                DisclosureGroup("esptool output") { LogView(text: failure.details) }
            }
        }
    }

    @ViewBuilder private var footer: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(model.catalog?.freshness.summary(now: model.now()) ?? "Checking for firmware builds…")
            if let warning = model.cacheWarning {
                Text(warning).foregroundStyle(.orange)
            }
        }
        .font(.caption)
        .foregroundStyle(.secondary)
    }
}

private struct UpdateBanner: View {
    var update: FlasherRelease

    var body: some View {
        HStack {
            Image(systemName: "arrow.down.circle.fill")
            Text("ShinerCore Flasher \(update.version) is available.")
            Spacer()
            Link("Download", destination: update.page)
        }
        .padding(10)
        .background(.blue.opacity(0.12), in: RoundedRectangle(cornerRadius: 8))
    }
}

private struct LogView: View {
    var text: String

    var body: some View {
        ScrollView {
            Text(text)
                .font(.system(.caption, design: .monospaced))
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(height: 140)
    }
}

// MARK: - Copy

extension SerialDevice {
    var label: String {
        let port = path.replacingOccurrences(of: "/dev/cu.", with: "")
        return productName.map { "\($0) (\(port))" } ?? port
    }
}

extension FirmwareBuild {
    var dayMonth: String { date.formatted(.dateTime.day().month(.abbreviated)) }

    func label(latest: Bool) -> String {
        "\(shortCommit) · \(dayMonth) · \(subject)\(latest ? " (latest)" : "")"
    }
}

extension FirmwareStatus {
    var summary: String {
        switch self {
        case .noFirmware:
            "No firmware"
        case .known(let build, _, 0):
            "Running firmware build \(build.shortCommit) from \(build.dayMonth), up to date"
        case .known(let build, _, let behind):
            "Running firmware build \(build.shortCommit) from \(build.dayMonth), \(behind) \(behind == 1 ? "build" : "builds") behind"
        case .unknown:
            "Unknown firmware (a local build, or not ShinerCore)"
        }
    }
}

extension BoardSelection {
    func caption(chosen: Bool) -> String {
        if chosen { return "Choose the model printed on the case." }
        switch reason {
        case .fingerprint: return "Read from the firmware that the core runs."
        case .remembered: return "Remembered from the last time this core was plugged in."
        case .onlyBoardForChip: return "The only model with this chip."
        case .userMustChoose: return "Choose the model printed on the case."
        }
    }
}

extension Freshness {
    func summary(now: Date) -> String {
        switch self {
        case .online(let checkedAt):
            let formatter = RelativeDateTimeFormatter()
            formatter.unitsStyle = .short
            return checkedAt.distance(to: now) < 60
                ? "Firmware builds checked just now"
                : "Firmware builds checked \(formatter.localizedString(for: checkedAt, relativeTo: now))"
        case .offline(let cachedAt?, _):
            return "Offline, using firmware builds cached on \(cachedAt.formatted(date: .abbreviated, time: .shortened))"
        case .offline(nil, let reason):
            return "Can't reach GitHub, and no firmware builds are saved yet. Connect to the internet. (\(reason))"
        case .localDirectory(let directory):
            return "Using the firmware build in \(directory.path)"
        }
    }
}
