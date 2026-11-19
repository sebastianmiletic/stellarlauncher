import AppKit
import SwiftUI

struct StellarGallery: View {
    @ObservedObject var library: PrismLibrary
    @ObservedObject var wallpapers: WallpaperStore
    private let canvas = Color(white: 0.055)
    private let secondary = Color(white: 0.62)

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider().overlay(Color.white.opacity(0.07))
            if library.instances.isEmpty {
                empty
            } else {
                inventory
            }
            Divider().overlay(Color.white.opacity(0.07))
            footer
        }
        .background(canvas)
        .foregroundStyle(Color(white: 0.94))
        .preferredColorScheme(.dark)
        .sheet(isPresented: $library.showWallpapers) {
            WallpaperBrowser(wallpapers: wallpapers, instance: library.selected)
        }
        .task(id: library.instances.map(\.version)) {
            for version in Set(library.instances.map(\.version)) {
                if let set = wallpapers.set(for: version) { await wallpapers.ensure(set) }
            }
        }
    }

    private var header: some View {
        HStack(spacing: 10) {
            if let logo = Bundle.main.url(forResource: "stellar-instance", withExtension: "png"),
               let image = NSImage(contentsOf: logo) {
                Image(nsImage: image).resizable().frame(width: 30, height: 30).accessibilityHidden(true)
            }
            Text("Stellar Launcher").font(.system(size: 17, weight: .semibold))
            Spacer()
            Button("Wallpapers", systemImage: "photo.on.rectangle") { library.showWallpapers = true }
            Button("Import", systemImage: "square.and.arrow.down") { library.importPack() }
                .disabled(library.prism == nil)
            Button("Manage in Prism", systemImage: "gearshape") { library.manage() }
        }
        .buttonStyle(.bordered)
        .controlSize(.small)
        .padding(.horizontal, 22)
        .padding(.vertical, 12)
    }

    private var inventory: some View {
        VStack(alignment: .leading, spacing: 13) {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text("Instances").font(.system(size: 20, weight: .semibold))
                Text("\(library.instances.count)").font(.system(size: 12, weight: .medium)).foregroundStyle(secondary)
                Spacer()
            }.padding(.horizontal, 22)

            ScrollView(.horizontal) {
                LazyHStack(alignment: .top, spacing: 13) {
                    ForEach(library.instances) { instance in
                        card(instance)
                    }
                }
                .padding(.horizontal, 22)
                .padding(.vertical, 3)
            }
            .defaultScrollAnchor(.leading)
            .frame(height: 203)
        }
        .padding(.top, 16)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    private func card(_ instance: GameInstance) -> some View {
        let selected = library.selection == instance.id
        return Button {
            library.selection = instance.id
        } label: {
            VStack(alignment: .leading, spacing: 0) {
                WallpaperImage(url: wallpapers.artwork(for: instance))
                    .frame(height: 128)
                VStack(alignment: .leading, spacing: 4) {
                    Text(instance.name).font(.system(size: 13, weight: .semibold)).lineLimit(1)
                    Text(instance.version.isEmpty ? instance.loader : "\(instance.version)  ·  \(instance.loader)")
                        .font(.system(size: 11, weight: .medium)).foregroundStyle(secondary).lineLimit(1)
                }
                .padding(.horizontal, 11)
                .padding(.vertical, 10)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .background(Color(white: selected ? 0.16 : 0.096))
            .clipShape(RoundedRectangle(cornerRadius: 9))
            .overlay(RoundedRectangle(cornerRadius: 9).strokeBorder(
                Color.white.opacity(selected ? 0.84 : 0.10), lineWidth: selected ? 1.5 : 1
            ))
            .frame(width: 164)
        }
        .buttonStyle(.plain)
        .accessibilityLabel(instance.name)
        .accessibilityValue("\(instance.version), \(instance.loader)")
        .accessibilityAddTraits(selected ? [.isSelected] : [])
        .help(instance.name)
        .contextMenu {
            Button("Play") { library.play(instance) }
            Button("Edit in Prism…") { library.edit(instance) }
            Button("Open Instance Folder") { NSWorkspace.shared.open(instance.folder) }
            Divider()
            Button("Choose Wallpaper…") { library.selection = instance.id; library.showWallpapers = true }
            Button("Reset Wallpaper") { wallpapers.reset(instance) }
        }
    }

    private var empty: some View {
        VStack(spacing: 10) {
            Text(library.prism == nil ? "Install Prism Launcher" : "Your library is empty")
                .font(.system(size: 22, weight: .semibold))
            Text(library.prism == nil ? "Prism handles Microsoft sign-in and game installation." : "Import a pack or add an instance in Prism.")
                .foregroundStyle(secondary)
            Button(library.prism == nil ? "Get Prism Launcher" : "Open Prism Launcher") { library.manage() }
                .buttonStyle(.bordered)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var footer: some View {
        HStack(spacing: 10) {
            if let instance = library.selected {
                VStack(alignment: .leading, spacing: 3) {
                    Text(instance.name).font(.system(size: 14, weight: .semibold)).lineLimit(1)
                    let detail = library.notice.isEmpty
                        ? (wallpapers.set(for: instance.version).map { "\($0.title) artwork" } ?? instance.loader)
                        : library.notice
                    Text(detail).font(.system(size: 11)).foregroundStyle(secondary).lineLimit(1)
                }
            } else {
                Text(library.notice).font(.caption).foregroundStyle(secondary).lineLimit(1)
            }
            Spacer()
            if let instance = library.selected {
                Button("Edit", systemImage: "slider.horizontal.3") { library.edit(instance) }
                    .buttonStyle(.bordered)
                Button {
                    library.play(instance)
                } label: {
                    Label("Play", systemImage: "play.fill")
                        .font(.system(size: 14, weight: .semibold))
                        .padding(.horizontal, 21).padding(.vertical, 7)
                        .foregroundStyle(Color(white: 0.07))
                        .background(Color(white: 0.93), in: RoundedRectangle(cornerRadius: 7))
                }
                .buttonStyle(.plain)
                .keyboardShortcut(.defaultAction)
                .disabled(library.prism == nil)
            }
        }
        .padding(.horizontal, 22)
        .padding(.vertical, 13)
        .background(Color(white: 0.08))
    }
}

private struct WallpaperBrowser: View {
    @ObservedObject var wallpapers: WallpaperStore
    let instance: GameInstance?
    @Environment(\.dismiss) private var dismiss
    private var selected: WallpaperSet? { wallpapers.sets.first { $0.id == wallpapers.collection } }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Wallpapers").font(.title2.weight(.semibold))
                Spacer()
                Button("Show Files") { NSWorkspace.shared.open(wallpapers.folder) }
                Button("Done") { dismiss() }.keyboardShortcut(.cancelAction)
            }
            HStack {
                Picker("Collection", selection: $wallpapers.collection) {
                    ForEach(wallpapers.sets) { set in Text(set.title).tag(set.id) }
                }.frame(width: 310)
                Spacer()
                Button(wallpapers.downloading ? "Saving \(wallpapers.completed)/\(wallpapers.sets.count)…" : "Save All") {
                    Task { await wallpapers.downloadAll() }
                }.disabled(wallpapers.downloading)
            }
            if let set = selected, let files = wallpapers.files[set.id], !files.isEmpty {
                ScrollView {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 145), spacing: 12)], spacing: 12) {
                        ForEach(files, id: \.self) { file in
                            Button {
                                if let instance { wallpapers.choose(file, for: instance); dismiss() }
                                else { NSWorkspace.shared.open(file) }
                            } label: {
                                WallpaperImage(url: file)
                                    .frame(height: 95)
                                    .clipShape(RoundedRectangle(cornerRadius: 7))
                            }.buttonStyle(.plain).help(file.lastPathComponent).accessibilityLabel(file.lastPathComponent)
                        }
                    }.padding(2)
                }
            } else {
                VStack(spacing: 10) {
                    Text("Official wallpapers are available to download.")
                    if let set = selected {
                        Button("Download \(set.title)") { Task { await wallpapers.ensure(set) } }
                    }
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            HStack {
                Text(wallpapers.message).font(.caption).foregroundStyle(.secondary)
                Spacer()
                Link("Official Minecraft collection", destination: URL(string: "https://www.minecraft.net/en-us/collectibles")!)
                    .font(.caption)
            }
        }
        .padding(18)
        .frame(width: 665, height: 427)
        .onAppear { wallpapers.collection = wallpapers.set(for: instance?.version ?? "")?.id ?? "" }
    }
}
