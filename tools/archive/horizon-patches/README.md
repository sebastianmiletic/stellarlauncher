# Stellar patches for the Rust engine

These patches target Horizon revision `0a875da91dbb0fdcb7ba8c42c11341aa229af51a`. They change engine code and require a new engine build. Stellar Launcher 1.5.11 still uses Horizon's published engine.

- [Title motion](stellar-title-motion.patch) keeps Stellar's cubemap and shadow fixed, animates light around the accretion disk and lensed arc, and twinkles the stars. The renderer enables the effect only when the active resource stack includes the Stellar panorama motion marker. The same renderer path serves the startup menu and the returning title screen.
- [Spear movement](spear-movement.patch) removes the Rust client's extra position update immediately before STAB. Java 26.3 sends STAB and leaves movement to the normal client tick. The Java server rejects two position updates before the next client tick end.

From the pinned Horizon source tree, run `git apply --check /path/to/stellar-title-motion.patch /path/to/spear-movement.patch`. Once both patches apply, apply them without `--check` and build the client using Horizon's build instructions. Enable the accompanying Stellar Rust Panorama resource pack to use the motion effect.

Both patches passed `cargo check --locked -p horizon-client --lib` against the pinned source. The title shader also passed Naga parsing and validation. The modified engine has yet to be verified in the title screen or on a multiplayer server.

The [Stellar Rust engine preview workflow](../../../.github/workflows/stellar-rust-engine.yml) builds a standalone Apple silicon test client from this revision.
