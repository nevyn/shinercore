#!/usr/bin/env swift
// Draws the app icon (a ring of glowing LEDs around an install arrow, on a dark tile) and writes
// it as an .icns. The PNGs stay in a temporary iconset for previews.
// Usage: swift scripts/make-icon.swift app/AppIcon.icns
import AppKit

let accent = (r: 0.82, g: 0.22, b: 0.92)
guard CommandLine.arguments.count == 2 else {
    FileHandle.standardError.write(Data("usage: make-icon.swift OUTPUT.icns\n".utf8))
    exit(64)
}
let output = URL(fileURLWithPath: CommandLine.arguments[1])

func color(_ r: Double, _ g: Double, _ b: Double, _ a: Double = 1) -> CGColor { CGColor(srgbRed: r, green: g, blue: b, alpha: a) }

/// Draws the icon on a 1024-point canvas; `pixels` picks the detail level.
func draw(_ context: CGContext, pixels: Int) {
    let small = pixels <= 32
    let tiny = pixels <= 16  // a 13-pixel tile: fewer, bigger dots and no tray
    // macOS icon grid: an 824-point tile centered on the 1024 canvas.
    let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
    let shape = CGPath(roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)
    context.saveGState()
    context.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: color(0, 0, 0, 0.35))
    context.addPath(shape)
    context.setFillColor(color(0.05, 0.03, 0.07))
    context.fillPath()
    context.restoreGState()

    context.saveGState()
    context.addPath(shape)
    context.clip()
    let center = CGPoint(x: 512, y: 512)
    let ground = CGGradient(colorsSpace: nil, colors: [color(0.20, 0.07, 0.24), color(0.04, 0.02, 0.06)] as CFArray, locations: [0, 1])!
    context.drawRadialGradient(ground, startCenter: center, startRadius: 0, endCenter: center, endRadius: 520, options: [])

    // The LED ring, brightest at the top and fading around like a chase.
    let count = tiny ? 6 : small ? 8 : 12
    let ringRadius: CGFloat = small ? 290 : 300
    let dotRadius: CGFloat = tiny ? 80 : small ? 58 : 34
    for index in 0..<count {
        let angle = .pi / 2 - Double(index) / Double(count) * 2 * .pi
        let level = small ? 1 : 0.35 + 0.65 * pow(1 - Double(index) / Double(count), 1.5)
        let dot = CGPoint(x: center.x + ringRadius * cos(angle), y: center.y + ringRadius * sin(angle))
        if !small {
            let glow = CGGradient(colorsSpace: nil, colors: [color(accent.r, accent.g, accent.b, 0.55 * level), color(accent.r, accent.g, accent.b, 0)] as CFArray,
                                  locations: [0, 1])!
            context.drawRadialGradient(glow, startCenter: dot, startRadius: 0, endCenter: dot, endRadius: dotRadius * 3.2, options: [])
        }
        func mix(_ a: Double, _ b: Double) -> Double { a + (b - a) * level }
        let hot = color(mix(accent.r * 0.7, 1), mix(accent.g * 0.7, 0.9), mix(accent.b * 0.7, 1))
        let rim = color(accent.r * mix(0.35, 1), accent.g * mix(0.35, 1), accent.b * mix(0.35, 1))
        let core = CGGradient(colorsSpace: nil, colors: [hot, rim] as CFArray, locations: [0, 1])!
        context.saveGState()
        context.addEllipse(in: CGRect(x: dot.x - dotRadius, y: dot.y - dotRadius, width: dotRadius * 2, height: dotRadius * 2))
        context.clip()
        context.drawRadialGradient(core, startCenter: dot, startRadius: 0, endCenter: dot, endRadius: dotRadius, options: [])
        context.restoreGState()
    }

    // The install arrow, pointing down onto a tray.
    let white = color(1, 0.94, 1)
    let line: CGFloat = tiny ? 120 : small ? 74 : 56
    context.setStrokeColor(white)
    context.setLineWidth(line)
    context.setLineCap(.round)
    context.setLineJoin(.round)
    let top: CGFloat = tiny ? 650 : small ? 690 : 660
    let tip: CGFloat = tiny ? 370 : small ? 410 : 430
    let head: CGFloat = tiny ? 130 : small ? 120 : 110
    context.move(to: CGPoint(x: 512, y: top))
    context.addLine(to: CGPoint(x: 512, y: tip))
    context.move(to: CGPoint(x: 512 - head, y: tip + head))
    context.addLine(to: CGPoint(x: 512, y: tip))
    context.addLine(to: CGPoint(x: 512 + head, y: tip + head))
    context.strokePath()
    guard !tiny else {
        context.restoreGState()
        return
    }
    let trayY: CGFloat = small ? 330 : 350
    let trayHalf: CGFloat = small ? 150 : 140
    context.move(to: CGPoint(x: 512 - trayHalf, y: trayY))
    context.addLine(to: CGPoint(x: 512 + trayHalf, y: trayY))
    context.strokePath()
    context.restoreGState()
}

func png(pixels: Int) -> Data {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels, bitsPerSample: 8, samplesPerPixel: 4,
                               hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    let context = NSGraphicsContext(bitmapImageRep: rep)!.cgContext
    context.scaleBy(x: CGFloat(pixels) / 1024, y: CGFloat(pixels) / 1024)
    draw(context, pixels: pixels)
    return rep.representation(using: .png, properties: [:])!
}

let iconset = FileManager.default.temporaryDirectory.appending(path: "AppIcon-\(UUID().uuidString).iconset")
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for points in [16, 32, 128, 256, 512] {
    try png(pixels: points).write(to: iconset.appending(path: "icon_\(points)x\(points).png"))
    try png(pixels: points * 2).write(to: iconset.appending(path: "icon_\(points)x\(points)@2x.png"))
}
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", output.path]
try iconutil.run()
iconutil.waitUntilExit()
guard iconutil.terminationStatus == 0 else { fatalError("iconutil failed for \(iconset.path)") }
print("wrote \(output.path); the iconset is in \(iconset.path)")
