import AppKit
import SwiftUI

@MainActor
final class Stellar: NSObject, NSApplicationDelegate {
    private let library = PrismLibrary()
    private let wallpapers = WallpaperStore()
    private var window: NSWindow?
    private var keyboardMonitor: Any?

    func applicationDidFinishLaunching(_ notification: Notification) {
        installMenu()
        library.refresh()

        let content = NSHostingController(rootView: StellarGallery(library: library, wallpapers: wallpapers))
        let window = NSWindow(contentRect: .init(x: 0, y: 0, width: 818, height: 414),
                              styleMask: [.titled, .fullSizeContentView, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "Stellar Launcher"
        window.appearance = NSAppearance(named: .darkAqua)
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.toolbarStyle = .unifiedCompact
        window.titlebarSeparatorStyle = .none
        for button in [NSWindow.ButtonType.closeButton, .miniaturizeButton, .zoomButton] {
            window.standardWindowButton(button)?.isHidden = true
        }
        window.isMovableByWindowBackground = true
        window.contentViewController = content
        window.contentMinSize = NSSize(width: 610, height: 355)
        window.setContentSize(NSSize(width: 818, height: 414))
        window.center()
        self.window = window
        keyboardMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            let handled = MainActor.assumeIsolated { self?.handleKey(event) == true }
            return handled ? nil : event
        }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationDidBecomeActive(_ notification: Notification) { library.refresh() }

    func applicationWillTerminate(_ notification: Notification) {
        if let keyboardMonitor { NSEvent.removeMonitor(keyboardMonitor) }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    private enum Key: UInt16 {
        case enter = 36, escape = 53, keypadEnter = 76, home = 115, end = 119, context = 109
        case left = 123, right = 124, down = 125, up = 126
    }

    private func handleKey(_ event: NSEvent) -> Bool {
        guard let key = Key(rawValue: event.keyCode), let window, let keyWindow = event.window,
              keyWindow === window || keyWindow.sheetParent === window,
              !(keyWindow.firstResponder is NSTextView) else { return false }
        let modifiers = event.modifierFlags.intersection([.command, .option, .control, .shift])

        if key == .context && modifiers == .shift && library.panel == nil {
            library.panel = library.selected == nil ? .library : .instance
            return true
        }
        guard modifiers.isEmpty else { return false }
        switch library.panel {
        case .wallpapers:
            switch key {
            case .left: wallpapers.moveHighlight(by: -1)
            case .right: wallpapers.moveHighlight(by: 1)
            case .down: wallpapers.moveHighlight(by: 3)
            case .up: wallpapers.moveHighlight(by: -3)
            case .home: wallpapers.selectBoundary(last: false)
            case .end: wallpapers.selectBoundary(last: true)
            case .enter, .keypadEnter:
                if !event.isARepeat { applyWallpaper() }
            case .escape: library.panel = nil
            case .context: return false
            }
        case .instance:
            if key == .escape {
                library.panel = nil
            } else {
                return false
            }
        case .library:
            guard key == .escape else { return false }
            library.panel = nil
        case nil:
            guard keyWindow === window else { return false }
            switch key {
            case .left, .up: library.moveSelection(by: -1)
            case .right, .down: library.moveSelection(by: 1)
            case .home: library.selectBoundary(last: false)
            case .end: library.selectBoundary(last: true)
            case .enter, .keypadEnter:
                if !event.isARepeat { library.playSelected() }
            case .context, .escape: return false
            }
        }
        return true
    }

    private func applyWallpaper() {
        guard let image = wallpapers.selectedImage else { return }
        if let instance = library.selected {
            wallpapers.choose(image, for: instance)
            library.panel = nil
        } else {
            NSWorkspace.shared.open(image)
        }
    }

    @objc private func refresh() { library.refresh() }
    @objc private func openPrism() { library.manage() }
    @objc private func importPack() { library.importPack() }
    @objc private func closeWindow() { window?.close() }

    private func installMenu() {
        let main = NSMenu()
        let app = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit Stellar Launcher", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        app.submenu = appMenu
        main.addItem(app)

        let libraryItem = NSMenuItem(title: "Library", action: nil, keyEquivalent: "")
        let libraryMenu = NSMenu(title: "Library")
        for (title, selector, key) in [
            ("Import Instance…", #selector(importPack), "o"),
            ("Refresh Instances", #selector(refresh), "r"),
            ("Open Prism Launcher", #selector(openPrism), ""),
        ] {
            libraryMenu.addItem(withTitle: title, action: selector, keyEquivalent: key).target = self
        }
        libraryItem.submenu = libraryMenu
        main.addItem(libraryItem)

        let windowItem = NSMenuItem(title: "Window", action: nil, keyEquivalent: "")
        let windowMenu = NSMenu(title: "Window")
        windowMenu.addItem(withTitle: "Close Window", action: #selector(closeWindow), keyEquivalent: "w").target = self
        windowMenu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        windowItem.submenu = windowMenu
        main.addItem(windowItem)
        NSApp.windowsMenu = windowMenu
        NSApp.mainMenu = main
    }
}

MainActor.assumeIsolated {
    let application = NSApplication.shared
    application.setActivationPolicy(.regular)
    let stellar = Stellar()
    application.delegate = stellar
    application.run()
}
