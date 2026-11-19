import SwiftUI

enum LaunchAnimation: String, CaseIterable, Identifiable {
    case wave = "Color Wave"
    case wipe = "Color Reveal"
    case fade = "Fade to Color"
    case off = "Off"

    var id: Self { self }
    static let setting = "LaunchAnimation"
}

struct GallerySettings: View {
    @AppStorage(LaunchAnimation.setting) private var animation = LaunchAnimation.wave
    @AppStorage("FillGameScreen") private var fillScreen = false
    @AppStorage("DisableOpeningAnimation") private var skipOpening = false
    let dismiss: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            GalleryPanelHeader(title: "Settings", dismiss: dismiss)
            Picker("Launch animation", selection: $animation) {
                ForEach(LaunchAnimation.allCases) { Text($0.rawValue).tag($0) }
            }
            Toggle("Fill screen when Minecraft opens", isOn: $fillScreen)
            Toggle("Animate app opening", isOn: Binding(get: { !skipOpening }, set: { skipOpening = !$0 }))
            Spacer(minLength: 0)
        }
        .padding(17)
        .frame(width: 364, height: 180)
        .galleryPanel()
    }
}

struct GalleryPanelHeader: View {
    let title: String
    let dismiss: () -> Void

    var body: some View {
        HStack {
            Text(title).font(.system(size: 17, weight: .semibold))
            Spacer()
            Button("Close", systemImage: "xmark", action: dismiss)
                .labelStyle(.iconOnly).keyboardShortcut(.cancelAction).help("Close")
        }
    }
}

extension View {
    func galleryPanel() -> some View {
        background(Color(white: 0.09), in: RoundedRectangle(cornerRadius: 15))
            .overlay(RoundedRectangle(cornerRadius: 15).strokeBorder(.white.opacity(0.12)).allowsHitTesting(false))
    }
}

struct VersionEntry: Decodable, Identifiable {
    let id: String
    let type: String
}

@MainActor
final class InstanceDraft: ObservableObject {
    @Published var name = ""
    @Published var version = ""
    @Published var loader = "Vanilla"
    @Published private(set) var versions: [VersionEntry] = []
    @Published private(set) var working = false
    @Published private(set) var message = ""

    func loadVersions() async {
        struct Manifest: Decodable { let versions: [VersionEntry] }
        do {
            let manifest: Manifest = try await get(URL(string: "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json")!)
            versions = manifest.versions.filter { $0.type == "release" }
            version = versions.first?.id ?? ""
        } catch {
            message = "Couldn’t load Minecraft versions. \(error.localizedDescription)"
        }
    }

    func create(in library: StellarLibrary) async {
        struct Fabric: Decodable {
            struct Loader: Decodable { let version: String; let stable: Bool }
            let loader: Loader
        }
        guard !working, versions.contains(where: { $0.id == version }) else { return }
        let version = self.version
        let loader = self.loader
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        working = true
        message = ""
        defer { working = false }
        do {
            var fabric: String?
            if loader == "Fabric" {
                let url = URL(string: "https://meta.fabricmc.net/v2/versions/loader")!.appendingPathComponent(version)
                let releases: [Fabric] = try await get(url)
                guard let supported = releases.first(where: { $0.loader.stable }) else {
                    message = "Fabric has no stable loader for this Minecraft release."
                    return
                }
                fabric = supported.loader.version
            }
            try Task.checkCancellation()
            try library.addInstance(name: name.isEmpty ? "Minecraft \(version)" : name,
                                    version: version, fabric: fabric)
        } catch {
            if !Task.isCancelled { message = error.localizedDescription }
        }
    }

    private func get<T: Decodable>(_ url: URL) async throws -> T {
        let request = URLRequest(url: url, timeoutInterval: 15)
        let (data, response) = try await URLSession.shared.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 200 else { throw URLError(.badServerResponse) }
        return try JSONDecoder().decode(T.self, from: data)
    }
}

struct CreateInstancePanel: View {
    @ObservedObject var library: StellarLibrary
    @StateObject private var draft = InstanceDraft()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            GalleryPanelHeader(title: "Create New Instance") { library.panel = nil }
            TextField("Instance name", text: $draft.name)
            Picker("Minecraft", selection: $draft.version) {
                if draft.versions.isEmpty { Text("Loading versions…").tag("") }
                ForEach(draft.versions) { Text($0.id).tag($0.id) }
            }
            Picker("Loader", selection: $draft.loader) {
                Text("Vanilla").tag("Vanilla")
                Text("Fabric").tag("Fabric")
            }
            if !draft.message.isEmpty {
                Text(draft.message).font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
            HStack {
                Button("More Options…") {
                    library.panel = nil
                    library.createAdvanced()
                }.help("More loaders, snapshots, and modpacks")
                Spacer()
                Button("Create", systemImage: "plus") {
                    Task { await draft.create(in: library) }
                }.keyboardShortcut(.defaultAction).disabled(draft.working || draft.version.isEmpty)
            }
        }
        .padding(17)
        .frame(width: 364)
        .galleryPanel()
        .task { await draft.loadVersions() }
    }
}

struct WallpaperLaunchOverlay: View {
    let artwork: URL?
    let progress: Double
    let animation: LaunchAnimation

    var body: some View {
        GeometryReader { area in
            ZStack {
                WallpaperImage(url: artwork).saturation(0)
                WallpaperImage(url: artwork)
                    .mask {
                        if animation == .fade { Rectangle().opacity(progress) }
                        else { ColorFill(progress: progress, waves: animation == .wave).fill(.white) }
                    }
            }
            .frame(width: area.size.width, height: area.size.height)
            .clipped()
        }
        .clipShape(RoundedRectangle(cornerRadius: 18))
        .allowsHitTesting(false)
        .accessibilityLabel("Minecraft is loading")
    }
}

private struct ColorFill: Shape {
    var progress: Double
    let waves: Bool
    var animatableData: Double {
        get { progress }
        set { progress = newValue }
    }

    func path(in rect: CGRect) -> Path {
        guard progress > 0 else { return Path() }
        guard progress < 1 else { return Path(rect) }

        var path = Path()
        let amplitude = waves ? min(1, progress * 12, (1 - progress) * 12) * 9 : 0
        let line = rect.height * (1 - progress)
        path.move(to: CGPoint(x: 0, y: rect.height))
        for segment in 0...36 {
            let step = Double(segment) / 36
            let wave = sin(step * .pi * 3 + progress * 12) * amplitude
            path.addLine(to: CGPoint(x: rect.width * step, y: line + wave))
        }
        path.addLine(to: CGPoint(x: rect.width, y: rect.height))
        path.closeSubpath()
        return path
    }
}
