import AppKit
import QuartzCore
import SwiftUI

@MainActor
final class Stellar: NSObject, NSApplicationDelegate {
    private let library = StellarLibrary()
    private let wallpapers = WallpaperStore()
    private var window: NSWindow?
    private var keyboardMonitor: Any?

    func applicationDidFinishLaunching(_ notification: Notification) {
        installMenu()
        library.refresh()
        let animate = !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion &&
            !UserDefaults.standard.bool(forKey: "DisableOpeningAnimation")
        library.opening = animate

        let content = NSHostingController(rootView: StellarGallery(library: library, wallpapers: wallpapers))
        let window = NSWindow(contentRect: .init(x: 0, y: 0, width: 818, height: 414),
                              styleMask: [.titled, .fullSizeContentView, .closable, .miniaturizable],
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
        let size = NSSize(width: 818, height: 414)
        window.contentMinSize = size
        window.contentMaxSize = size
        if !window.setFrameUsingName("Stellar Gallery") { window.center() }
        window.setContentSize(size)
        window.setFrameAutosaveName("Stellar Gallery")
        self.window = window
        keyboardMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            let handled = MainActor.assumeIsolated { self?.handleKey(event) == true }
            return handled ? nil : event
        }
        let destination = window.frame
        if animate {
            window.alphaValue = 0
            window.setFrame(destination.insetBy(dx: 10, dy: 5), display: false)
        }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        window.orderFrontRegardless()
        if animate {
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.25
                context.timingFunction = CAMediaTimingFunction(name: .easeOut)
                window.animator().setFrame(destination, display: true)
                window.animator().alphaValue = 1
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.16) { [weak self] in
                withAnimation(.easeOut(duration: 0.29)) { self?.library.opening = false }
            }
        }
    }

    func applicationDidBecomeActive(_ notification: Notification) { library.refresh() }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows: Bool) -> Bool {
        if let window {
            if window.isMiniaturized { window.deminiaturize(nil) }
            window.makeKeyAndOrderFront(nil)
        }
        return true
    }

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
            library.panel = library.selected == nil || library.createFocused ? .library : .instance
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
        case .instance, .library, .resources, .create, .settings:
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
                if !event.isARepeat { library.activateSelection() }
            case .escape:
                return library.dismissLaunch()
            case .context: return false
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
    @objc private func createInstance() { library.create() }
    @objc private func copyInstance() { library.copyInstance() }
    @objc private func accounts() { library.accounts() }
    @objc private func skins() { library.skins() }
    @objc private func settings() { library.panel = .settings }
    @objc private func importPack() { library.importPack() }
    @objc private func closeWindow() { window?.close() }

    private func installMenu() {
        let main = NSMenu()
        let app = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Settings…", action: #selector(settings), keyEquivalent: ",").target = self
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Quit Stellar Launcher", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        app.submenu = appMenu
        main.addItem(app)

        let libraryItem = NSMenuItem(title: "Library", action: nil, keyEquivalent: "")
        let libraryMenu = NSMenu(title: "Library")
        for (title, selector, key) in [
            ("New Instance…", #selector(createInstance), "n"),
            ("Import Instance…", #selector(importPack), "o"),
            ("Copy Existing Instance…", #selector(copyInstance), ""),
            ("Refresh Instances", #selector(refresh), "r"),
            ("Minecraft Accounts…", #selector(accounts), ""),
            ("Change Skin…", #selector(skins), ""),
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
