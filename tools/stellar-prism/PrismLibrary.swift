import AppKit
import Foundation
import UniformTypeIdentifiers

struct GameInstance: Identifiable, Equatable {
    let id: String
    let name: String
    let version: String
    let loader: String
    let folder: URL
}

enum LibraryPanel {
    case instance
    case wallpapers
    case library
}

@MainActor
final class PrismLibrary: ObservableObject {
    static let bundleID = "org.prismlauncher.PrismLauncher"
    let data = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/PrismLauncher", isDirectory: true)
    @Published private(set) var instances: [GameInstance] = []
    @Published var selection = UserDefaults.standard.string(forKey: "SelectedInstance") {
        didSet { UserDefaults.standard.set(selection, forKey: "SelectedInstance") }
    }
    @Published var notice = ""
    @Published var panel: LibraryPanel?
    @Published private(set) var prism: URL?

    var selected: GameInstance? { instances.first { $0.id == selection } }

    func refresh() {
        prism = NSWorkspace.shared.urlForApplication(withBundleIdentifier: Self.bundleID)
        let settings = Self.settings(data.appendingPathComponent("prismlauncher.cfg"))
        let main = settings["InstanceDir"] ?? "instances"
        let additional = (settings["AdditionalInstanceDirs"] ?? "")
            .replacingOccurrences(of: "@StringList(", with: "")
            .trimmingCharacters(in: CharacterSet(charactersIn: ")"))
            .split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
        var found = [String: GameInstance]()
        for path in [main] + additional where !path.isEmpty {
            let root = path.hasPrefix("/") ? URL(fileURLWithPath: path) : data.appendingPathComponent(path)
            let folders = (try? FileManager.default.contentsOfDirectory(
                at: root, includingPropertiesForKeys: [.isDirectoryKey], options: [.skipsHiddenFiles]
            )) ?? []
            for folder in folders {
                let id = folder.lastPathComponent
                let config = Self.settings(folder.appendingPathComponent("instance.cfg"))
                guard found[id] == nil, !config.isEmpty else { continue }
                let manifest = folder.appendingPathComponent("mmc-pack.json")
                let components = (try? Data(contentsOf: manifest)).flatMap { try? JSONDecoder().decode(Pack.self, from: $0) }?.components ?? []
                let version = components.first { $0.uid == "net.minecraft" }?.version ?? ""
                let names = ["net.fabricmc.fabric-loader": "Fabric", "net.neoforged": "NeoForge",
                             "net.minecraftforge": "Forge", "org.quiltmc.quilt-loader": "Quilt"]
                let loader = components.compactMap { names[$0.uid] }.first ?? "Vanilla"
                found[id] = GameInstance(id: id, name: config["name"] ?? id, version: version, loader: loader, folder: folder)
            }
        }
        instances = found.values.sorted { lhs, rhs in
            let order = lhs.name.localizedStandardCompare(rhs.name)
            return order == .orderedSame ? lhs.id < rhs.id : order == .orderedAscending
        }
        if !instances.contains(where: { $0.id == selection }) { selection = instances.first?.id }
    }

    func moveSelection(by step: Int) {
        guard !instances.isEmpty else { return }
        let current = instances.firstIndex { $0.id == selection } ?? 0
        selection = instances[min(max(current + step, 0), instances.count - 1)].id
    }

    func selectBoundary(last: Bool) {
        selection = (last ? instances.last : instances.first)?.id
    }

    func playSelected() {
        guard let selected, prism != nil else { return }
        play(selected)
    }

    func play(_ instance: GameInstance) { send(["--launch", instance.id]) }
    func edit(_ instance: GameInstance) { send(["--show", instance.id]) }

    func manage() {
        guard let prism else {
            if let page = URL(string: "https://prismlauncher.org/download/") { NSWorkspace.shared.open(page) }
            return
        }
        NSWorkspace.shared.openApplication(at: prism, configuration: .init(), completionHandler: nil)
    }

    func importPack() {
        let dialog = NSOpenPanel()
        dialog.allowedContentTypes = [.zip, UTType(filenameExtension: "mrpack") ?? .data]
        dialog.allowsMultipleSelection = false
        dialog.prompt = "Import"
        if dialog.runModal() == .OK, let url = dialog.url { send(["--import", url.absoluteString]) }
    }

    private func send(_ arguments: [String]) {
        guard let prism else {
            notice = "Install Prism Launcher to play or import instances."
            return
        }
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.createsNewApplicationInstance = true
        configuration.arguments = ["--dir", data.path] + arguments
        NSWorkspace.shared.openApplication(at: prism, configuration: configuration) { _, error in
            Task { @MainActor in
                self.notice = error.map { "Couldn’t open Prism. \($0.localizedDescription)" } ?? ""
            }
        }
    }

    private struct Pack: Decodable {
        struct Component: Decodable { let uid: String; let version: String? }
        let components: [Component]
    }

    private static func settings(_ url: URL) -> [String: String] {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return [:] }
        return text.split(whereSeparator: \.isNewline).reduce(into: [:]) { result, line in
            let parts = line.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            guard parts.count == 2, !parts[0].hasPrefix("#") else { return }
            let value = String(parts[1])
            result[String(parts[0])] = value.hasPrefix("\"") && value.hasSuffix("\"")
                ? String(value.dropFirst().dropLast()).replacingOccurrences(of: "\\\"", with: "\"")
                : value
        }
    }
}
