// SPDX-License-Identifier: GPL-3.0-only
package app.stellar.client;

import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.TitleScreen;
import org.slf4j.LoggerFactory;

public final class ReplayBridge {
    private static String requested = System.getProperty("stellar.replay", "");

    private ReplayBridge() {}

    public static void openWhenReady(Minecraft game) {
        if (requested.isEmpty() || game.gui.overlay() != null || !(game.gui.screen() instanceof TitleScreen)) return;
        String name = requested;
        requested = "";
        if (!name.endsWith(".zip") || name.contains("/") || name.contains("\\")) return;
        Path file = game.gameDirectory.toPath().resolve("flashback/replays").resolve(name);
        if (!Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS) || !FabricLoader.getInstance().isModLoaded("flashback")) return;
        game.schedule(() -> {
            try {
                Class.forName("com.moulberry.flashback.Flashback").getMethod("openReplayWorld", Path.class).invoke(null, file);
            } catch (ReflectiveOperationException | LinkageError error) {
                LoggerFactory.getLogger("Stellar").warn("Could not open the requested Flashback replay", error);
            }
        });
    }
}
