// Stress the shinercore property write path: subscribe to tau, storm writes,
// yank the connection mid-flight (the wedge scenario), reconnect and verify.
// Regression check for the ACL-credit wedge (docs/ble-comms.md): unpatched
// firmware hangs on this; patched firmware prints PASS.
//
//   swiftc -o /tmp/blestorm blestorm.swift && /tmp/blestorm [name-substring]
//
// Run from a real terminal, not an agent shell — macOS TCC kills CLI Bluetooth
// without a prompt there. The optional argument restricts the first scan to a
// core whose advertised name contains it; the rescan is always pinned to the
// stormed core's identifier.
import CoreBluetooth
import Foundation

let serviceUUID = CBUUID(string: "6c0de004-629d-4717-bed5-847fddfbdc2e")
let tauUUID = CBUUID(string: "d879c81a-09f0-4a24-a66c-cebf358bb97a")

final class Test: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var peripheral: CBPeripheral?
    var tau: CBCharacteristic?
    var targetID: UUID? // the stormed core; scan2 must verify the SAME device
    var phase = "scan1" // scan1 -> storm -> yank -> scan2 -> verify
    var writesSent = 0, responses = 0, notifies = 0

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        print("bt state \(c.state.rawValue)")
        switch c.state {
        case .poweredOn: c.scanForPeripherals(withServices: [serviceUUID])
        case .unauthorized: print("FAIL bluetooth unauthorized for this process"); exit(2)
        case .poweredOff: print("FAIL bluetooth off"); exit(2)
        default: break
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi: NSNumber) {
        if let target = targetID, p.identifier != target {
            print("[\(phase)] ignoring other core \(p.name ?? "?") rssi \(rssi)")
            return
        }
        if targetID == nil, let filter = CommandLine.arguments.dropFirst().first,
           !(p.name ?? "").localizedCaseInsensitiveContains(filter) {
            print("[\(phase)] ignoring non-matching \(p.name ?? "?") rssi \(rssi)")
            return
        }
        print("[\(phase)] found \(p.name ?? "?") rssi \(rssi)")
        targetID = p.identifier
        peripheral = p
        c.stopScan()
        c.connect(p)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        print("[\(phase)] connected")
        p.delegate = self
        p.discoverServices([serviceUUID])
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        print("FAIL connect: \(error?.localizedDescription ?? "?")")
        exit(5)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = p.services?.first(where: { $0.uuid == serviceUUID }) else {
            print("FAIL no shiner service"); exit(3)
        }
        p.discoverCharacteristics([tauUUID], for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        guard let t = s.characteristics?.first(where: { $0.uuid == tauUUID }) else {
            print("FAIL no tau characteristic"); exit(4)
        }
        tau = t
        p.setNotifyValue(true, for: t)
        if phase == "scan1" {
            phase = "storm"
            print("[storm] queueing 200 writes-with-response")
            for i in 0..<200 {
                let v = String(format: "%.2f", 5.0 + Double(i % 40) * 0.1)
                p.writeValue(v.data(using: .utf8)!, for: t, type: .withResponse)
                writesSent += 1
            }
            // yank while responses and notify echoes are still in flight
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) {
                print("[yank] \(self.responses)/\(self.writesSent) responses, \(self.notifies) notifies — cancelling now")
                self.phase = "yank"
                self.central.cancelPeripheralConnection(p)
            }
        } else {
            phase = "verify"
            p.readValue(for: t)
        }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
        responses += 1
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        notifies += 1
        let s = c.value.flatMap { String(data: $0, encoding: .utf8) } ?? "?"
        if phase == "verify" {
            print("PASS reconnected and read tau = '\(s)' (storm: \(responses) responses, \(notifies) notifies)")
            exit(0)
        }
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        print("[\(phase)] disconnected (\(error?.localizedDescription ?? "clean"))")
        if phase == "yank" {
            phase = "scan2"
            DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
                print("[scan2] rescanning — does the core still advertise and answer?")
                c.scanForPeripherals(withServices: [serviceUUID])
            }
        }
    }
}

let test = Test()
DispatchQueue.main.asyncAfter(deadline: .now() + 45) {
    print("FAIL timeout in phase \(test.phase)")
    exit(1)
}
RunLoop.main.run()
