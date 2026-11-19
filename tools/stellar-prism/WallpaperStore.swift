import AppKit
import Foundation
import ImageIO
import SwiftUI

struct WallpaperSet: Identifiable, Decodable, Hashable, Sendable {
    let id: String
    let title: String
    let versions: [String]
    let archive: URL
    let fallback: URL?
}

@MainActor
final class WallpaperStore: ObservableObject {
    let folder = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/Stellar Gallery/Wallpapers", isDirectory: true)
    let sets: [WallpaperSet]
    @Published private(set) var files: [String: [URL]] = [:]
    @Published var collection = "" {
        didSet { if collection != oldValue { highlighted = nil } }
    }
    @Published var highlighted: URL?
    @Published private(set) var downloading = false
    @Published private(set) var completed = 0
    @Published private(set) var message = ""
    @Published private var covers = UserDefaults.standard.dictionary(forKey: "WallpaperCovers") as? [String: String] ?? [:]
    private var pending: Set<String> = []

    init() {
        let url = Bundle.main.url(forResource: "wallpapers", withExtension: "json")
        sets = url.flatMap { try? Data(contentsOf: $0) }.flatMap { try? JSONDecoder().decode([WallpaperSet].self, from: $0) } ?? []
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        for set in sets { read(set) }
    }

    func set(for version: String) -> WallpaperSet? {
        sets.first { item in item.versions.contains { version == $0 || version.hasPrefix($0 + ".") } } ?? sets.first
    }

    func artwork(for instance: GameInstance) -> URL? {
        if let choice = covers[instance.id] {
            let url = folder.appendingPathComponent(choice)
            if FileManager.default.fileExists(atPath: url.path) { return url }
        }
        guard let set = set(for: instance.version) else { return nil }
        let images = files[set.id] ?? []
        return ["3840x2160", "2560x1440", "1920x1080", "desktop", "landscape"]
            .lazy.compactMap { label in images.first { $0.lastPathComponent.lowercased().contains(label) } }
            .first ?? images.first
    }

    var visibleImages: [URL] { files[collection] ?? [] }
    var selectedImage: URL? {
        if let highlighted, visibleImages.contains(highlighted) { return highlighted }
        return visibleImages.first
    }

    func moveHighlight(by step: Int) {
        let images = visibleImages
        guard !images.isEmpty else { return }
        let current = selectedImage.flatMap { images.firstIndex(of: $0) } ?? 0
        highlighted = images[min(max(current + step, 0), images.count - 1)]
    }

    func selectBoundary(last: Bool) {
        highlighted = last ? visibleImages.last : visibleImages.first
    }

    func hasChoice(for instance: GameInstance) -> Bool {
        covers[instance.id] != nil
    }

    func choose(_ image: URL, for instance: GameInstance) {
        covers[instance.id] = image.deletingLastPathComponent().lastPathComponent + "/" + image.lastPathComponent
        UserDefaults.standard.set(covers, forKey: "WallpaperCovers")
    }

    func reset(_ instance: GameInstance) {
        covers.removeValue(forKey: instance.id)
        UserDefaults.standard.set(covers, forKey: "WallpaperCovers")
    }

    func ensure(_ set: WallpaperSet) async {
        guard files[set.id]?.isEmpty != false, pending.insert(set.id).inserted else { return }
        defer { pending.remove(set.id) }
        let destination = folder.appendingPathComponent(set.id, isDirectory: true)
        do {
            try await Task.detached(priority: .utility) { try await Self.download(set, to: destination) }.value
            read(set)
        } catch {
            message = "Couldn’t download \(set.title). You can retry from Wallpapers."
        }
    }

    func downloadAll() async {
        guard !downloading else { return }
        downloading = true
        completed = 0
        message = ""
        for set in sets {
            await ensure(set)
            if files[set.id]?.isEmpty == false { completed += 1 }
        }
        downloading = false
        message = "\(completed) of \(sets.count) official collections saved."
    }

    private func read(_ set: WallpaperSet) {
        files[set.id] = ((try? FileManager.default.contentsOfDirectory(at: folder.appendingPathComponent(set.id),
                     includingPropertiesForKeys: nil, options: .skipsHiddenFiles)) ?? [])
            .filter { Self.formats.contains($0.pathExtension.lowercased()) }
            .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
    }

    private nonisolated static let formats: Set<String> = ["jpg", "jpeg", "png", "webp"]

    private nonisolated static func download(_ set: WallpaperSet, to destination: URL) async throws {
        do {
            try await unpack(set.archive, to: destination)
        } catch {
            guard let fallback = set.fallback else { throw error }
            let (image, _) = try await fetch(fallback)
            guard CGImageSourceCreateWithURL(image as CFURL, nil) != nil else { throw URLError(.cannotDecodeContentData) }
            try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
            try FileManager.default.copyItem(at: image, to: destination.appendingPathComponent("Official key art.jpg"))
        }
    }

    private nonisolated static func fetch(_ url: URL) async throws -> (URL, URLResponse) {
        guard url.scheme == "https", url.host == "www.minecraft.net" else { throw URLError(.badURL) }
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 14
        configuration.timeoutIntervalForResource = 48
        let (archive, response) = try await URLSession(configuration: configuration).download(from: url)
        guard let http = response as? HTTPURLResponse, http.statusCode == 200,
              http.url?.host == "www.minecraft.net",
              (try archive.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0) < 96 * 1_048_576
        else { throw URLError(.badServerResponse) }
        return (archive, response)
    }

    private nonisolated static func unpack(_ url: URL, to destination: URL) async throws {
        let (archive, _) = try await fetch(url)
        let manager = FileManager.default
        let root = destination.deletingLastPathComponent()
        let staging = root.appendingPathComponent("." + UUID().uuidString, isDirectory: true)
        try manager.createDirectory(at: staging, withIntermediateDirectories: true)
        defer { try? manager.removeItem(at: staging) }

        let names = try unzip(["-Z1", archive.path], limit: 1_048_576)
        for (index, name) in String(decoding: names, as: UTF8.self).split(whereSeparator: \.isNewline).prefix(180).enumerated() {
            let member = String(name)
            let file = (member as NSString).lastPathComponent
            guard !member.hasPrefix("__MACOSX/"), !file.hasPrefix("."),
                  !member.contains("*"), !member.contains("?"), !member.contains("["),
                  formats.contains((file as NSString).pathExtension.lowercased()) else { continue }
            guard let bytes = try? unzip(["-p", archive.path, member], limit: 32 * 1_048_576),
                  CGImageSourceCreateWithData(bytes as CFData, nil) != nil else { continue }
            try bytes.write(to: staging.appendingPathComponent("\(index)-\(file)"))
        }
        guard !(try manager.contentsOfDirectory(atPath: staging.path)).isEmpty else { throw URLError(.cannotDecodeContentData) }
        if !manager.fileExists(atPath: destination.path) { try manager.moveItem(at: staging, to: destination) }
    }

    private nonisolated static func unzip(_ arguments: [String], limit: Int) throws -> Data {
        let task = Process()
        let output = Pipe()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/unzip")
        task.arguments = arguments
        task.standardOutput = output
        task.standardError = FileHandle.nullDevice
        try task.run()
        let handle = output.fileHandleForReading
        var data = Data()
        while let next = try handle.read(upToCount: 65_536), !next.isEmpty {
            guard next.count <= limit - data.count else {
                task.terminate()
                try? handle.close()
                task.waitUntilExit()
                throw URLError(.dataLengthExceedsMaximum)
            }
            data.append(next)
        }
        task.waitUntilExit()
        if task.terminationStatus != 0 { throw URLError(.cannotDecodeContentData) }
        return data
    }
}

struct WallpaperImage: View {
    let url: URL?
    private static let thumbnails: NSCache<NSURL, NSImage> = {
        let cache = NSCache<NSURL, NSImage>()
        cache.totalCostLimit = 24 * 1_048_576
        return cache
    }()

    var body: some View {
        GeometryReader { geometry in
            Group {
                if let image = thumbnail() {
                    Image(nsImage: image).resizable().scaledToFill()
                } else {
                    Color(white: 0.16)
                }
            }.frame(width: geometry.size.width, height: geometry.size.height).clipped()
        }
        .accessibilityHidden(true)
    }

    private func thumbnail() -> NSImage? {
        guard let source = url ?? Bundle.main.url(forResource: "stellar-wallpaper", withExtension: "jpg") else { return nil }
        let key = source as NSURL
        if let cached = Self.thumbnails.object(forKey: key) { return cached }
        guard let reference = CGImageSourceCreateWithURL(source as CFURL, nil),
              let bitmap = CGImageSourceCreateThumbnailAtIndex(reference, 0, [
                  kCGImageSourceCreateThumbnailFromImageAlways: true,
                  kCGImageSourceThumbnailMaxPixelSize: 640,
                  kCGImageSourceCreateThumbnailWithTransform: true,
              ] as CFDictionary) else { return nil }
        let image = NSImage(cgImage: bitmap, size: .zero)
        Self.thumbnails.setObject(image, forKey: key, cost: bitmap.bytesPerRow * bitmap.height)
        return image
    }
}
