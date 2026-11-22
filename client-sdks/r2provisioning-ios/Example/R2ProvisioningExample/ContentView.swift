import SwiftUI
import r2provisioning

struct ContentView: View {
    @StateObject private var client = R2ProvisioningClient()

    @State private var ssid = ""
    @State private var password = ""
    @State private var customData = ""
    @State private var isSending = false
    @State private var errorMessage: String?

    var body: some View {
        NavigationStack {
            Form {
                deviceSection
                if client.isProvisioned {
                    Section {
                        Label("Device joined \(ssid) and is restarting.", systemImage: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                    }
                } else {
                    credentialsSection
                }
            }
            .navigationTitle("r2 Provisioning")
        }
        .onAppear { client.start() }
        .onDisappear { client.stop() }
    }

    private var deviceSection: some View {
        Section("Device") {
            HStack {
                Circle()
                    .fill(client.isConnected ? .green : .red)
                    .frame(width: 10, height: 10)
                Text(client.isConnected ? "Connected" : "Not reachable")
            }
            if let info = client.deviceInfo {
                LabeledContent("ID", value: info.id)
                LabeledContent("Firmware", value: "\(info.name) \(info.version)")
                LabeledContent("State", value: info.state.rawValue)
                if let error = info.error {
                    LabeledContent("Last error", value: error)
                }
            } else if !client.isProvisioned {
                Text("Join the device's \"r2-XXXXXX\" WiFi hotspot in Settings, then return here.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var credentialsSection: some View {
        Section {
            TextField("SSID", text: $ssid)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
            SecureField("Password", text: $password)
            TextField("Custom data (UTF-8, optional)", text: $customData)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()

            Button {
                Task { await send() }
            } label: {
                HStack {
                    Text(isSending ? "Waiting for device to join…" : "Provision")
                    if isSending {
                        Spacer()
                        ProgressView()
                    }
                }
            }
            .disabled(!client.isConnected || ssid.isEmpty || isSending)

            if let errorMessage {
                Text(errorMessage)
                    .foregroundStyle(.red)
            }
        } header: {
            Text("WiFi network")
        } footer: {
            Text("The device keeps its hotspot up while it tries the network, so this phone may drop off briefly.")
        }
    }

    private func send() async {
        isSending = true
        errorMessage = nil
        defer { isSending = false }
        do {
            try await client.send(ssid: ssid, password: password, data: Data(customData.utf8))
        } catch {
            errorMessage = describe(error)
        }
    }

    private func describe(_ error: R2ProvisioningError) -> String {
        switch error {
        case .invalidSSID: "SSID must be 1–32 bytes."
        case .invalidPassword: "Password must be at most 64 bytes."
        case .dataTooLarge(let max): "Custom data must be at most \(max) bytes."
        case .httpStatus(409): "A join attempt is already running."
        case .httpStatus(let code): "Device rejected the request (HTTP \(code))."
        case .joinFailed(let reason): "Device could not join the network: \(reason). Check the credentials and try again."
        case .joinTimeout: "The device did not report a result in time."
        case .invalidResponse: "Unexpected response from the device."
        case .transport(let error): "Could not reach the device: \(error.localizedDescription)"
        }
    }
}

#Preview {
    ContentView()
}
