# Stellar Launcher

Stellar Launcher is a compact Minecraft Java launcher for macOS based on Prism Launcher. It is an independent fork. It includes Prism’s existing instance and resource managers plus a Stellar theme, live library playtime and a Minecraft version switch that checks matching mods and dependencies.

- **Change Minecraft version** in **Edit → Version**. Select the Minecraft component and choose **Change version**. Review compatible mod downloads after the component change. Stellar reports missing and locked mods so you can review them before launching.
- **Accounts** lets you add and switch between Microsoft accounts. Set a compatible Microsoft app client ID in **Settings → Services** to enable sign-in for the fork. CurseForge services use an optional key configured on the same page.
- **Play using Prism on macOS** from **File → Play with Prism**. Choose an installed Prism instance and Prism handles Microsoft sign-in and launches it with its existing settings. Choose **Import selected Stellar instance into Prism** in the same menu to export a copy and open Prism’s import screen. After importing, select the instance from **Play with Prism**.
- **Stellar Look** adds the black-hole title screen and grayscale menus to Minecraft Java 1.21.9 through 26.3. Install the pack from File and enable it in Minecraft’s Resource Packs menu.
- **Updates** use Sparkle on macOS. Stellar checks in the background on startup when automatic updates are enabled. Updates are installed from signed releases. Private GitHub Releases require authentication, so a private repository needs a separately accessible HTTPS feed and download endpoint for Sparkle.

## Build on Apple silicon

Install the dependencies and initialize the git submodule before running the build.

```sh
brew install cmake ninja extra-cmake-modules qtbase qtsvg qtnetworkauth cmark qrencode tomlplusplus libarchive openjdk@17
git submodule update --init --recursive
./tools/archive/build-macos.sh
```

The script builds and tests Stellar and places the signed local app in `../dist/Stellar Launcher.app`. The build accepts `ARCHIVE_MSA_CLIENT_ID`, `ARCHIVE_CURSEFORGE_API_KEY`, `ARCHIVE_UPDATE_FEED_URL` and `ARCHIVE_UPDATE_PUBLIC_KEY`. Set both update variables to empty for a build without Sparkle.

## Release on macOS

Bump the version in `CMakeLists.txt` before building. Keep the private Sparkle update key in the macOS Keychain under the account `archivelauncher`. Use the matching public key in builds. For public GitHub Releases, keep the app’s feed URL pointed to the latest release’s `appcast.xml`. Package the app and feed with `tools/archive/package-update.py` and publish both outputs to the same release. The enclosure must point at the exact tagged ZIP, served over HTTPS. A local Apple ad-hoc signature supports development. A distributed build should use an Apple Developer ID signature and notarization.

Stellar inherits GPL-3.0-only code and contributor notices from Prism Launcher. See [LICENSE](LICENSE), [COPYING.md](COPYING.md), and the [upstream README](README.upstream.md).
