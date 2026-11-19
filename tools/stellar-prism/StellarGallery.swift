import AppKit
import SwiftUI

private let canvas = Color(white: 0.06)
private let panel = Color(white: 0.09)
private let secondary = Color(white: 0.64)
private let line = Color.white.opacity(0.12)

struct StellarGallery: View {
    @ObservedObject var library: StellarLibrary
    @ObservedObject var wallpapers: WallpaperStore
    private enum GalleryTarget { case create }

    var body: some View {
        GeometryReader { geometry in
            if library.instances.isEmpty {
                createTile(width: geometry.size.width - 10, height: geometry.size.height - 10)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .overlay {
                        RightClickRegion(enabled: library.panel == nil) { library.panel = .library }.accessibilityHidden(true)
                    }
                    .accessibilityAction(named: Text("Library options")) { library.panel = .library }
            } else {
                inventory(size: geometry.size)
            }
        }
        .overlay {
            if library.opening {
                ZStack {
                    canvas
                    if let icon = NSImage(named: NSImage.applicationIconName) {
                        Image(nsImage: icon).resizable().scaledToFit().frame(width: 96)
                    }
                }
                .transition(.opacity)
                .allowsHitTesting(false)
                .accessibilityHidden(true)
            }
        }
        .background(canvas)
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
        .ignoresSafeArea()
        .foregroundStyle(Color(white: 0.95))
        .preferredColorScheme(.dark)
        .overlay(alignment: .top) {
            WindowDragRegion().frame(height: 7).accessibilityHidden(true)
        }
        .overlay {
            if let launch = library.launchVisual {
                WallpaperLaunchOverlay(artwork: wallpapers.artwork(for: launch.instance),
                                       progress: launch.progress, animation: launch.animation)
                    .transition(.opacity)
            }
        }
        .overlay(alignment: .center) {
            if library.panel != nil {
                LibrarySheet(library: library, wallpapers: wallpapers)
                    .shadow(color: .black.opacity(0.72), radius: 24, y: 10)
                    .padding(12)
            }
        }
        .alert("Stellar Launcher", isPresented: Binding(
            get: { !library.notice.isEmpty },
            set: { if !$0 { library.notice = "" } }
        )) {
            Button("OK", role: .cancel) { library.notice = "" }
        } message: {
            Text(library.notice)
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
                    createTile(width: width, height: height).id(GalleryTarget.create)
                }
                .padding(gutter)
                .background(HorizontalScrollChrome())
            }
            .scrollIndicators(.never, axes: .horizontal)
            .onChange(of: library.selection, initial: true) { _, selected in
                if !library.createFocused, let selected { scroll.scrollTo(selected) }
            }
            .onChange(of: library.createFocused) { _, focused in
                if focused { scroll.scrollTo(GalleryTarget.create) }
                else if let selected = library.selection { scroll.scrollTo(selected) }
            }
        }
    }

    private func card(_ instance: GameInstance, size: CGSize) -> some View {
        let selected = !library.createFocused && library.selection == instance.id
        return Button {
            library.createFocused = false
            library.selection = instance.id
            library.panel = nil
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
            library.createFocused = false
            library.selection = instance.id
            library.play(instance)
        })
        .overlay {
            RightClickRegion(enabled: library.panel == nil) { openActions(for: instance) }.accessibilityHidden(true)
        }
        .accessibilityLabel(instance.name)
        .accessibilityValue("\(instance.version), \(instance.loader)")
        .accessibilityAddTraits(selected ? [.isSelected] : [])
        .accessibilityAction(named: Text("Instance options")) { openActions(for: instance) }
    }

    private func openActions(for instance: GameInstance) {
        library.createFocused = false
        library.selection = instance.id
        library.panel = .instance
    }

    private func createTile(width: CGFloat, height: CGFloat) -> some View {
        Button {
            library.createFocused = true
            library.create()
        } label: {
            VStack(spacing: 9) {
                Image(systemName: "plus").font(.system(size: 24, weight: .light))
                Text("Create New Instance").font(.system(size: 14, weight: .medium))
            }
            .frame(width: width, height: height)
            .background(Color(white: 0.095), in: RoundedRectangle(cornerRadius: 13))
            .overlay(RoundedRectangle(cornerRadius: 13).strokeBorder(
                .white.opacity(library.createFocused ? 0.88 : 0.12),
                lineWidth: library.createFocused ? 2 : 1))
        }
        .buttonStyle(.plain)
    }

}

private struct HorizontalScrollChrome: NSViewRepresentable {
    func makeNSView(context: Context) -> Region { Region() }
    func updateNSView(_ view: Region, context: Context) { view.hideIndicator() }

    final class Region: NSView {
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            hideIndicator()
        }

        override func hitTest(_ point: NSPoint) -> NSView? { nil }

        func hideIndicator() {
            guard let scroll = enclosingScrollView else { return }
            scroll.hasHorizontalScroller = false
            scroll.horizontalScroller?.isHidden = true
        }
    }
}

private struct WindowDragRegion: NSViewRepresentable {
    func makeNSView(context: Context) -> Region { Region() }
    func updateNSView(_ view: Region, context: Context) {}

    final class Region: NSView {
        override func mouseDown(with event: NSEvent) { window?.performDrag(with: event) }
    }
}

private struct RightClickRegion: NSViewRepresentable {
    let enabled: Bool
    let action: () -> Void

    func makeNSView(context: Context) -> Region { Region() }

    func updateNSView(_ view: Region, context: Context) {
        view.action = enabled ? action : nil
    }

    final class Region: NSView {
        var action: (() -> Void)?
        private var monitor: Any?

        override func hitTest(_ point: NSPoint) -> NSView? { nil }

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            if let monitor { NSEvent.removeMonitor(monitor) }
            monitor = nil
            guard window != nil else { return }
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.rightMouseDown, .leftMouseDown]) { [weak self] event in
                let handled = MainActor.assumeIsolated {
                    guard let self, let action = self.action, self.window === event.window,
                          event.type == .rightMouseDown || event.modifierFlags.contains(.control),
                          self.visibleRect.contains(self.convert(event.locationInWindow, from: nil)) else { return false }
                    action()
                    return true
                }
                return handled ? nil : event
            }
        }
    }
}

private struct LibrarySheet: View {
    @ObservedObject var library: StellarLibrary
    @ObservedObject var wallpapers: WallpaperStore

    var body: some View {
        if library.panel == .wallpapers {
            WallpaperBrowser(wallpapers: wallpapers, instance: library.selected) { library.panel = nil }
        } else if library.panel == .create {
            CreateInstancePanel(library: library)
        } else if library.panel == .settings {
            GallerySettings { library.panel = nil }
        } else if library.panel == .resources, let instance = library.selected {
            ResourcePanel(instance: instance) { library.panel = nil }.id(instance.id)
        } else {
            VStack(alignment: .leading, spacing: 12) {
                GalleryPanelHeader(title: library.panel == .instance ? library.selected?.name ?? "Instance" : "Library") {
                    library.panel = nil
                }
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: 4), spacing: 8) {
                    if library.panel == .instance, let instance = library.selected {
                        action("Play", image: "play.fill") {
                            library.panel = nil
                            library.play(instance)
                        }
                        action("Edit Instance", image: "slider.horizontal.3") {
                            library.panel = nil
                            library.edit(instance)
                        }
                        action("Mods, Packs & Shaders", image: "square.stack.3d.up") { library.panel = .resources }
                        action("Minecraft Accounts", image: "person.crop.circle") {
                            library.panel = nil
                            library.accounts()
                        }
                        action("Change Skin", image: "person.crop.square") {
                            library.panel = nil
                            library.skins()
                        }
                        action("Show in Finder", image: "folder") {
                            library.panel = nil
                            NSWorkspace.shared.open(instance.folder)
                        }
                        action("Change Wallpaper", image: "photo") { library.panel = .wallpapers }
                        if wallpapers.hasChoice(for: instance) {
                            action("Use Default Wallpaper", image: "arrow.counterclockwise") {
                                wallpapers.reset(instance)
                                library.panel = nil
                            }
                        } else {
                            action("Settings", image: "gearshape") { library.panel = .settings }
                        }
                    } else {
                        action("New Instance", image: "plus") { library.create() }
                        action("Import Instance", image: "square.and.arrow.down") {
                            library.panel = nil
                            library.importPack()
                        }
                        action("Copy Existing Instance", image: "square.on.square") {
                            library.panel = nil
                            library.copyInstance()
                        }
                        action("Minecraft Accounts", image: "person.crop.circle") {
                            library.panel = nil
                            library.accounts()
                        }
                        action("Wallpapers", image: "photo") { library.panel = .wallpapers }
                        action("Settings", image: "gearshape") { library.panel = .settings }
                    }
                }
            }
            .padding(14)
            .frame(width: 300)
            .galleryPanel()
        }
    }

    private func action(_ title: String, image: String, perform: @escaping () -> Void) -> some View {
        Button(action: perform) {
            Label(title, systemImage: image)
                .labelStyle(.iconOnly)
                .font(.system(size: 17, weight: .medium))
                .frame(maxWidth: .infinity)
                .frame(height: 37)
        }
        .buttonStyle(.bordered)
        .help(title)
    }
}

private struct WallpaperBrowser: View {
    @ObservedObject var wallpapers: WallpaperStore
    let instance: GameInstance?
    let dismiss: () -> Void
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
        .frame(width: 564, height: 322)
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
