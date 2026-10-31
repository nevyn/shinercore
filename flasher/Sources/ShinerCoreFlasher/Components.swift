import AppKit
import FlasherKit
import SwiftUI

enum Theme {
    /// ShinerCore Remote's AccentColor; the darker light variant keeps text on white above 4.5:1.
    static let accent = Color(nsColor: NSColor(name: nil) {
        $0.isDark ? NSColor(srgbRed: 0.82, green: 0.22, blue: 0.92, alpha: 1)
                  : NSColor(srgbRed: 0.68, green: 0.13, blue: 0.78, alpha: 1)
    })
    /// The grouped page behind cards; in dark mode cards are lighter than the page, as on iOS.
    static let page = Color(nsColor: NSColor(name: nil) { $0.isDark ? NSColor(white: 0.11, alpha: 1) : .windowBackgroundColor })
    static let card = Color(nsColor: NSColor(name: nil) { $0.isDark ? NSColor(white: 0.18, alpha: 1) : .controlBackgroundColor })
}

private extension NSAppearance {
    var isDark: Bool { bestMatch(from: [.darkAqua, .aqua]) == .darkAqua }
}

/// A rounded group of rows or content on the page.
struct Card<Content: View>: View {
    var padding: CGFloat = 16
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 0) { content }
            .padding(padding)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.card, in: .rect(cornerRadius: 16))
            .overlay(RoundedRectangle(cornerRadius: 16).strokeBorder(.separator.opacity(0.6), lineWidth: 0.5))
    }
}

struct SectionTitle: View {
    let title: String
    var trailing: String?

    init(_ title: String, trailing: String? = nil) {
        self.title = title
        self.trailing = trailing
    }

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Text(title).font(.title3.weight(.semibold))
            Spacer()
            if let trailing { Text(trailing).font(.footnote).foregroundStyle(.secondary) }
        }
    }
}

/// The small text under a card.
struct CardFooter: View {
    let text: String
    init(_ text: String) { self.text = text }

    var body: some View {
        Text(text)
            .font(.footnote)
            .foregroundStyle(.secondary)
            .padding(.horizontal, 4)
            .fixedSize(horizontal: false, vertical: true)
    }
}

struct IconWell: View {
    var symbol = "sparkles"
    var size: CGFloat = 40
    var tint = Theme.accent

    var body: some View {
        Image(systemName: symbol)
            .font(.system(size: size * 0.45, weight: .medium))
            .foregroundStyle(tint)
            .frame(width: size, height: size)
            .background(tint.opacity(0.14), in: .circle)
    }
}

/// The trailing status of a core row.
struct StatusBadge: View {
    let session: CoreSession

    var body: some View {
        Group {
            if session.isBusy {
                ProgressView().controlSize(.small)
            } else {
                switch session.primary {
                case .upToDate, .installed: Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
                case .update, .install: Image(systemName: "arrow.down.circle.fill").foregroundStyle(Theme.accent)
                case .chooseBoard: Image(systemName: "questionmark.circle.fill").foregroundStyle(.orange)
                case .check:
                    if session.failure != nil { Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange) }
                case .working: EmptyView()
                }
            }
        }
        .font(.title3)
        .frame(width: 24)
    }
}

/// A chip to pick one of a few options; the selected one gets an accent stroke.
struct Chip: View {
    let title: String
    let selected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.callout.weight(.medium))
                .padding(.horizontal, 14)
                .padding(.vertical, 8)
                .background(Theme.card, in: .rect(cornerRadius: 12))
                .overlay(RoundedRectangle(cornerRadius: 12)
                    .strokeBorder(selected ? Theme.accent : Color.secondary.opacity(0.3), lineWidth: selected ? 2 : 1))
                .contentShape(.rect(cornerRadius: 12))
        }
        .buttonStyle(.plain)
    }
}

struct LogView: View {
    var text: String

    var body: some View {
        ScrollView {
            Text(text.isEmpty ? "No output yet." : text)
                .font(.system(.caption, design: .monospaced))
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(8)
        }
        .defaultScrollAnchor(.bottom)
        .frame(height: 160)
        .background(.quaternary.opacity(0.5), in: .rect(cornerRadius: 10))
    }
}

struct UpdateBanner: View {
    var update: FlasherRelease

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "arrow.down.app.fill").foregroundStyle(Theme.accent).font(.title2)
            Text("A new ShinerCore Flasher (\(update.version)) is available.")
            Spacer()
            Link("Download", destination: update.page)
        }
        .padding(12)
        .background(Theme.accent.opacity(0.12), in: .rect(cornerRadius: 16))
    }
}

// MARK: - Copy

extension FirmwareBuild {
    var dayMonth: String { date.formatted(.dateTime.day().month(.abbreviated)) }

    func label(latest: Bool) -> String {
        "\(shortCommit) · \(dayMonth) · \(subject)\(latest ? " (latest)" : "")"
    }
}

extension BoardSelection {
    func caption(chosen: Bool) -> String {
        if chosen { return "Choose the model printed on the case." }
        switch reason {
        case .running: return "Read from the ShinerCore that the core runs."
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
                ? "Checked for new ShinerCore just now"
                : "Checked for new ShinerCore \(formatter.localizedString(for: checkedAt, relativeTo: now))"
        case .offline(let cachedAt?, _):
            return "Offline, using ShinerCore saved on \(cachedAt.formatted(date: .abbreviated, time: .shortened))"
        case .offline(nil, let reason):
            return "Can't reach GitHub, and no ShinerCore is saved yet. Connect to the internet. (\(reason))"
        case .localDirectory(let directory):
            return "Using the firmware build in \(directory.path)"
        }
    }
}
