import AppKit
import CryptoKit
import SwiftUI
import UniformTypeIdentifiers

enum ResourceKind: String, CaseIterable, Identifiable {
    case mods, resourcepacks, shaderpacks
    var id: Self { self }
    var title: String {
        switch self {
        case .mods: "Mods"
        case .resourcepacks: "Resource Packs"
        case .shaderpacks: "Shaders"
        }
    }
    var projectType: String {
        switch self {
        case .mods: "mod"
        case .resourcepacks: "resourcepack"
        case .shaderpacks: "shader"
        }
    }
    var fileExtension: String { self == .mods ? "jar" : "zip" }
}

struct ModrinthProject: Decodable, Identifiable {
    let project_id: String
    let title: String
    let description: String
    var id: String { project_id }
}

private struct ModrinthVersion: Decodable {
    struct File: Decodable {
        struct Hashes: Decodable { let sha512: String }
        let url: URL
        let filename: String
        let primary: Bool
        let size: Int
        let hashes: Hashes
    }
    struct Dependency: Decodable {
        let version_id: String?
        let project_id: String?
        let dependency_type: String
    }
    let id: String
    let project_id: String
    let version_type: String
    let game_versions: [String]
    let loaders: [String]
    let files: [File]
    let dependencies: [Dependency]
}

private struct ResourceError: LocalizedError {
    let errorDescription: String?
    init(_ message: String) { errorDescription = message }
}

private struct Modrinth {
    let kind: ResourceKind
    let instance: GameInstance
    private var loader: String { instance.loader.lowercased() }

    func search(_ query: String) async throws -> [ModrinthProject] {
        struct Results: Decodable { let hits: [ModrinthProject] }
        var facets = [["project_type:\(kind.projectType)"], ["versions:\(instance.version)"]]
        if kind == .mods { facets.append(["categories:\(loader)"]) }
        let result: Results = try await get("/v2/search", [
            "query": query, "facets": try json(facets), "limit": "24"
        ])
        return result.hits
    }

    func files(for project: String) async throws -> [ModrinthVersion.File] {
        var result = [ModrinthVersion.File]()
        var versions = [String: String]()
        try await resolve(project: project, version: nil, versions: &versions, files: &result)
        return result
    }

    private func resolve(project: String?, version: String?, versions: inout [String: String],
                         files: inout [ModrinthVersion.File]) async throws {
        let release: ModrinthVersion
        if let project, let installedVersion = versions[project] {
            guard version == nil || version == installedVersion else {
                throw ResourceError("Two required files need different versions of the same project.")
            }
            return
        }
        if let version {
            try validateID(version)
            release = try await get("/v2/version/\(version)")
        } else if let project {
            try validateID(project)
            var filters = ["game_versions": try json([instance.version])]
            if kind == .mods { filters["loaders"] = try json([loader]) }
            let releases: [ModrinthVersion] = try await get("/v2/project/\(project)/version", filters)
            guard let match = releases.first(where: { $0.version_type == "release" }) ?? releases.first else {
                throw ResourceError("No compatible release was found for \(instance.version) and \(instance.loader).")
            }
            release = match
        } else {
            throw ResourceError("A required file has no downloadable version on Modrinth.")
        }

        if let previous = versions[release.project_id] {
            guard previous == release.id else {
                throw ResourceError("Two required files need different versions of the same project.")
            }
            return
        }
        guard versions.count < 32 else { throw ResourceError("This project requires more than 32 files.") }
        guard release.game_versions.contains(instance.version), kind != .mods || release.loaders.contains(loader) else {
            throw ResourceError("A required file does not support this Minecraft version or loader.")
        }
        let matching = release.files.filter { $0.filename.lowercased().hasSuffix(".\(kind.fileExtension)") }
        guard let file = matching.first(where: \.primary) ?? matching.first else {
            throw ResourceError("No compatible .\(kind.fileExtension) download was found.")
        }
        versions[release.project_id] = release.id
        for dependency in release.dependencies where dependency.dependency_type == "required" {
            try await resolve(project: dependency.project_id, version: dependency.version_id,
                              versions: &versions, files: &files)
        }
        files.append(file)
    }

    func download(_ file: ModrinthVersion.File) async throws -> URL {
        guard file.url.scheme == "https", file.url.host == "cdn.modrinth.com", safeFilename(file.filename),
              file.size > 0, file.size <= 128_000_000 else {
            throw ResourceError("The download has an invalid filename, source, or size.")
        }
        let (temporary, response) = try await URLSession.shared.download(for: request(file.url))
        do {
            guard (response as? HTTPURLResponse)?.statusCode == 200,
                  response.url?.host == "cdn.modrinth.com",
                  try (temporary.resourceValues(forKeys: [.fileSizeKey])).fileSize == file.size,
                  try checksum(temporary) == file.hashes.sha512.lowercased() else {
                throw ResourceError("Couldn’t verify the download for \(file.filename).")
            }
            return temporary
        } catch {
            try? FileManager.default.removeItem(at: temporary)
            throw error
        }
    }

    private func validateID(_ id: String) throws {
        guard !id.isEmpty, id.utf8.allSatisfy({ (48...57).contains($0) || (65...90).contains($0) || (97...122).contains($0) }) else {
            throw ResourceError("Modrinth returned an invalid project or version identifier.")
        }
    }

    private func get<T: Decodable>(_ path: String, _ filters: [String: String] = [:]) async throws -> T {
        var components = URLComponents()
        components.scheme = "https"
        components.host = "api.modrinth.com"
        components.path = path
        if !filters.isEmpty {
            components.queryItems = filters.map { URLQueryItem(name: $0.key, value: $0.value) }
        }
        guard let url = components.url else { throw ResourceError("The Modrinth request is invalid.") }
        let (data, response) = try await URLSession.shared.data(for: request(url))
        guard (response as? HTTPURLResponse)?.statusCode == 200 else {
            throw ResourceError("Modrinth couldn’t complete the request. Try again in a moment.")
        }
        return try JSONDecoder().decode(T.self, from: data)
    }

    private func request(_ url: URL) -> URLRequest {
        var request = URLRequest(url: url, timeoutInterval: 25)
        request.setValue("sebastianmiletic/archivelauncher (Stellar Launcher)", forHTTPHeaderField: "User-Agent")
        return request
    }

    private func json<T: Encodable>(_ value: T) throws -> String {
        String(decoding: try JSONEncoder().encode(value), as: UTF8.self)
    }
}

private func safeFilename(_ name: String) -> Bool {
    !name.isEmpty && name != "." && name != ".." && !name.contains("/") && !name.contains("\\")
}

private func checksum(_ file: URL) throws -> String {
    let contents = try FileHandle(forReadingFrom: file)
    defer { try? contents.close() }
    var hash = SHA512()
    while let data = try contents.read(upToCount: 512 * 1024), !data.isEmpty { hash.update(data: data) }
    return hash.finalize().map { String(format: "%02x", $0) }.joined()
}

@MainActor
final class ResourceStore: ObservableObject {
    let instance: GameInstance
    @Published var kind: ResourceKind = .mods
    @Published var browsing = false
    @Published var query = ""
    @Published private(set) var projects = [ModrinthProject]()
    @Published private(set) var installed = [URL]()
    @Published private(set) var busy = false
    @Published private(set) var message = ""
    private let disk = FileManager.default

    init(instance: GameInstance) { self.instance = instance }

    var folder: URL { instance.gameFolder.appendingPathComponent(kind.rawValue, isDirectory: true) }

    var canBrowse: Bool { !instance.version.isEmpty && (kind != .mods || instance.loader != "Vanilla") }

    func refresh() {
        installed = ((try? disk.contentsOfDirectory(at: folder, includingPropertiesForKeys: [.isRegularFileKey],
                                                      options: [.skipsHiddenFiles])) ?? [])
            .filter { url in
                let name = url.lastPathComponent.lowercased()
                return (name.hasSuffix(".\(kind.fileExtension)") || name.hasSuffix(".\(kind.fileExtension).disabled"))
                    && (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true
            }
            .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
    }

    func changeTab() {
        projects = []
        message = ""
        refresh()
    }

    func search() async {
        guard !busy else { return }
        guard canBrowse else {
            message = kind == .mods ? "Choose a Minecraft version and mod loader in the instance settings." :
                "Choose a Minecraft version in the instance settings."
            return
        }
        busy = true
        message = ""
        projects = []
        defer { busy = false }
        do {
            projects = try await Modrinth(kind: kind, instance: instance).search(query)
            if projects.isEmpty { message = "No compatible projects found." }
        } catch { message = error.localizedDescription }
    }

    func install(_ project: ModrinthProject) async {
        guard !busy else { return }
        busy = true
        message = "Checking compatibility and required files…"
        defer { busy = false }
        let client = Modrinth(kind: kind, instance: instance)
        let destination = folder
        var downloads = [(source: URL, target: URL)]()
        var added = [URL]()
        var filenames = Set<String>()
        defer { downloads.forEach { try? disk.removeItem(at: $0.source) } }
        do {
            let files = try await client.files(for: project.id)
            try disk.createDirectory(at: destination, withIntermediateDirectories: true)
            for file in files {
                try Task.checkCancellation()
                guard safeFilename(file.filename), filenames.insert(file.filename.lowercased()).inserted else {
                    throw ResourceError("The project references conflicting filenames.")
                }
                let target = destination.appendingPathComponent(file.filename)
                let disabled = target.appendingPathExtension("disabled")
                if disk.fileExists(atPath: target.path) {
                    guard try checksum(target) == file.hashes.sha512.lowercased() else {
                        throw ResourceError("A different file named \(file.filename) is already installed.")
                    }
                    continue
                }
                guard !disk.fileExists(atPath: disabled.path) else {
                    throw ResourceError("Enable \(file.filename) in Installed before adding this project.")
                }
                message = "Downloading \(file.filename)…"
                downloads.append((try await client.download(file), target))
            }
            try Task.checkCancellation()
            for (source, target) in downloads {
                try disk.moveItem(at: source, to: target)
                added.append(target)
            }
            refresh()
            message = added.isEmpty ? "Already installed." : "Installed \(project.title)."
        } catch {
            for file in added { try? disk.removeItem(at: file) }
            refresh()
            message = error.localizedDescription
        }
    }

    func addFiles(to window: NSWindow?) {
        let picker = NSOpenPanel()
        picker.allowedContentTypes = [UTType(filenameExtension: kind.fileExtension) ?? .data]
        picker.allowsMultipleSelection = true
        picker.prompt = "Add"
        let destination = folder
        let fileExtension = kind.fileExtension
        let completion: (NSApplication.ModalResponse) -> Void = { [weak self] response in
            guard response == .OK, let self else { return }
            self.perform {
                try self.disk.createDirectory(at: destination, withIntermediateDirectories: true)
                var added = [URL]()
                do {
                    for file in picker.urls {
                        guard file.pathExtension.lowercased() == fileExtension else {
                            throw ResourceError("Choose .\(fileExtension) files.")
                        }
                        let target = destination.appendingPathComponent(file.lastPathComponent)
                        try self.disk.copyItem(at: file, to: target)
                        added.append(target)
                    }
                } catch {
                    for file in added { try? self.disk.removeItem(at: file) }
                    throw error
                }
            }
        }
        if let window { picker.beginSheetModal(for: window, completionHandler: completion) }
        else { picker.begin(completionHandler: completion) }
    }

    func toggle(_ file: URL) {
        perform {
            let target = file.pathExtension == "disabled" ? file.deletingPathExtension() : file.appendingPathExtension("disabled")
            try disk.moveItem(at: file, to: target)
        }
    }

    func remove(_ file: URL) { perform { try disk.trashItem(at: file, resultingItemURL: nil) } }

    func reveal() {
        perform {
            try disk.createDirectory(at: folder, withIntermediateDirectories: true)
            NSWorkspace.shared.open(folder)
        }
    }

    private func perform(_ operation: () throws -> Void) {
        do {
            try operation()
            message = ""
        } catch { message = error.localizedDescription }
        refresh()
    }
}

struct ResourcePanel: View {
    @StateObject private var store: ResourceStore
    let dismiss: () -> Void

    init(instance: GameInstance, dismiss: @escaping () -> Void) {
        _store = StateObject(wrappedValue: ResourceStore(instance: instance))
        self.dismiss = dismiss
    }

    private var packHint: String {
        switch store.kind {
        case .mods: ""
        case .resourcepacks: "Select installed resource packs in Minecraft."
        case .shaderpacks: "Select shaders in Minecraft with a compatible shader loader."
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Manage Content").font(.system(size: 17, weight: .semibold))
                    Text("\(store.instance.name) · \(store.instance.version) · \(store.instance.loader)")
                        .font(.caption).foregroundStyle(.secondary).lineLimit(1)
                }
                Spacer()
                Button("Done", action: dismiss).keyboardShortcut(.cancelAction)
            }
            Picker("Content", selection: $store.kind) {
                ForEach(ResourceKind.allCases) { Text($0.title).tag($0) }
            }.pickerStyle(.segmented).labelsHidden().disabled(store.busy)
            HStack {
                Picker("View", selection: $store.browsing) {
                    Text("Installed").tag(false)
                    Text("Browse Modrinth").tag(true)
                }.labelsHidden().fixedSize().disabled(store.busy)
                if store.browsing {
                    TextField("Search \(store.kind.title.lowercased())", text: $store.query)
                        .onSubmit { Task { await store.search() } }.disabled(store.busy)
                    Button("Search") { Task { await store.search() } }.disabled(store.busy)
                } else {
                    Spacer()
                    Button("Show Folder") { store.reveal() }
                    Button("Add Files…") { store.addFiles(to: NSApp.keyWindow) }
                }
            }
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    if store.browsing {
                        ForEach(store.projects) { project in
                            HStack {
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(project.title).lineLimit(1)
                                    Text(project.description).font(.caption).foregroundStyle(.secondary).lineLimit(2)
                                }
                                Spacer()
                                Button("Install") { Task { await store.install(project) } }.disabled(store.busy)
                            }.padding(8)
                            Divider()
                        }
                    } else {
                        ForEach(store.installed, id: \.self) { file in
                            let disabled = file.pathExtension == "disabled"
                            HStack {
                                Text((disabled ? file.deletingPathExtension() : file).lastPathComponent).lineLimit(1)
                                Spacer()
                                Button(disabled ? "Enable" : "Disable") { store.toggle(file) }
                                Button("Move to Trash", systemImage: "trash") { store.remove(file) }
                                    .labelStyle(.iconOnly).help("Move to Trash")
                            }.padding(8)
                            Divider()
                        }
                    }
                }
            }
            .overlay {
                if !store.browsing && store.installed.isEmpty {
                    Text("No \(store.kind.title.lowercased()) installed").foregroundStyle(.secondary).allowsHitTesting(false)
                }
            }
            .background(.white.opacity(0.035), in: RoundedRectangle(cornerRadius: 10))
            Text(store.message.isEmpty && !store.browsing ? packHint : store.message)
                .font(.caption).foregroundStyle(.secondary).lineLimit(2)
                .frame(minHeight: 15, alignment: .leading)
        }
        .padding(14)
        .frame(width: 564, height: 322)
        .background(Color(white: 0.09), in: RoundedRectangle(cornerRadius: 15))
        .overlay(RoundedRectangle(cornerRadius: 15).strokeBorder(.white.opacity(0.12)).allowsHitTesting(false))
        .onAppear { store.refresh() }
        .onChange(of: store.kind) { store.changeTab() }
        .onChange(of: store.browsing) {
            store.changeTab()
            if store.browsing { Task { await store.search() } }
        }
    }
}
