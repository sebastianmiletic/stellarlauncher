# Stellar Launcher

Stellar Launcher is a compact Minecraft Java launcher for macOS based on Prism Launcher. It is an independent fork. It includes Prism’s existing instance and resource managers plus a Stellar theme, live library playtime and a Minecraft version switch that checks matching mods and dependencies.

- **Change Minecraft version** in **Edit → Version**. Select the Minecraft component and choose **Change version**. Review compatible mod downloads after the component change. Stellar reports missing and locked mods so you can review them before launching.
- **Play Minecraft** by signing in under **Accounts → Add Microsoft account**. Stellar installs Java and Minecraft directly. Open a saved world from **Edit → Worlds** or **Singleplayer** in Minecraft.
- **Development builds** require a Microsoft application registration approved by Minecraft Services. Set its application (client) ID in **Settings → Services**, or provide `ARCHIVE_MSA_CLIENT_ID` at build time. Enable public client flows for personal Microsoft accounts and submit the ID via the [Minecraft API application](https://aka.ms/mce-reviewappid). For browser sign-in, register the Stellar desktop redirect URI `archivelauncher://oauth/microsoft` and, for loopback sign-in, `http://localhost`. Use public client authentication.
- **Stellar Look** adds an orbiting black-hole panorama and grayscale menus to Minecraft Java 1.21.9 through 26.3. Install the pack from File and enable it in Minecraft’s Resource Packs menu. Reinstall Stellar Look from the same menu to refresh an older copy.
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
