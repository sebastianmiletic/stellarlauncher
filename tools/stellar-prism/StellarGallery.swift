import AppKit
import SwiftUI

private let canvas = Color(white: 0.06)
private let panel = Color(white: 0.09)
private let secondary = Color(white: 0.64)
private let line = Color.white.opacity(0.12)

struct StellarGallery: View {
    @ObservedObject var library: PrismLibrary
    @ObservedObject var wallpapers: WallpaperStore

    var body: some View {
        GeometryReader { geometry in
            if library.instances.isEmpty {
                empty
            } else {
                inventory(size: geometry.size)
            }
        }
        .background(canvas)
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
        .ignoresSafeArea()
        .foregroundStyle(Color(white: 0.95))
        .preferredColorScheme(.dark)
        .sheet(isPresented: Binding(
            get: { library.panel != nil },
            set: { if !$0 { library.panel = nil } }
        )) {
            LibrarySheet(library: library, wallpapers: wallpapers)
        }
        .task(id: library.instances.map(\.version)) {
            for version in Set(library.instances.map(\.version)) {
                if let set = wallpapers.set(for: version) { await wallpapers.ensure(set) }
            }
        }
    }

    private func inventory(size: CGSize) -> some View {
        let visible = min(library.instances.count, max(1, Int(size.width / 220)))
        let gutter: CGFloat = 5
        let width = (size.width - 2 * gutter - CGFloat(visible - 1) * gutter) / CGFloat(visible)
        let height = max(1, size.height - 2 * gutter)
        return ScrollViewReader { scroll in
            ScrollView(.horizontal) {
                LazyHStack(spacing: gutter) {
                    ForEach(library.instances) { instance in
                        card(instance, size: CGSize(width: width, height: height)).id(instance.id)
                    }
                }.padding(gutter)
            }
            .scrollIndicators(.hidden)
            .onChange(of: library.selection, initial: true) { _, selected in
                if let selected { scroll.scrollTo(selected) }
            }
        }
    }

    private func card(_ instance: GameInstance, size: CGSize) -> some View {
        let selected = library.selection == instance.id
        return Button {
            library.selection = instance.id
        } label: {
            ZStack(alignment: .bottomLeading) {
                WallpaperImage(url: wallpapers.artwork(for: instance))
                LinearGradient(colors: [.black.opacity(0.87), .black.opacity(0)],
                               startPoint: .bottom, endPoint: .top).frame(height: 132)
                VStack(alignment: .leading, spacing: 5) {
                    Text(instance.name).font(.system(size: 17, weight: .semibold)).lineLimit(1)
                    Text(instance.version.isEmpty ? instance.loader : "\(instance.version) · \(instance.loader)")
                        .font(.system(size: 12, weight: .medium)).foregroundStyle(Color(white: 0.78))
                }
                .padding(.horizontal, 16)
                .padding(.bottom, 17)
            }
            .frame(width: size.width, height: size.height)
            .clipShape(RoundedRectangle(cornerRadius: 13, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 13, style: .continuous)
                .strokeBorder(Color.white.opacity(selected ? 0.88 : 0), lineWidth: 2))
        }
        .buttonStyle(.plain)
        .simultaneousGesture(TapGesture(count: 2).onEnded {
            library.selection = instance.id
            if library.prism != nil { library.play(instance) }
        })
        .overlay {
            RightClickRegion { openActions(for: instance) }.accessibilityHidden(true)
        }
        .accessibilityLabel(instance.name)
        .accessibilityValue("\(instance.version), \(instance.loader)")
        .accessibilityAddTraits(selected ? [.isSelected] : [])
        .accessibilityAction(named: Text("Instance options")) { openActions(for: instance) }
    }

    private func openActions(for instance: GameInstance) {
        library.selection = instance.id
        library.panel = .instance
    }

    private var empty: some View {
        VStack(spacing: 8) {
            Text(library.prism == nil ? "Install Prism Launcher" : "No instances")
                .font(.system(size: 20, weight: .semibold))
            Text(library.prism == nil ? "Prism handles sign-in and game installation." : "Right-click for library options.")
                .foregroundStyle(secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .overlay {
            RightClickRegion { library.panel = .library }.accessibilityHidden(true)
        }
        .accessibilityAction(named: Text("Library options")) { library.panel = .library }
    }
}

private struct RightClickRegion: NSViewRepresentable {
    let action: () -> Void

    func makeNSView(context: Context) -> Region { Region() }

    func updateNSView(_ view: Region, context: Context) { view.action = action }

    final class Region: NSView {
        var action: () -> Void = {}
        private var monitor: Any?

        override func hitTest(_ point: NSPoint) -> NSView? { nil }

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            if let monitor { NSEvent.removeMonitor(monitor) }
            monitor = nil
            guard window != nil else { return }
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.rightMouseDown, .leftMouseDown]) { [weak self] event in
                let handled = MainActor.assumeIsolated {
                    guard let self, self.window === event.window,
                          event.type == .rightMouseDown || event.modifierFlags.contains(.control),
                          self.visibleRect.contains(self.convert(event.locationInWindow, from: nil)) else { return false }
                    self.action()
                    return true
                }
                return handled ? nil : event
            }
        }
    }
}

private struct LibrarySheet: View {
    @ObservedObject var library: PrismLibrary
    @ObservedObject var wallpapers: WallpaperStore

    var body: some View {
        if library.panel == .wallpapers {
            WallpaperBrowser(wallpapers: wallpapers, instance: library.selected)
        } else {
            VStack(alignment: .leading, spacing: 0) {
                HStack {
                    Text(library.panel == .instance ? library.selected?.name ?? "Instance" : "Library")
                        .font(.system(size: 17, weight: .semibold)).lineLimit(1)
                    Spacer()
                    Button("Done") { library.panel = nil }.keyboardShortcut(.cancelAction)
                }
                .padding(.horizontal, 16)
                .frame(height: 49)
                Rectangle().fill(line).frame(height: 1)
                VStack(alignment: .leading, spacing: 4) {
                    if library.panel == .instance, let instance = library.selected {
                        action("Play", image: "play.fill") {
                            library.panel = nil
                            library.play(instance)
                        }.disabled(library.prism == nil)
                        action("Edit in Prism", image: "slider.horizontal.3") {
                            library.panel = nil
                            library.edit(instance)
                        }.disabled(library.prism == nil)
                        action("Show in Finder", image: "folder") {
                            library.panel = nil
                            NSWorkspace.shared.open(instance.folder)
                        }
                        Rectangle().fill(line).frame(height: 1).padding(.vertical, 4)
                        action("Change Wallpaper", image: "photo") { library.panel = .wallpapers }
                        if wallpapers.hasChoice(for: instance) {
                            action("Use Default Wallpaper", image: "arrow.counterclockwise") {
                                wallpapers.reset(instance)
                                library.panel = nil
                            }
                        }
                    } else {
                        action("Import Instance", image: "square.and.arrow.down") {
                            library.panel = nil
                            DispatchQueue.main.async { library.importPack() }
                        }.disabled(library.prism == nil)
                        action("Open Prism Launcher", image: "arrow.up.right.square") {
                            library.panel = nil
                            library.manage()
                        }
                        action("Wallpapers", image: "photo") { library.panel = .wallpapers }
                    }
                }.padding(12)
            }
            .background(canvas)
            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
            .frame(width: 360)
        }
    }

    private func action(_ title: String, image: String, perform: @escaping () -> Void) -> some View {
        Button(action: perform) {
            Label(title, systemImage: image)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.vertical, 4)
        }
        .buttonStyle(.bordered)
    }
}

private struct WallpaperBrowser: View {
    @ObservedObject var wallpapers: WallpaperStore
    let instance: GameInstance?
    @Environment(\.dismiss) private var dismiss
    private var selected: WallpaperSet? { wallpapers.sets.first { $0.id == wallpapers.collection } }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                Text("Wallpapers").font(.system(size: 18, weight: .semibold))
                Spacer()
                Menu {
                    Button("Save All Collections", systemImage: "arrow.down.circle") {
                        Task { await wallpapers.downloadAll() }
                    }.disabled(wallpapers.downloading)
                    Button("Show Files", systemImage: "folder") { NSWorkspace.shared.open(wallpapers.folder) }
                    Link("Official Minecraft Gallery", destination: URL(string: "https://www.minecraft.net/en-us/collectibles")!)
                } label: {
                    Image(systemName: "ellipsis")
                }.accessibilityLabel("Wallpaper Actions").fixedSize()
                Button("Done") { dismiss() }.keyboardShortcut(.cancelAction)
            }
            .padding(.horizontal, 18)
            .frame(height: 52)
            Rectangle().fill(line).frame(height: 1)

            HStack(spacing: 0) {
                collections.frame(width: 174)
                Rectangle().fill(line).frame(width: 1)
                artworkGrid
            }.frame(maxHeight: .infinity)
            Rectangle().fill(line).frame(height: 1)
            HStack {
                Text(status).font(.system(size: 11)).foregroundStyle(secondary).lineLimit(1)
                Spacer()
                Button(instance == nil ? "Open Image" : "Use Wallpaper") { useSelection() }
                    .buttonStyle(.borderedProminent)
                    .tint(Color(white: 0.29))
                    .disabled(wallpapers.selectedImage == nil)
            }
            .padding(.horizontal, 18)
            .frame(height: 52)
        }
        .background(canvas)
        .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
        .frame(width: 620, height: 362)
        .onAppear { wallpapers.collection = wallpapers.set(for: instance?.version ?? "")?.id ?? "" }
    }

    private var status: String {
        if wallpapers.downloading { return "Saving \(wallpapers.completed) of \(wallpapers.sets.count) collections…" }
        if !wallpapers.message.isEmpty { return wallpapers.message }
        return "\(wallpapers.visibleImages.count) images"
    }

    private var collections: some View {
        ScrollViewReader { scroll in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach(wallpapers.sets) { set in
                        let active = wallpapers.collection == set.id
                        Button {
                            wallpapers.collection = set.id
                        } label: {
                            Text(set.title).lineLimit(1).font(.system(size: 12, weight: active ? .semibold : .regular))
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding(.horizontal, 9).padding(.vertical, 6)
                                .background(active ? Color.white.opacity(0.13) : Color.clear, in: RoundedRectangle(cornerRadius: 5))
                        }
                        .buttonStyle(.plain)
                        .accessibilityAddTraits(active ? [.isSelected] : [])
                        .id(set.id)
                    }
                }.padding(7)
            }
            .onChange(of: wallpapers.collection, initial: true) { _, collection in scroll.scrollTo(collection) }
            .background(panel)
        }
    }

    private var artworkGrid: some View {
        Group {
            if !wallpapers.visibleImages.isEmpty {
                ScrollViewReader { scroll in
                    ScrollView {
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 9), count: 3), spacing: 9) {
                            ForEach(wallpapers.visibleImages, id: \.self) { file in
                                let active = wallpapers.selectedImage == file
                                Button {
                                    wallpapers.highlighted = file
                                } label: {
                                    WallpaperImage(url: file)
                                        .frame(height: 74)
                                        .clipShape(RoundedRectangle(cornerRadius: 5))
                                        .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(
                                            active ? Color.white : line, lineWidth: active ? 2 : 1
                                        ))
                                }
                                .buttonStyle(.plain)
                                .accessibilityLabel(file.lastPathComponent)
                                .accessibilityAddTraits(active ? [.isSelected] : [])
                                .id(file)
                            }
                        }.padding(12)
                    }
                    .onChange(of: wallpapers.selectedImage, initial: true) { _, image in
                        if let image { scroll.scrollTo(image) }
                    }
                }
            } else {
                VStack(spacing: 9) {
                    Text(selected?.title ?? "No collection").font(.system(size: 14, weight: .semibold))
                    if let set = selected {
                        Button("Download Collection") { Task { await wallpapers.ensure(set) } }
                    }
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }.frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func useSelection() {
        guard let image = wallpapers.selectedImage else { return }
        if let instance {
            wallpapers.choose(image, for: instance)
            dismiss()
        } else {
            NSWorkspace.shared.open(image)
        }
    }
}
