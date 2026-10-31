import Foundation
import IOKit
import IOKit.serial

/// A USB serial port that a core may sit behind.
public struct SerialDevice: Hashable, Sendable, Identifiable {
    /// USB vendor ids of Espressif's built-in USB and the common USB-serial bridges.
    public static let knownVendors: Set<Int> = [0x303A, 0x0403, 0x1A86, 0x10C4]

    /// The callout device, such as `/dev/cu.usbmodem1101`.
    public var path: String
    public var vendorID: Int
    public var productID: Int
    public var productName: String?
    public var serialNumber: String?

    public init(path: String, vendorID: Int, productID: Int, productName: String? = nil, serialNumber: String? = nil) {
        self.path = path
        self.vendorID = vendorID
        self.productID = productID
        self.productName = productName
        self.serialNumber = serialNumber
    }

    public var id: String { path }
    public var isKnownVendor: Bool { Self.knownVendors.contains(vendorID) }

    /// Known vendors first, then by path.
    public static func sorted(_ devices: [SerialDevice]) -> [SerialDevice] {
        devices.sorted { ($0.isKnownVendor ? 0 : 1, $0.path) < ($1.isKnownVendor ? 0 : 1, $1.path) }
    }
}

/// Lists USB serial ports and reports every plug and unplug.
public final class SerialPortMonitor {
    private let onChange: ([SerialDevice]) -> Void
    private var notifyPort: IONotificationPortRef?
    private var iterators: [io_iterator_t] = []

    /// Starts watching; `onChange` is called on the main run loop with the full, sorted list,
    /// once right away and after every change.
    public init(onChange: @escaping ([SerialDevice]) -> Void) {
        self.onChange = onChange
        guard let port = IONotificationPortCreate(kIOMainPortDefault) else {
            onChange(Self.devices())
            return
        }
        notifyPort = port
        CFRunLoopAddSource(CFRunLoopGetMain(), IONotificationPortGetRunLoopSource(port).takeUnretainedValue(), .defaultMode)
        let context = Unmanaged.passUnretained(self).toOpaque()
        let callback: IOServiceMatchingCallback = { context, iterator in
            SerialPortMonitor.drain(iterator)
            Unmanaged<SerialPortMonitor>.fromOpaque(context!).takeUnretainedValue().rescan()
        }
        for type in [kIOFirstMatchNotification, kIOTerminatedNotification] {
            var iterator: io_iterator_t = 0
            if IOServiceAddMatchingNotification(port, type, IOServiceMatching(kIOSerialBSDServiceValue), callback, context, &iterator) == KERN_SUCCESS {
                Self.drain(iterator)  // arms the notification
                iterators.append(iterator)
            }
        }
        rescan()
    }

    deinit {
        iterators.forEach { IOObjectRelease($0) }
        if let notifyPort { IONotificationPortDestroy(notifyPort) }
    }

    private func rescan() { onChange(Self.devices()) }

    private static func drain(_ iterator: io_iterator_t) {
        while case let service = IOIteratorNext(iterator), service != 0 { IOObjectRelease(service) }
    }

    /// Every serial port with a USB device above it, sorted. Bluetooth and debug-console ports
    /// have no USB parent and are skipped.
    public static func devices() -> [SerialDevice] {
        var iterator: io_iterator_t = 0
        guard IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(kIOSerialBSDServiceValue), &iterator) == KERN_SUCCESS else { return [] }
        defer { IOObjectRelease(iterator) }
        var devices: [SerialDevice] = []
        while case let service = IOIteratorNext(iterator), service != 0 {
            defer { IOObjectRelease(service) }
            guard let path = property(service, kIOCalloutDeviceKey, searchParents: false) as? String,
                  let vendor = property(service, "idVendor") as? Int,
                  let product = property(service, "idProduct") as? Int else { continue }
            devices.append(SerialDevice(path: path, vendorID: vendor, productID: product,
                                        productName: property(service, "USB Product Name") as? String,
                                        serialNumber: property(service, "USB Serial Number") as? String))
        }
        return SerialDevice.sorted(devices)
    }

    private static func property(_ service: io_object_t, _ key: String, searchParents: Bool = true) -> Any? {
        let options = searchParents ? IOOptionBits(kIORegistryIterateRecursively | kIORegistryIterateParents) : 0
        return IORegistryEntrySearchCFProperty(service, kIOServicePlane, key as CFString, kCFAllocatorDefault, options)
    }
}
