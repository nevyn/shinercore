import FlasherKit
import SwiftUI

/// One core: a hero with the one obvious action, errors, and the power tools under Advanced.
struct CoreDetailContent: View {
    @Bindable var session: CoreSession
    let title: String

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            hero
            if let notice = session.notice {
                Label(notice, systemImage: "info.circle").foregroundStyle(.secondary)
            }
            if let failure = session.failure { failureCard(failure) }
            advanced
        }
        .padding(24)
        .frame(maxWidth: .infinity, alignment: .leading)
        .alert(session.confirmation?.title ?? "", isPresented: Binding(
            get: { session.confirmation != nil }, set: { if !$0 { session.confirmation = nil } }
        )) {
            Button("Install", role: .destructive) { session.flash() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text(session.confirmation?.message ?? "")
        }
    }

    // MARK: Hero

    private var hero: some View {
        Card(padding: 24) {
            VStack(spacing: 14) {
                if case .installed = session.primary {
                    Image(systemName: "checkmark.circle.fill")
                        .font(.system(size: 56))
                        .foregroundStyle(.green)
                        .symbolEffect(.bounce, value: session.installed)
                } else {
                    IconWell(size: 64)
                }
                VStack(spacing: 4) {
                    Text(title).font(.title.weight(.bold)).multilineTextAlignment(.center)
                    Text(statusLine).foregroundStyle(.secondary).multilineTextAlignment(.center)
                        .fixedSize(horizontal: false, vertical: true)
                }
                primary.frame(maxWidth: 320)
            }
            .frame(maxWidth: .infinity)
        }
    }

    private var statusLine: String {
        switch session.primary {
        case .working: return session.identity == nil ? "Checking restarts the core, so its lights go dark for a moment." : summary
        case .installed: return "ShinerCore is installed. Unplug the core and open ShinerCore Remote to light it up."
        case .check: return session.failure == nil ? "Check this core to see what's on it. That restarts it, so its lights go dark for a moment." : "Couldn't check this core."
        case .chooseBoard: return "Which model is it? Choose the name printed on the case."
        case .install, .update, .upToDate: return summary
        }
    }

    private var summary: String {
        guard let identity = session.identity else { return "" }
        switch identity.status {
        case .noFirmware: return "Nothing is installed on this core yet."
        case .unknown: return "Something else is installed. Installing ShinerCore replaces it."
        case .known(let build, _, _) where session.idlePrimary == .update:
            return "ShinerCore from \(build.dayMonth) is installed. A newer one is ready."
        case .known: return "ShinerCore is installed and up to date."
        }
    }

    @ViewBuilder private var primary: some View {
        switch session.primary {
        case .working(let label, let progress, let cancellable):
            VStack(spacing: 8) {
                AccentBar(progress: progress)
                HStack {
                    Text(label).monospacedDigit()
                    Spacer()
                    if cancellable { Button("Cancel") { session.cancel() }.controlSize(.small) }
                }
                .font(.callout)
                .foregroundStyle(.secondary)
            }
        case .installed:
            Button("Done") { session.installed = nil }.controlSize(.large)
        case .check:
            mainButton("Check this core") { session.identify() }
        case .chooseBoard:
            if let selection = session.boardSelection {
                HStack(spacing: 8) {
                    ForEach(selection.options) { board in
                        Chip(title: board.name, selected: session.board?.id == board.id) { session.chooseBoard(board.id) }
                    }
                }
                .fixedSize()
            }
        case .install:
            mainButton(session.factoryReset ? "Factory Reset and Install" : "Install ShinerCore") { session.requestFlash() }
            keepsSettings
        case .update:
            mainButton(session.factoryReset ? "Factory Reset and Update" : "Update") { session.requestFlash() }
            keepsSettings
        case .upToDate:
            VStack(spacing: 10) {
                Label("Up to date", systemImage: "checkmark.seal.fill").font(.headline).foregroundStyle(.green)
                Button(session.factoryReset ? "Factory Reset and Reinstall" : "Reinstall") { session.requestFlash() }
            }
        }
    }

    private func mainButton(_ title: String, action: @escaping () -> Void) -> some View {
        Button(action: action) { Text(title).frame(maxWidth: .infinity) }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
    }

    @ViewBuilder private var keepsSettings: some View {
        if !session.factoryReset, case .known = session.identity?.status {
            Text("Keeps your presets, wiring and settings.").font(.caption).foregroundStyle(.secondary)
        }
    }

    // MARK: Failure

    private func failureCard(_ failure: CoreSession.Failure) -> some View {
        Card {
            VStack(alignment: .leading, spacing: 8) {
                Label(failure.title, systemImage: "exclamationmark.triangle.fill")
                    .font(.headline)
                    .foregroundStyle(.red)
                Text(failure.hint).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
                DisclosureGroup("Details", isExpanded: $session.detailsExpanded) {
                    LogView(text: failure.details).padding(.top, 6)
                }
            }
        }
    }

    // MARK: Advanced

    private var advanced: some View {
        DisclosureGroup("Advanced", isExpanded: $session.advancedExpanded) {
            Card {
                VStack(alignment: .leading, spacing: 14) {
                    if let selection = session.boardSelection {
                        VStack(alignment: .leading, spacing: 4) {
                            Picker("Board", selection: Binding(get: { session.board?.id }, set: { if let id = $0 { session.chooseBoard(id) } })) {
                                if session.board == nil { Text("Choose…").tag(String?.none) }
                                ForEach(selection.options) { board in Text(board.name).tag(String?.some(board.id)) }
                            }
                            Text(selection.caption(chosen: session.chosenBoardID != nil)).font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    if !session.app.builds.isEmpty {
                        Picker("Firmware build", selection: Binding(get: { session.selectedBuild?.id }, set: { session.selectedBuildID = $0 })) {
                            ForEach(Array(session.app.builds.enumerated()), id: \.element.id) { index, build in
                                Text(build.label(latest: index == 0)).tag(String?.some(build.id))
                            }
                        }
                    }
                    Toggle(isOn: $session.factoryReset) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Factory reset")
                            Text("Also erases presets, wiring and settings.").font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    Divider()
                    facts
                    HStack {
                        Text("esptool output").font(.subheadline.weight(.medium))
                        Spacer()
                        Button("Check again") { session.identify() }.controlSize(.small)
                    }
                    LogView(text: session.log.joined(separator: "\n"))
                }
                .disabled(session.isBusy)
            }
            .padding(.top, 8)
        }
    }

    private var facts: some View {
        Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 4) {
            fact("Port", session.device.path)
            if let identity = session.identity {
                fact("Chip", identity.chip.description)
                fact("MAC", identity.chip.mac)
                fact("Fingerprint", identity.status.fingerprint ?? "none (empty)")
                if case .known(let build, _, _) = identity.status { fact("Running", build.label(latest: false)) }
            }
        }
        .font(.caption)
    }

    private func fact(_ label: String, _ value: String) -> some View {
        GridRow {
            Text(label).foregroundStyle(.secondary)
            Text(value).font(.system(.caption, design: .monospaced)).textSelection(.enabled).lineLimit(2)
        }
    }
}

/// A progress bar in the accent color; without a value it sweeps back and forth.
struct AccentBar: View {
    var progress: Double?

    var body: some View {
        TimelineView(.animation(paused: progress != nil)) { context in
            GeometryReader { geometry in
                let width = geometry.size.width
                ZStack(alignment: .leading) {
                    Capsule().fill(.quaternary)
                    if let progress {
                        Capsule().fill(Theme.accent).frame(width: max(8, width * progress))
                    } else {
                        let phase = context.date.timeIntervalSinceReferenceDate.truncatingRemainder(dividingBy: 1.6) / 1.6
                        Capsule().fill(Theme.accent).frame(width: width * 0.3)
                            .offset(x: width * 0.7 * (0.5 - 0.5 * cos(phase * 2 * .pi)))
                    }
                }
            }
        }
        .frame(height: 6)
        .animation(.easeOut(duration: 0.3), value: progress)
        .accessibilityElement()
        .accessibilityValue(progress.map { "\(Int($0 * 100)) percent" } ?? "Working")
    }
}
