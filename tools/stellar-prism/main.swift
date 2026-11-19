import AppKit
import SwiftUI

@MainActor
final class Stellar: NSObject, NSApplicationDelegate {
    private let library = PrismLibrary()
    private let wallpapers = WallpaperStore()
    private var window: NSWindow?

    func applicationDidFinishLaunching(_ notification: Notification) {
        installMenu()
        library.refresh()

        let content = NSHostingController(rootView: StellarGallery(library: library, wallpapers: wallpapers))
        let window = NSWindow(contentRect: .init(x: 0, y: 0, width: 818, height: 414),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "Stellar Launcher"
        window.appearance = NSAppearance(named: .darkAqua)
        window.contentViewController = content
        window.contentMinSize = NSSize(width: 610, height: 350)
        window.setContentSize(NSSize(width: 818, height: 414))
        window.center()
        self.window = window
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationDidBecomeActive(_ notification: Notification) { library.refresh() }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    @objc private func refresh() { library.refresh() }

    private func installMenu() {
        let main = NSMenu()
        let app = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit Stellar Launcher", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        app.submenu = appMenu
        main.addItem(app)

        let libraryItem = NSMenuItem(title: "Library", action: nil, keyEquivalent: "")
        let libraryMenu = NSMenu(title: "Library")
        let reload = libraryMenu.addItem(withTitle: "Refresh Instances", action: #selector(refresh), keyEquivalent: "r")
        reload.target = self
        libraryItem.submenu = libraryMenu
        main.addItem(libraryItem)
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
