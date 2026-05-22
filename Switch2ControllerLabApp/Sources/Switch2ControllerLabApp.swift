import AppKit
import ApplicationServices
import CoreGraphics
import Foundation
import Network
import SwiftUI

private let defaultBridgePort: UInt16 = 28782

struct ControllerPacket: Codable, Equatable {
    var version: Int
    var sequence: Int64
    var timestamp: Double
    var vendorId: Int
    var productId: Int
    var buttonMask: UInt32
    var buttons0: Int
    var buttons1: Int
    var buttons2: Int
    var leftStickX: Double
    var leftStickY: Double
    var rightStickX: Double
    var rightStickY: Double
    var leftTrigger: Double
    var rightTrigger: Double

    static let empty = ControllerPacket(
        version: 1,
        sequence: 0,
        timestamp: 0,
        vendorId: 0x057e,
        productId: 0x2069,
        buttonMask: 0,
        buttons0: 0,
        buttons1: 0,
        buttons2: 0,
        leftStickX: 0,
        leftStickY: 0,
        rightStickX: 0,
        rightStickY: 0,
        leftTrigger: 0,
        rightTrigger: 0
    )

    func isPressed(_ button: ControllerButton) -> Bool {
        switch button.byteIndex {
        case 0:
            return (buttons0 & button.mask) != 0
        case 1:
            return (buttons1 & button.mask) != 0
        default:
            return (buttons2 & button.mask) != 0
        }
    }
}

enum ControllerButton: String, CaseIterable, Identifiable {
    case b = "B"
    case a = "A"
    case y = "Y"
    case x = "X"
    case l = "L"
    case r = "R"
    case zl = "ZL"
    case zr = "ZR"
    case minus = "Minus"
    case plus = "Plus"
    case leftStick = "Left Stick"
    case rightStick = "Right Stick"
    case dpadUp = "D-Pad Up"
    case dpadDown = "D-Pad Down"
    case dpadLeft = "D-Pad Left"
    case dpadRight = "D-Pad Right"

    var id: String { rawValue }

    var byteIndex: Int {
        switch self {
        case .b, .a, .y, .x, .r, .zr, .plus, .rightStick:
            return 0
        default:
            return 1
        }
    }

    var mask: Int {
        switch self {
        case .b: return 0x01
        case .a: return 0x02
        case .y: return 0x04
        case .x: return 0x08
        case .r: return 0x10
        case .zr: return 0x20
        case .plus: return 0x40
        case .rightStick: return 0x80
        case .dpadDown: return 0x01
        case .dpadRight: return 0x02
        case .dpadLeft: return 0x04
        case .dpadUp: return 0x08
        case .l: return 0x10
        case .zl: return 0x20
        case .minus: return 0x40
        case .leftStick: return 0x80
        }
    }
}

final class UDPControllerReceiver {
    private var listener: NWListener?
    private let queue = DispatchQueue(label: "app.switch2controllerlab.udp")
    private let onPacket: (ControllerPacket) -> Void
    private let onError: (String) -> Void

    init(port: UInt16, onPacket: @escaping (ControllerPacket) -> Void, onError: @escaping (String) -> Void) throws {
        self.onPacket = onPacket
        self.onError = onError

        let params = NWParameters.udp
        listener = try NWListener(using: params, on: NWEndpoint.Port(rawValue: port)!)
        listener?.newConnectionHandler = { [weak self] connection in
            self?.handle(connection)
        }
        listener?.stateUpdateHandler = { [weak self] state in
            switch state {
            case .failed(let error):
                self?.onError("UDP listener failed: \(error)")
            case .cancelled:
                break
            default:
                break
            }
        }
    }

    func start() {
        listener?.start(queue: queue)
    }

    func stop() {
        listener?.cancel()
        listener = nil
    }

    private func handle(_ connection: NWConnection) {
        connection.start(queue: queue)
        receive(on: connection)
    }

    private func receive(on connection: NWConnection) {
        connection.receiveMessage { [weak self] data, _, _, error in
            guard let self else { return }

            if let error {
                self.onError("UDP receive failed: \(error)")
                return
            }

            if let data, !data.isEmpty {
                do {
                    let packet = try JSONDecoder().decode(ControllerPacket.self, from: data)
                    self.onPacket(packet)
                } catch {
                    self.onError("Invalid UDP packet: \(error.localizedDescription)")
                }
            }

            self.receive(on: connection)
        }
    }
}

struct MouseSettings {
    var leftClickButton: ControllerButton = .a
    var rightClickButton: ControllerButton = .b
    var sensitivity: Double = 9.0
    var deadzone: Double = 0.14
    var scrollWithTriggers: Bool = true
    var scrollSensitivity: Double = 8.0
}

final class MouseModeController {
    private var timer: Timer?
    private var latestPacket = ControllerPacket.empty
    private var settings = MouseSettings()
    private var leftMouseDown = false
    private var rightMouseDown = false

    func setEnabled(_ enabled: Bool) {
        if enabled {
            promptForAccessibility()
            startTimer()
        } else {
            stopTimer()
            releaseMouseButtons()
        }
    }

    func update(packet: ControllerPacket, settings: MouseSettings, enabled: Bool) {
        latestPacket = packet
        self.settings = settings

        guard enabled else { return }

        updateMouseButton(.left, isDown: packet.isPressed(settings.leftClickButton), state: &leftMouseDown)
        updateMouseButton(.right, isDown: packet.isPressed(settings.rightClickButton), state: &rightMouseDown)
    }

    private func startTimer() {
        if timer != nil { return }
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            self?.tick()
        }
    }

    private func stopTimer() {
        timer?.invalidate()
        timer = nil
    }

    private func tick() {
        let x = applyDeadzone(latestPacket.leftStickX)
        let y = applyDeadzone(latestPacket.leftStickY)

        if x != 0 || y != 0 {
            moveMouse(deltaX: x * settings.sensitivity, deltaY: -y * settings.sensitivity)
        }

        if settings.scrollWithTriggers {
            let scroll = (latestPacket.leftTrigger - latestPacket.rightTrigger) * settings.scrollSensitivity
            if abs(scroll) >= 0.5 {
                postScroll(amount: Int32(scroll))
            }
        }
    }

    private func applyDeadzone(_ value: Double) -> Double {
        abs(value) < settings.deadzone ? 0 : value
    }

    private func moveMouse(deltaX: Double, deltaY: Double) {
        guard let event = CGEvent(source: nil) else { return }
        let current = event.location
        let next = CGPoint(x: current.x + deltaX, y: current.y + deltaY)
        CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: next, mouseButton: .left)?
            .post(tap: .cghidEventTap)
    }

    private func updateMouseButton(_ button: CGMouseButton, isDown: Bool, state: inout Bool) {
        guard isDown != state else { return }
        state = isDown

        guard let event = CGEvent(source: nil) else { return }
        let position = event.location
        let type: CGEventType
        switch (button, isDown) {
        case (.left, true): type = .leftMouseDown
        case (.left, false): type = .leftMouseUp
        case (.right, true): type = .rightMouseDown
        case (.right, false): type = .rightMouseUp
        default: type = isDown ? .otherMouseDown : .otherMouseUp
        }

        CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: position, mouseButton: button)?
            .post(tap: .cghidEventTap)
    }

    private func postScroll(amount: Int32) {
        CGEvent(
            scrollWheelEvent2Source: nil,
            units: .pixel,
            wheelCount: 1,
            wheel1: amount,
            wheel2: 0,
            wheel3: 0
        )?.post(tap: .cghidEventTap)
    }

    private func releaseMouseButtons() {
        updateMouseButton(.left, isDown: false, state: &leftMouseDown)
        updateMouseButton(.right, isDown: false, state: &rightMouseDown)
    }

    private func promptForAccessibility() {
        let key = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
        AXIsProcessTrustedWithOptions([key: true] as CFDictionary)
    }
}

@MainActor
final class ControllerLabModel: ObservableObject {
    @Published var bridgePort: UInt16 = defaultBridgePort
    @Published var isListening = false
    @Published var isHelperRunning = false
    @Published var latestPacket = ControllerPacket.empty
    @Published var lastPacketDate: Date?
    @Published var statusText = "Idle"
    @Published var logs: [String] = []
    @Published var mouseModeEnabled = false {
        didSet {
            mouseController.setEnabled(mouseModeEnabled)
            mouseController.update(packet: latestPacket, settings: mouseSettings, enabled: mouseModeEnabled)
        }
    }
    @Published var mouseSettings = MouseSettings() {
        didSet {
            mouseController.update(packet: latestPacket, settings: mouseSettings, enabled: mouseModeEnabled)
        }
    }

    private var receiver: UDPControllerReceiver?
    private var helperProcess: Process?
    private let mouseController = MouseModeController()

    var hasRecentPacket: Bool {
        guard let lastPacketDate else { return false }
        return Date().timeIntervalSince(lastPacketDate) < 1.5
    }

    func startBridge() {
        startListening()
        startHelper()
    }

    func stopBridge() {
        stopHelper()
        stopListening()
    }

    func startListening() {
        guard !isListening else { return }

        do {
            receiver = try UDPControllerReceiver(
                port: bridgePort,
                onPacket: { [weak self] packet in
                    Task { @MainActor in
                        self?.handle(packet)
                    }
                },
                onError: { [weak self] message in
                    Task { @MainActor in
                        self?.appendLog(message)
                    }
                }
            )
            receiver?.start()
            isListening = true
            statusText = "Listening on UDP \(bridgePort)"
            appendLog("Listening on UDP port \(bridgePort).")
        } catch {
            statusText = "UDP listener failed"
            appendLog("Could not listen on UDP port \(bridgePort): \(error.localizedDescription)")
        }
    }

    func stopListening() {
        receiver?.stop()
        receiver = nil
        isListening = false
        if !isHelperRunning {
            statusText = "Idle"
        }
        appendLog("Stopped UDP listener.")
    }

    func startHelper() {
        guard !isHelperRunning else { return }
        guard let helperURL = bundledHelperURL() else {
            statusText = "Helper missing"
            appendLog("Could not find bundled switch2_mac_bridge helper.")
            return
        }

        let process = Process()
        process.executableURL = helperURL
        process.arguments = ["--udp", "127.0.0.1:\(bridgePort)"]

        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            guard !data.isEmpty, let text = String(data: data, encoding: .utf8) else { return }
            Task { @MainActor in
                self?.appendHelperOutput(text)
            }
        }

        process.terminationHandler = { [weak self] terminatedProcess in
            Task { @MainActor in
                self?.isHelperRunning = false
                self?.statusText = "Helper exited with code \(terminatedProcess.terminationStatus)"
                self?.appendLog("Helper exited with code \(terminatedProcess.terminationStatus).")
            }
        }

        do {
            try process.run()
            helperProcess = process
            isHelperRunning = true
            statusText = "Helper running"
            appendLog("Started helper: \(helperURL.path)")
        } catch {
            statusText = "Helper failed"
            appendLog("Could not start helper: \(error.localizedDescription)")
        }
    }

    func stopHelper() {
        guard let helperProcess else { return }
        helperProcess.terminate()
        self.helperProcess = nil
        isHelperRunning = false
        appendLog("Stopped helper.")
    }

    func appendLog(_ line: String) {
        logs.append(line)
        if logs.count > 160 {
            logs.removeFirst(logs.count - 160)
        }
    }

    private func appendHelperOutput(_ output: String) {
        for line in output.split(whereSeparator: \.isNewline) {
            appendLog(String(line))
        }
    }

    private func handle(_ packet: ControllerPacket) {
        latestPacket = packet
        lastPacketDate = Date()
        statusText = "Live input"
        mouseController.update(packet: packet, settings: mouseSettings, enabled: mouseModeEnabled)
    }

    private func bundledHelperURL() -> URL? {
        let helperURL = Bundle.main.bundleURL
            .appendingPathComponent("Contents")
            .appendingPathComponent("MacOS")
            .appendingPathComponent("switch2_mac_bridge")

        if FileManager.default.isExecutableFile(atPath: helperURL.path) {
            return helperURL
        }

        return nil
    }
}

struct ContentView: View {
    @StateObject private var model = ControllerLabModel()

    var body: some View {
        VStack(spacing: 0) {
            HeaderView(model: model)
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    ControllerLiveView(packet: model.latestPacket, isLive: model.hasRecentPacket)
                    MouseModeView(model: model)
                    LogView(logs: model.logs)
                }
                .padding(18)
            }
        }
        .frame(minWidth: 920, minHeight: 720)
        .onDisappear {
            model.stopBridge()
        }
    }
}

struct HeaderView: View {
    @ObservedObject var model: ControllerLabModel

    var body: some View {
        HStack(spacing: 14) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Switch2 Controller Lab")
                    .font(.system(size: 24, weight: .semibold))
                Text(model.statusText)
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            StatusPill(title: "UDP", isActive: model.isListening)
            StatusPill(title: "Helper", isActive: model.isHelperRunning)
            StatusPill(title: "Input", isActive: model.hasRecentPacket)

            Button(model.isHelperRunning || model.isListening ? "Stop Bridge" : "Start Bridge") {
                if model.isHelperRunning || model.isListening {
                    model.stopBridge()
                } else {
                    model.startBridge()
                }
            }
            .buttonStyle(.borderedProminent)
        }
        .padding(18)
    }
}

struct StatusPill: View {
    let title: String
    let isActive: Bool

    var body: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(isActive ? Color.green : Color.gray.opacity(0.45))
                .frame(width: 8, height: 8)
            Text(title)
                .font(.caption.weight(.semibold))
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(.quaternary, in: RoundedRectangle(cornerRadius: 8))
    }
}

struct ControllerLiveView: View {
    let packet: ControllerPacket
    let isLive: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Controller")
                    .font(.headline)
                Spacer()
                Text(isLive ? "Live" : "Waiting")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(isLive ? .green : .secondary)
            }

            HStack(alignment: .top, spacing: 22) {
                VStack(spacing: 16) {
                    ShoulderRow(packet: packet)
                    HStack(spacing: 22) {
                        StickView(title: "Left Stick", x: packet.leftStickX, y: packet.leftStickY)
                        DPadView(packet: packet)
                        FaceButtonsView(packet: packet)
                        StickView(title: "Right Stick", x: packet.rightStickX, y: packet.rightStickY)
                    }
                    TriggerRow(packet: packet)
                }

                VStack(alignment: .leading, spacing: 8) {
                    Text("Raw State")
                        .font(.subheadline.weight(.semibold))
                    Text("Sequence \(packet.sequence)")
                    Text(String(format: "Buttons %02X %02X %02X", packet.buttons0, packet.buttons1, packet.buttons2))
                    Text(String(format: "Left %.2f, %.2f", packet.leftStickX, packet.leftStickY))
                    Text(String(format: "Right %.2f, %.2f", packet.rightStickX, packet.rightStickY))
                    Text(String(format: "Triggers %.2f, %.2f", packet.leftTrigger, packet.rightTrigger))
                }
                .font(.system(.body, design: .monospaced))
                .padding(12)
                .frame(width: 240, alignment: .leading)
                .background(.quaternary, in: RoundedRectangle(cornerRadius: 8))
            }
        }
        .sectionPanel()
    }
}

struct ShoulderRow: View {
    let packet: ControllerPacket

    var body: some View {
        HStack(spacing: 10) {
            ButtonChip("ZL", active: packet.isPressed(.zl))
            ButtonChip("L", active: packet.isPressed(.l))
            Spacer()
            ButtonChip("R", active: packet.isPressed(.r))
            ButtonChip("ZR", active: packet.isPressed(.zr))
        }
    }
}

struct TriggerRow: View {
    let packet: ControllerPacket

    var body: some View {
        HStack(spacing: 14) {
            TriggerMeter(title: "Left Trigger", value: packet.leftTrigger)
            TriggerMeter(title: "Right Trigger", value: packet.rightTrigger)
        }
    }
}

struct TriggerMeter: View {
    let title: String
    let value: Double

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.caption)
            GeometryReader { proxy in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 5)
                        .fill(.quaternary)
                    RoundedRectangle(cornerRadius: 5)
                        .fill(Color.blue)
                        .frame(width: proxy.size.width * max(0, min(1, value)))
                }
            }
            .frame(height: 10)
        }
    }
}

struct StickView: View {
    let title: String
    let x: Double
    let y: Double

    var body: some View {
        VStack(spacing: 8) {
            ZStack {
                Circle()
                    .stroke(.secondary.opacity(0.5), lineWidth: 2)
                    .background(Circle().fill(.quaternary))
                Circle()
                    .fill(Color.accentColor)
                    .frame(width: 18, height: 18)
                    .offset(x: CGFloat(max(-1, min(1, x))) * 42, y: CGFloat(-max(-1, min(1, y))) * 42)
            }
            .frame(width: 112, height: 112)
            Text(title)
                .font(.caption)
        }
    }
}

struct DPadView: View {
    let packet: ControllerPacket

    var body: some View {
        VStack(spacing: 5) {
            ButtonChip("Up", active: packet.isPressed(.dpadUp))
            HStack(spacing: 5) {
                ButtonChip("Left", active: packet.isPressed(.dpadLeft))
                ButtonChip("Right", active: packet.isPressed(.dpadRight))
            }
            ButtonChip("Down", active: packet.isPressed(.dpadDown))
            Text("D-Pad")
                .font(.caption)
        }
        .frame(width: 130)
    }
}

struct FaceButtonsView: View {
    let packet: ControllerPacket

    var body: some View {
        VStack(spacing: 5) {
            ButtonChip("X", active: packet.isPressed(.x))
            HStack(spacing: 5) {
                ButtonChip("Y", active: packet.isPressed(.y))
                ButtonChip("A", active: packet.isPressed(.a))
            }
            ButtonChip("B", active: packet.isPressed(.b))
            HStack(spacing: 5) {
                ButtonChip("-", active: packet.isPressed(.minus))
                ButtonChip("+", active: packet.isPressed(.plus))
            }
        }
        .frame(width: 130)
    }
}

struct ButtonChip: View {
    let title: String
    let active: Bool

    init(_ title: String, active: Bool) {
        self.title = title
        self.active = active
    }

    var body: some View {
        Text(title)
            .font(.caption.weight(.semibold))
            .frame(minWidth: 42, minHeight: 28)
            .padding(.horizontal, 4)
            .background(active ? Color.accentColor : Color.secondary.opacity(0.16), in: RoundedRectangle(cornerRadius: 8))
            .foregroundStyle(active ? .white : .primary)
    }
}

struct MouseModeView: View {
    @ObservedObject var model: ControllerLabModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Toggle("Mouse Mode", isOn: $model.mouseModeEnabled)
                .font(.headline)

            HStack(spacing: 20) {
                Picker("Left Click", selection: mouseSetting(\.leftClickButton)) {
                    ForEach(ControllerButton.allCases) { button in
                        Text(button.rawValue).tag(button)
                    }
                }
                .frame(width: 210)

                Picker("Right Click", selection: mouseSetting(\.rightClickButton)) {
                    ForEach(ControllerButton.allCases) { button in
                        Text(button.rawValue).tag(button)
                    }
                }
                .frame(width: 210)

                Toggle("Triggers Scroll", isOn: mouseSetting(\.scrollWithTriggers))
            }

            HStack(spacing: 22) {
                VStack(alignment: .leading) {
                    Text("Pointer Sensitivity")
                    Slider(value: mouseSetting(\.sensitivity), in: 1...22)
                }
                VStack(alignment: .leading) {
                    Text("Deadzone")
                    Slider(value: mouseSetting(\.deadzone), in: 0...0.4)
                }
                VStack(alignment: .leading) {
                    Text("Scroll Sensitivity")
                    Slider(value: mouseSetting(\.scrollSensitivity), in: 1...24)
                }
            }
        }
        .sectionPanel()
    }

    private func mouseSetting<Value>(_ keyPath: WritableKeyPath<MouseSettings, Value>) -> Binding<Value> {
        Binding(
            get: { model.mouseSettings[keyPath: keyPath] },
            set: { newValue in
                var settings = model.mouseSettings
                settings[keyPath: keyPath] = newValue
                model.mouseSettings = settings
            }
        )
    }
}

struct LogView: View {
    let logs: [String]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Helper Log")
                .font(.headline)

            ScrollView {
                VStack(alignment: .leading, spacing: 3) {
                    ForEach(Array(logs.enumerated()), id: \.offset) { _, line in
                        Text(line)
                            .font(.system(size: 11, design: .monospaced))
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
                .padding(10)
            }
            .frame(height: 180)
            .background(Color.black.opacity(0.08), in: RoundedRectangle(cornerRadius: 8))
        }
        .sectionPanel()
    }
}

extension View {
    func sectionPanel() -> some View {
        self
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(.background, in: RoundedRectangle(cornerRadius: 8))
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(.separator.opacity(0.6), lineWidth: 1)
            )
    }
}

@main
struct Switch2ControllerLabApplication: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
        .windowStyle(.titleBar)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
