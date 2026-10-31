import FlasherKit
import SwiftUI

struct RootView: View {
    @Bindable var model: FlasherModel

    var body: some View {
        NavigationStack(path: $model.path) {
            ScrollView { CoreListContent(model: model) }
                .background(Theme.page)
                .navigationTitle("ShinerCore Flasher")
                .navigationDestination(for: String.self) { path in
                    if let session = model.sessions[path] {
                        ScrollView { CoreDetailContent(session: session, title: model.title(for: session)) }
                            .background(Theme.page)
                            .navigationTitle(model.title(for: session))
                    }
                }
                .toolbar {
                    Button { model.showingHowItWorks = true } label: { Label("How it works", systemImage: "questionmark.circle") }
                }
        }
        .sheet(isPresented: $model.showingHowItWorks) {
            HowItWorksSheet(boards: BoardShop.boards(in: model.catalog?.latest))
        }
        .tint(Theme.accent)
    }
}

/// The start page: plugged-in cores, or the welcome when there are none.
struct CoreListContent: View {
    @Bindable var model: FlasherModel

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            if let update = model.catalog?.flasherUpdate { UpdateBanner(update: update) }
            if model.cores.isEmpty {
                welcome
            } else {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Plugged-in cores").font(.largeTitle.weight(.bold))
                    Card(padding: 0) { rows(model.cores) }
                    CardFooter("Missing one? Plug it in with a USB data cable. Still missing? Hold its button while you plug it in.")
                }
            }
            if !model.otherDevices.isEmpty {
                DisclosureGroup("Other serial devices", isExpanded: $model.otherDevicesExpanded) {
                    VStack(alignment: .leading, spacing: 8) {
                        Card(padding: 0) { rows(model.otherDevices) }
                        CardFooter("These aren't known ShinerCore boards. Open one to check it anyway.")
                    }
                    .padding(.top, 8)
                }
            }
            footer
        }
        .padding(24)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder private var welcome: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Welcome").font(.largeTitle.weight(.bold))
            Card {
                VStack(spacing: 10) {
                    IconWell(symbol: "cable.connector", size: 64)
                    Text("Plug in a core").font(.title2.weight(.semibold))
                    Text("Use a USB data cable; some cables only charge. Still missing? Hold its button while you plug it in.")
                        .multilineTextAlignment(.center)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 8)
            }
        }
        VStack(alignment: .leading, spacing: 8) {
            SectionTitle("How it works")
            HowItWorksSteps(boards: BoardShop.boards(in: model.catalog?.latest))
        }
    }

    private func rows(_ sessions: [CoreSession]) -> some View {
        ForEach(Array(sessions.enumerated()), id: \.element.id) { index, session in
            if index > 0 { Divider().padding(.leading, 68) }
            CoreRow(session: session, title: model.title(for: session)) { model.open(session) }
        }
    }

    private var footer: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(model.catalog?.freshness.summary(now: model.now()) ?? "Checking for new ShinerCore…")
            if let warning = model.cacheWarning { Text(warning).foregroundStyle(.orange) }
        }
        .font(.footnote)
        .foregroundStyle(.secondary)
        .padding(.horizontal, 4)
    }
}

struct CoreRow: View {
    let session: CoreSession
    let title: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 14) {
                IconWell(symbol: session.device.isKnownVendor ? "sparkles" : "cable.connector",
                         tint: session.device.isKnownVendor ? Theme.accent : .secondary)
                VStack(alignment: .leading, spacing: 2) {
                    Text(title).font(.headline)
                    Text(session.summary).font(.subheadline).foregroundStyle(.secondary)
                }
                Spacer(minLength: 8)
                StatusBadge(session: session)
                Image(systemName: "chevron.right").foregroundStyle(.tertiary).font(.footnote.weight(.semibold))
            }
            .padding(.vertical, 6)
            .padding(.horizontal, 14)
            .padding(.vertical, 4)
            .contentShape(.rect)
        }
        .buttonStyle(.plain)
    }
}
