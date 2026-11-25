# Stellar patches for the Rust engine

These patches target Horizon revision `0a875da91dbb0fdcb7ba8c42c11341aa229af51a`. They change engine code and require a new engine build. Stellar Launcher 1.5.11 still uses Horizon's published engine.

- [Title motion](stellar-title-motion.patch) keeps Stellar's cubemap and shadow fixed, animates light around the accretion disk and lensed arc, and twinkles the stars. The renderer enables the effect only when the active resource stack includes the Stellar panorama motion marker. The same renderer path serves the startup menu and the returning title screen.
- [Rust branding and window fit](stellar-rust-brand.patch) labels the game window Stellar Minecraft and the title footer Rust. The launcher’s fit setting requests a maximized desktop window.
- [Spear movement](spear-movement.patch) removes the Rust client's extra position update immediately before STAB. Java 26.3 sends STAB and leaves movement to the normal client tick. The Java server rejects two position updates before the next client tick end.

From the pinned Horizon source tree, run `git apply --check /path/to/stellar-title-motion.patch /path/to/spear-movement.patch /path/to/stellar-rust-brand.patch`. Once both patches apply, apply them without `--check` and build the client using Horizon's build instructions. Enable the accompanying Stellar Rust Panorama resource pack to use the motion effect.

The title and movement patches passed `cargo check --locked -p horizon-client --lib` against the pinned source. The shader passed Naga validation. On an M4 Mac, test frames showed changes in the lit disk and stars with identical pixels inside the shadow. [Recorded title preview](../../stellar-prism/screenshots/rust-title-motion.webp)

A reproduction using the production position sender emitted two position packets in one tick with the extra spear sync and one with the patch. The patched client also completed 25 spear attacks while moving in an isolated local world. Public multiplayer compatibility remains unverified.

The [Stellar Rust engine preview workflow](../../../.github/workflows/stellar-rust-engine.yml) builds a standalone Apple silicon test client from this revision. [Apple silicon build](https://github.com/sebastianmiletic/stellarlauncher/actions/runs/45507063485)
