// SPDX-License-Identifier: GPL-3.0-only
package app.stellar.client;

import java.util.Arrays;
import java.util.Locale;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.LevelLoadingScreen;

public final class Performance {
    private static final long[] frames = new long[240];
    private static int next;
    private static int count;
    private static long lastFrame;
    private static long loading;
    private static long lastLoad;

    private Performance() {}

    public static void frame(Minecraft game) {
        long now = System.nanoTime();
        var screen = game.gui.screen();
        if (screen instanceof LevelLoadingScreen) {
            if (loading == 0) {
                loading = now;
                count = 0;
                next = 0;
            }
        } else if (screen == null && game.level != null && game.player != null) {
            if (loading != 0) {
                lastLoad = now - loading;
                loading = 0;
            }
            if (lastFrame != 0) {
                frames[next] = now - lastFrame;
                next = (next + 1) % frames.length;
                count = Math.min(count + 1, frames.length);
            }
            lastFrame = now;
            return;
        } else if (game.level == null) {
            loading = 0;
        }
        lastFrame = 0;
    }

    public static String frames() {
        if (count == 0) return "Frame times · Play a world to collect samples";
        long[] sorted = Arrays.copyOf(frames, count);
        Arrays.sort(sorted);
        return String.format(Locale.ROOT, "Frame time · %.1f ms median · %.1f ms p95 · %d frames",
            sorted[(count - 1) / 2] / 1_000_000.0, sorted[(int) Math.ceil(count * 0.95) - 1] / 1_000_000.0, count);
    }

    public static String world() {
        if (lastLoad == 0) return "World load · No completed load this session";
        return String.format(Locale.ROOT, "World load · %.2f s", lastLoad / 1_000_000_000.0);
    }
}
