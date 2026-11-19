import AppKit
import Foundation
import UniformTypeIdentifiers
import SwiftUI
import CoreGraphics

struct GameInstance: Identifiable, Equatable {
    let id: String
    let name: String
    let version: String
    let loader: String
    let folder: URL

    var gameFolder: URL {
        let modern = folder.appendingPathComponent("minecraft", isDirectory: true)
        let legacy = folder.appendingPathComponent(".minecraft", isDirectory: true)
        let disk = FileManager.default
        return !disk.fileExists(atPath: modern.path) && disk.fileExists(atPath: legacy.path) ? legacy : modern
    }
}

enum LibraryPanel {
    case instance
    case wallpapers
    case library
    case resources
    case create
    case settings
}

struct LaunchVisual {
    let instance: GameInstance
    let animation: LaunchAnimation
    var progress: Double
}

@MainActor
final class StellarLibrary: ObservableObject {
    let data: URL = {
        let arguments = CommandLine.arguments
        if let flag = arguments.firstIndex(of: "--dir"), arguments.indices.contains(flag + 1) {
            return URL(fileURLWithPath: arguments[flag + 1], isDirectory: true).standardizedFileURL
        }
        return FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/Stellar Launcher", isDirectory: true)
    }()
    @Published private(set) var instances: [GameInstance] = []
    @Published var selection = UserDefaults.standard.string(forKey: "SelectedInstance") {
        didSet { UserDefaults.standard.set(selection, forKey: "SelectedInstance") }
    }
    @Published var notice = ""
    @Published var panel: LibraryPanel?
    private let engine = Bundle.main.bundleURL.appendingPathComponent("Contents/Helpers/Stellar Engine.app")
    @Published var copying = false
    @Published var opening = false
    @Published var createFocused = false
    @Published private(set) var launchVisual: LaunchVisual?
    private var launchInProgress: String?

    var selected: GameInstance? { instances.first { $0.id == selection } }

    func refresh() {
        let settings = Self.settings(data.appendingPathComponent("archivelauncher.cfg"))
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
        if createFocused {
            if step < 0 {
                selection = instances.last?.id
                createFocused = false
            }
            return
        }
        let current = instances.firstIndex { $0.id == selection } ?? 0
        let next = current + step
        if next >= instances.count {
            createFocused = true
        } else {
            selection = instances[max(next, 0)].id
        }
    }

    func selectBoundary(last: Bool) {
        createFocused = last
        if !last { selection = instances.first?.id }
    }

    func activateSelection() {
        if createFocused || instances.isEmpty { create() }
        else if let selected { play(selected) }
    }

    func play(_ instance: GameInstance) {
        guard launchInProgress == nil else { return }
        let ticket = UUID().uuidString
        let choice = LaunchAnimation(rawValue: UserDefaults.standard.string(forKey: LaunchAnimation.setting) ?? "") ?? .wave
        if choice != .off && !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            launchVisual = LaunchVisual(instance: instance, animation: choice, progress: 0)
        }
        launchInProgress = ticket
        var options = ["--launch", instance.id, "--gallery-ticket", ticket]
        if UserDefaults.standard.bool(forKey: "FillGameScreen") { options.append("--gallery-fullscreen") }
        let start = Date()
        do {
            let progress = data.appendingPathComponent("launch-progress", isDirectory: true)
            try FileManager.default.createDirectory(at: progress, withIntermediateDirectories: true)
            try Data("requested".utf8).write(to: progress.appendingPathComponent(ticket), options: .withoutOverwriting)
        } catch {
            launchInProgress = nil
            dismissLaunch()
            notice = error.localizedDescription
            return
        }
        send(options) { [weak self] process in
            guard let self else { return }
            guard let process else {
                self.dismissLaunch()
                self.launchInProgress = nil
                try? FileManager.default.removeItem(at: self.data.appendingPathComponent("launch-progress").appendingPathComponent(ticket))
                return
            }
            Task { await self.watchLaunch(instance, ticket: ticket, process: process, started: start) }
        }
    }

    @discardableResult
    func dismissLaunch() -> Bool {
        guard launchVisual != nil else { return false }
        withAnimation(.easeOut(duration: 0.2)) { launchVisual = nil }
        return true
    }

    private func watchLaunch(_ instance: GameInstance, ticket: String, process: NSRunningApplication, started: Date) async {
        let milestones = data.appendingPathComponent("launch-progress").appendingPathComponent(ticket)
        let gameLog = instance.gameFolder.appendingPathComponent("logs/latest.log")
        var hasRendered = false
        var lastRenderUpdate: Date?
        defer {
            try? FileManager.default.removeItem(at: milestones)
            launchInProgress = nil
            dismissLaunch()
        }
        while !Task.isCancelled && Date().timeIntervalSince(started) < 900 {
            let state = (try? String(contentsOf: milestones, encoding: .utf8)) ?? ""
            let fields = state.split(separator: " ")
            switch fields.first {
            case "failed", "ended":
                return
            case "java":
                advance(to: 0.74)
                if let update = lastRenderProgress(at: gameLog, since: started) {
                    hasRendered = true
                    lastRenderUpdate = update
                    advance(to: 0.91)
                }
                let pid = fields.dropFirst().first.flatMap { Int32($0) }
                let showsWindow = pid.map { gameWindowVisible(pid: $0) } ?? false
                if showsWindow && ((hasRendered && Date().timeIntervalSince(lastRenderUpdate ?? started) >= 0.7)
                    || (instance.version.hasPrefix("1.") && Date().timeIntervalSince(started) > 25 && !hasRendered)) {
                    advance(to: 1)
                    try? await Task.sleep(for: .milliseconds(360))
                    return
                }
            case "download":
                let amount = fields.dropFirst().first.flatMap { Double($0) } ?? 0
                advance(to: 0.17 + 0.49 * min(max(0, amount), 1))
            case "preparing":
                advance(to: 0.1)
            default:
                if process.isTerminated && Date().timeIntervalSince(started) > 4 { return }
            }
            try? await Task.sleep(for: .milliseconds(200))
        }
    }

    private func advance(to progress: Double) {
        guard let visual = launchVisual, progress > visual.progress else { return }
        withAnimation(.easeOut(duration: 0.34)) { launchVisual?.progress = progress }
    }

    private func lastRenderProgress(at file: URL, since started: Date) -> Date? {
        guard let attributes = try? file.resourceValues(forKeys: [.contentModificationDateKey]),
              let updated = attributes.contentModificationDate, updated >= started,
              let handle = try? FileHandle(forReadingFrom: file) else { return nil }
        defer { try? handle.close() }
        guard let length = try? handle.seekToEnd(),
              (try? handle.seek(toOffset: length > 96_000 ? length - 96_000 : 0)) != nil,
              let content = try? handle.readToEnd() else { return nil }
        let log = String(decoding: content, as: UTF8.self)
        let resourceLine = log.contains("minecraft:textures/atlas/gui.png") ||
            (log.contains("Reloading ResourceManager") && log.contains("OpenAL initialized"))
        return resourceLine ? updated : nil
    }

    private func gameWindowVisible(pid: Int32) -> Bool {
        guard let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements],
                                                       kCGNullWindowID) as? [[String: Any]] else { return false }
        return windows.contains {
            ($0[kCGWindowOwnerPID as String] as? Int32) == pid &&
            ($0[kCGWindowLayer as String] as? Int) == 0
        }
    }
    func edit(_ instance: GameInstance) { send(["--show", instance.id]) }

    func create() { panel = .create }
    func createAdvanced() { send(["--gallery-action", "create"]) }

    func addInstance(name: String, version: String, fabric: String?) throws {
        struct Manifest: Encodable {
            struct Component: Encodable { let uid: String; let version: String; let important: Bool }
            let formatVersion = 1
            var components: [Component]
        }
        let disk = FileManager.default
        let trimmed = String(name.split(whereSeparator: \.isNewline).joined(separator: " ").prefix(80))
        guard !trimmed.isEmpty else { throw CocoaError(.fileWriteInvalidFileName) }
        let invalid = CharacterSet(charactersIn: #"/:\"#).union(.controlCharacters)
        let slug = trimmed.components(separatedBy: invalid).joined(separator: "-")
            .trimmingCharacters(in: CharacterSet(charactersIn: ". "))
        let base = slug.isEmpty ? "Minecraft" : slug
        let root = instanceRoot
        try disk.createDirectory(at: root, withIntermediateDirectories: true)
        let stage = root.appendingPathComponent(".create-\(UUID().uuidString)", isDirectory: true)
        defer { try? disk.removeItem(at: stage) }
        try disk.createDirectory(at: stage.appendingPathComponent("minecraft"), withIntermediateDirectories: true)
        var manifest = Manifest(components: [.init(uid: "net.minecraft", version: version, important: true)])
        if let fabric {
            manifest.components.append(.init(uid: "net.fabricmc.fabric-loader", version: fabric, important: false))
        }
        try JSONEncoder().encode(manifest).write(to: stage.appendingPathComponent("mmc-pack.json"))
        let config = "[General]\nInstanceType=OneSix\nname=\(trimmed)\niconKey=default\n"
        try Data(config.utf8).write(to: stage.appendingPathComponent("instance.cfg"))
        var id = base
        for next in 2...1000 {
            do {
                try disk.moveItem(at: stage, to: root.appendingPathComponent(id))
                panel = nil
                createFocused = false
                refresh()
                selection = id
                return
            } catch CocoaError.fileWriteFileExists {
                id = "\(base) \(next)"
            }
        }
        throw CocoaError(.fileWriteFileExists)
    }

    private var instanceRoot: URL {
        let path = Self.settings(data.appendingPathComponent("archivelauncher.cfg"))["InstanceDir"] ?? "instances"
        return path.hasPrefix("/") ? URL(fileURLWithPath: path) : data.appendingPathComponent(path)
    }
    func accounts() { send(["--gallery-action", "accounts"]) }
    func skins() { send(["--gallery-action", "skins"]) }

    func importPack() {
        let dialog = NSOpenPanel()
        dialog.allowedContentTypes = [.zip, UTType(filenameExtension: "mrpack") ?? .data]
        dialog.allowsMultipleSelection = false
        dialog.prompt = "Import"
        dialog.begin { [weak self] response in
            if response == .OK, let url = dialog.url { self?.send(["--import", url.absoluteString]) }
        }
    }

    func copyInstance() {
        guard !copying else { return }
        let dialog = NSOpenPanel()
        dialog.canChooseFiles = false
        dialog.canChooseDirectories = true
        dialog.prompt = "Copy Instance"
        dialog.directoryURL = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/PrismLauncher/instances")
        dialog.begin { [weak self] response in
            guard response == .OK, let self, let source = dialog.url else { return }
            let root = self.instanceRoot
            self.copying = true
            Task {
                defer { self.copying = false }
                do {
                    try await Task.detached {
                        let disk = FileManager.default
                        guard disk.fileExists(atPath: source.appendingPathComponent("instance.cfg").path),
                              disk.fileExists(atPath: source.appendingPathComponent("mmc-pack.json").path) else {
                            throw CocoaError(.fileReadCorruptFile)
                        }
                        try disk.createDirectory(at: root, withIntermediateDirectories: true)
                        let staging = root.appendingPathComponent(".copy-\(UUID().uuidString)", isDirectory: true)
                        defer { try? disk.removeItem(at: staging) }
                        try disk.copyItem(at: source.resolvingSymlinksInPath(), to: staging)
                        let copiedRoot = staging.resolvingSymlinksInPath().path + "/"
                        if let files = disk.enumerator(at: staging, includingPropertiesForKeys: [.isSymbolicLinkKey]) {
                            while let link = files.nextObject() as? URL {
                                guard try link.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink == true else { continue }
                                let target = try disk.destinationOfSymbolicLink(atPath: link.path)
                                let destination = URL(fileURLWithPath: target, relativeTo: link.deletingLastPathComponent())
                                guard destination.standardizedFileURL.resolvingSymlinksInPath().path.hasPrefix(copiedRoot) else {
                                    throw CocoaError(.fileWriteInvalidFileName, userInfo: [
                                        NSLocalizedDescriptionKey: "This instance links to files outside its folder."
                                    ])
                                }
                            }
                        }
                        try disk.moveItem(at: staging, to: root.appendingPathComponent(source.lastPathComponent))
                    }.value
                    self.refresh()
                    self.selection = source.lastPathComponent
                } catch {
                    self.notice = "Couldn’t copy the instance. \(error.localizedDescription)"
                }
            }
        }
    }

    private func send(_ arguments: [String], completion: ((NSRunningApplication?) -> Void)? = nil) {
        guard FileManager.default.fileExists(atPath: engine.path) else {
            notice = "The Stellar game engine is missing from this app bundle."
            completion?(nil)
            return
        }
        do {
            try FileManager.default.createDirectory(at: data, withIntermediateDirectories: true)
            let settings = data.appendingPathComponent("archivelauncher.cfg")
            if !FileManager.default.fileExists(atPath: settings.path) {
                let language = Locale.current.identifier.components(separatedBy: "@").first ?? "en_US"
                let defaults = "[General]\nLanguage=\(language)\nAutomaticJavaDownload=true\n"
                do { try Data(defaults.utf8).write(to: settings, options: .withoutOverwriting) }
                catch CocoaError.fileWriteFileExists { /* Another command created the settings. */ }
            }
        } catch {
            notice = error.localizedDescription
            completion?(nil)
            return
        }
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.createsNewApplicationInstance = true
        configuration.arguments = ["--gallery", "--dir", data.path] + arguments
        NSWorkspace.shared.openApplication(at: engine, configuration: configuration) { application, error in
            Task { @MainActor in
                if let error { self.notice = "Couldn’t open Stellar. \(error.localizedDescription)" }
                completion?(application)
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
