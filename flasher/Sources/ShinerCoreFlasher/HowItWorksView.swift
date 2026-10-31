import FlasherKit
import SwiftUI

/// Where to buy each board, by board id. A board missing here still shows its manifest name.
enum BoardShop {
    struct Entry {
        /// Used only when no firmware build is known yet.
        var name: String
        var page: URL
        var note: String?
    }

    static let entries: [(id: String, entry: Entry)] = [
        ("atom-lite", Entry(name: "M5Atom Lite", page: URL(string: "https://docs.m5stack.com/en/core/Atom-Lite")!)),
        ("atom-echo", Entry(name: "M5Atom Echo", page: URL(string: "https://docs.m5stack.com/en/atom/Atom_Voice")!,
                            note: "now sold as Atom Voice")),
        ("atoms3-lite", Entry(name: "M5AtomS3 Lite", page: URL(string: "https://docs.m5stack.com/en/core/AtomS3-Lite")!)),
        ("stickc-plus", Entry(name: "M5StickC Plus", page: URL(string: "https://docs.m5stack.com/en/core/StickC-Plus")!)),
    ]

    static func entry(for id: String) -> Entry? { entries.first { $0.id == id }?.entry }

    /// The boards to list: the firmware build's, or the known ones before any build has loaded.
    static func boards(in build: FirmwareBuild?) -> [(id: String, name: String)] {
        build.map { $0.boards.map { ($0.id, $0.name) } } ?? entries.map { ($0.id, $0.entry.name) }
    }
}

enum Links {
    static let remote = URL(string: "https://apps.apple.com/app/shinercore-remote/id6451475618")!
    static let home = URL(string: "https://nevyn.dev/shinercore/")!
}

/// The three steps from a bare board to lights (get a board, get the Remote, add LEDs), and a link to the home page.
struct HowItWorksSteps: View {
    let boards: [(id: String, name: String)]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Step(number: 1, symbol: "cpu", title: "Get a compatible board") {
                Text("ShinerCore runs on these small boards from M5Stack. Plug one into this Mac, and this app puts ShinerCore on it.")
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(boards, id: \.id) { board in
                        if let entry = BoardShop.entry(for: board.id) {
                            HStack(spacing: 4) {
                                Link(board.name, destination: entry.page)
                                if let note = entry.note { Text("(\(note))").foregroundStyle(.secondary) }
                            }
                        } else {
                            Text(board.name)
                        }
                    }
                }
            }
            Step(number: 2, symbol: "iphone", title: "Get ShinerCore Remote") {
                Text("Control the lights from your iPhone, iPad or Mac.")
                Link("ShinerCore Remote on the App Store", destination: Links.remote)
            }
            Step(number: 3, symbol: "light.strip.2", title: "Add LEDs") {
                Text("Any WS2812-compatible LED strip works; they're also sold as NeoPixel. Connect it the way the Wiring tab in ShinerCore Remote shows.")
            }
            Link("More about ShinerCore, with photos of what people build, at nevyn.dev/shinercore", destination: Links.home)
                .font(.footnote)
                .foregroundStyle(Theme.accent)
                .padding(.horizontal, 4)
        }
    }

    private struct Step<Content: View>: View {
        let number: Int
        let symbol: String
        let title: String
        @ViewBuilder var content: Content

        var body: some View {
            Card {
                HStack(alignment: .top, spacing: 14) {
                    IconWell(symbol: symbol)
                    VStack(alignment: .leading, spacing: 6) {
                        Text("\(number). \(title)").font(.headline)
                        content.foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    }
                    Spacer(minLength: 0)
                }
            }
        }
    }
}

/// How it works, as a sheet reachable from the toolbar and the Help menu.
struct HowItWorksSheet: View {
    let boards: [(id: String, name: String)]
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("How it works").font(.largeTitle.weight(.bold))
            HowItWorksSteps(boards: boards)
            HStack {
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(24)
        .frame(width: 480)
        .background(Theme.page)
    }
}
