import AppKit

private let prismID = "org.prismlauncher.PrismLauncher"

final class Stellar: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        let workspace = NSWorkspace.shared
        guard let prism = workspace.urlForApplication(withBundleIdentifier: prismID) else {
            showError("Install Prism Launcher to open your Stellar library.")
            return
        }

        do {
            let running = !NSRunningApplication.runningApplications(withBundleIdentifier: prismID).isEmpty
            try installTheme(activate: !running)
        } catch {
            showError("Stellar couldn’t install its theme. \(error.localizedDescription)")
            return
        }

        let configuration = NSWorkspace.OpenConfiguration()
        configuration.activates = true
        workspace.openApplication(at: prism, configuration: configuration) { _, error in
            DispatchQueue.main.async {
                if let error {
                    self.showError(error.localizedDescription)
                } else {
                    NSApp.terminate(nil)
                }
            }
        }
    }

    private func installTheme(activate: Bool) throws {
        let files = FileManager.default
        let support = try files.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        let data = support.appendingPathComponent("PrismLauncher", isDirectory: true)
        let source = Bundle.main.resourceURL!
        let assets = [
            "themes/stellar_prism/theme.json",
            "themes/stellar_prism/themeStyle.css",
            "catpacks/stellar_prism/catpack.json",
            "catpacks/stellar_prism/stellar-library.png",
            "icons/stellar-instance.png",
        ]
        for path in assets {
            let destination = data.appendingPathComponent(path)
            let relativeSource = path.hasPrefix("icons/") ? destination.lastPathComponent : path
            let contents = try Data(contentsOf: source.appendingPathComponent(relativeSource))
            try files.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
            if (try? Data(contentsOf: destination)) != contents {
                try contents.write(to: destination, options: .atomic)
            }
        }

        // Prism keeps a live copy of its configuration. Activate on a cold
        // start so we do not overwrite settings the running process owns.
        guard activate else { return }
        let config = data.appendingPathComponent("prismlauncher.cfg")
        var contents = ""
        if files.fileExists(atPath: config.path) {
            contents = try String(contentsOf: config, encoding: .utf8)
        }
        var updated = contents
        let appearance = [
            ("ApplicationTheme", "stellar_prism"), ("IconTheme", "flat_white"),
            ("BackgroundCat", "stellar_prism"), ("TheCat", "true"),
            ("CatOpacity", "100"), ("CatFit", "fit"), ("ToolbarsLocked", "true"),
        ]
        for (key, value) in appearance {
            let expression = try NSRegularExpression(pattern: "(?m)^" + key + "=[^\r\n]*")
            let range = NSRange(updated.startIndex..<updated.endIndex, in: updated)
            if expression.firstMatch(in: updated, range: range) == nil {
                updated = key + "=" + value + "\n" + updated
            } else {
                updated = expression.stringByReplacingMatches(in: updated, range: range, withTemplate: key + "=" + value)
            }
        }
        if updated != contents {
            try updated.write(to: config, atomically: true, encoding: .utf8)
        }

        let instance = data.appendingPathComponent("instances/Stellar/instance.cfg")
        if let current = try? String(contentsOf: instance, encoding: .utf8) {
            let branded = current.replacingOccurrences(
                of: #"(?m)^iconKey=default$"#, with: "iconKey=stellar-instance", options: .regularExpression
            )
            if branded != current {
                try branded.write(to: instance, atomically: true, encoding: .utf8)
            }
        }
    }

    private func showError(_ message: String) {
        let alert = NSAlert()
        alert.messageText = "Stellar Launcher"
        alert.informativeText = message
        alert.runModal()
        NSApp.terminate(nil)
    }
}

let application = NSApplication.shared
let stellar = Stellar()
application.delegate = stellar
application.run()
