// SPDX-License-Identifier: GPL-3.0-only
package app.stellar.client;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;
import java.util.function.Predicate;

import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.Tooltip;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.packs.PackSelectionScreen;
import net.minecraft.network.chat.Component;

public final class ToolsScreen extends Screen {
    private enum Page { HOME, MODS, SHADERS, SKINS, PERFORMANCE }
    private record SkinEntry(String name, Path texture, boolean slim) {}

    private final Screen parent;
    private Page page = Page.HOME;
    private int offset;
    private int skinModel;
    private SkinEntry selectedSkin;
    private boolean applyingSkin;
    private String status = "";

    public ToolsScreen(Screen parent) {
        super(Component.literal("Stellar"));
        this.parent = parent;
    }

    @Override
    protected void init() {
        int center = width / 2;
        switch (page) {
            case HOME -> {
                int top = Math.max(35, height / 2 - 48);
                control(center - 100, top, 98, "Mods", () -> open(Page.MODS));
                control(center + 2, top, 98, "Shaders", () -> open(Page.SHADERS));
                control(center - 100, top + 24, 98, "Skins", () -> open(Page.SKINS));
                control(center + 2, top + 24, 98, "Resource packs", this::openResourcePacks);
                control(center - 100, top + 48, 98, "Performance", () -> open(Page.PERFORMANCE));
            }
            case PERFORMANCE -> { }
            case MODS -> {
                control(center - 90, 32, 180, "Browse Modrinth mods", () -> minecraft.gui.setScreen(new CatalogScreen(this, false)));
                list(files(gameFolder().resolve("mods"), path -> {
                String file = path.getFileName().toString().toLowerCase(Locale.ROOT);
                return file.endsWith(".jar") || file.endsWith(".jar.disabled");
            }), this::toggleMod, path -> {
                String name = path.getFileName().toString();
                boolean disabled = name.endsWith(".disabled");
                return (disabled ? "Enable  •  " : "Disable  •  ") + name.replaceFirst("\\.jar(?:\\.disabled)?$", "");
            }, "No installed mods", 56);
            }
            case SHADERS -> {
                control(center - 100, 38, 200, "Open Iris shader settings", this::openIris);
                control(center - 100, 62, 200, "Browse Modrinth shaders", () -> minecraft.gui.setScreen(new CatalogScreen(this, true)));
                control(center - 100, 86, 200, "Open shader packs folder", () -> openFolder(gameFolder().resolve("shaderpacks")));
                if (status.isEmpty() && !FabricLoader.getInstance().isModLoaded("iris"))
                    status = "Install compatible Iris to apply shader packs";
            }
            case SKINS -> {
                List<SkinEntry> skins = skins();
                list(skins, skin -> {
                    selectedSkin = skin;
                    status = skin.name();
                    rebuildWidgets();
                }, SkinEntry::name, "Import a PNG in Stellar's skin library", 34);
                String variant = switch (skinModel) {
                    case 1 -> "Classic";
                    case 2 -> "Slim";
                    default -> "Auto";
                };
                var model = control(center - 134, height - 52, 88, "Model: " + variant, () -> {
                    skinModel = (skinModel + 1) % 3;
                    rebuildWidgets();
                });
                model.active = !applyingSkin;
                var apply = control(center - 42, height - 52, 72, "Apply skin", () -> applySkin(selectedSkin));
                apply.active = selectedSkin != null && !applyingSkin;
            }
        }
        control(center - 42, height - 26, 84, "Back", this::onClose);
    }

    private <T> void list(List<T> items, Consumer<T> select, java.util.function.Function<T, String> label, String empty, int top) {
        int left = Math.max(6, width / 2 - 134);
        int listWidth = Math.min(268, width - 12);
        int rows = Math.max(1, (height - top - 60) / 22);
        int lastOffset = Math.max(0, ((items.size() - 1) / rows) * rows);
        offset = Math.min(offset, lastOffset);
        if (items.isEmpty() && status.isEmpty()) status = empty;
        for (int index = offset; index < Math.min(offset + rows, items.size()); index++) {
            T item = items.get(index);
            String name = label.apply(item);
            var button = control(left, top + (index - offset) * 22, listWidth,
                font.plainSubstrByWidth(name, listWidth - 14), () -> select.accept(item));
            button.setTooltip(Tooltip.create(Component.literal(name)));
            button.active = !applyingSkin;
        }
        if (lastOffset > 0) {
            var previous = control(left + listWidth - 82, height - 52, 40, "Prev", () -> {
                offset -= rows;
                rebuildWidgets();
            });
            previous.active = offset > 0;
            var next = control(left + listWidth - 40, height - 52, 40, "Next", () -> {
                offset += rows;
                rebuildWidgets();
            });
            next.active = offset < lastOffset;
        }
    }

    private Button control(int x, int y, int size, String text, Runnable action) {
        return addRenderableWidget(Button.builder(Component.literal(text), button -> action.run())
            .bounds(x, y, size, 20).build());
    }

    @Override
    public void extractBackground(GuiGraphicsExtractor graphics, int mouseX, int mouseY, float delta) {
        graphics.fill(0, 0, width, height, 0xFF161619);
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor graphics, int x, int y, float delta) {
        super.extractRenderState(graphics, x, y, delta);
        String title = switch (page) {
            case HOME -> "STELLAR";
            case MODS -> "MODS";
            case SHADERS -> "SHADERS";
            case SKINS -> "SKINS";
            case PERFORMANCE -> "PERFORMANCE";
        };
        graphics.centeredText(font, title, width / 2, 13, 0xFFF0EFF3);
        if (page == Page.PERFORMANCE) {
            graphics.centeredText(font, Performance.frames(), width / 2, 46, 0xFFF0EFF3);
            graphics.centeredText(font, Performance.world(), width / 2, 64, 0xFFF0EFF3);
            graphics.centeredText(font, "World loading screen to first playable frame", width / 2, 80, 0xFFCBC8D2);
        }
        if (!status.isBlank()) {
            graphics.centeredText(font, font.plainSubstrByWidth(status, width - 12),
                width / 2, height - (page == Page.HOME || page == Page.SHADERS ? 41 : 64), 0xFFCBC8D2);
        }
    }

    @Override
    public void onClose() {
        if (page == Page.HOME) minecraft.gui.setScreen(parent);
        else open(Page.HOME);
    }

    private void open(Page next) {
        page = next;
        offset = 0;
        status = "";
        rebuildWidgets();
    }

    private Path gameFolder() {
        return minecraft.gameDirectory.toPath();
    }

    private void toggleMod(Path path) {
        String name = path.getFileName().toString();
        if (name.equalsIgnoreCase("stellar-client.jar")) {
            status = "Remove Stellar from the launcher to disable this menu";
            rebuildWidgets();
            return;
        }
        String next = name.endsWith(".disabled") ? name.substring(0, name.length() - 9) : name + ".disabled";
        try {
            Files.move(path, path.resolveSibling(next));
            status = "Restart Minecraft to load this change";
        } catch (IOException failure) {
            status = "Could not change this mod file";
        }
        rebuildWidgets();
    }

    private void openIris() {
        if (FabricLoader.getInstance().isModLoaded("iris")) {
            try {
                var type = Class.forName("net.irisshaders.iris.gui.screen.ShaderPackScreen");
                minecraft.gui.setScreen((Screen) type.getConstructor(Screen.class).newInstance(this));
                return;
            } catch (ReflectiveOperationException | LinkageError failure) {
                status = "Open Iris through Video Settings for this Iris version";
            }
        } else {
            status = "Install compatible Iris to apply shader packs";
        }
    }

    private void openResourcePacks() {
        minecraft.gui.setScreen(new PackSelectionScreen(minecraft.getResourcePackRepository(), repository -> {
            minecraft.options.updateResourcePacks(repository);
            minecraft.gui.setScreen(this);
        }, gameFolder().resolve("resourcepacks"), Component.literal("Resource packs")));
    }

    private void openFolder(Path path) {
        try {
            Files.createDirectories(path);
            if (java.awt.Desktop.isDesktopSupported() && java.awt.Desktop.getDesktop().isSupported(java.awt.Desktop.Action.OPEN)) {
                java.awt.Desktop.getDesktop().open(path.toFile());
                return;
            }
        } catch (IOException ignored) {
        }
        status = "Open shaderpacks inside this instance folder";
    }

    private List<SkinEntry> skins() {
        String configured = System.getProperty("stellar.skinLibrary", "");
        if (configured.isBlank()) return List.of();
        Path root = Path.of(configured);
        return java.util.stream.Stream.of(root, root.resolve("Minecraft Defaults"), root.resolve("Downloaded"))
            .flatMap(folder -> files(folder, path ->
                path.getFileName().toString().toLowerCase(Locale.ROOT).endsWith(".png")).stream())
            .map(path -> {
                String name = path.getFileName().toString();
                name = name.substring(0, name.length() - 4);
                boolean defaultSlim = path.getParent().endsWith("Minecraft Defaults")
                    && List.of("Alex", "Efe", "Makena", "Noor").contains(name);
                return new SkinEntry(name, path, defaultSlim);
            })
            .sorted(Comparator.comparing(SkinEntry::name, String.CASE_INSENSITIVE_ORDER))
            .toList();
    }

    private void applySkin(SkinEntry skin) {
        if (applyingSkin) return;
        applyingSkin = true;
        status = "Applying " + skin.name();
        rebuildWidgets();
        String token = minecraft.getUser().getAccessToken();
        boolean useSlim = skinModel == 2 || (skinModel == 0 && skin.slim());
        CompletableFuture.supplyAsync(() -> SkinUpload.apply(token, skin.texture(), useSlim))
            .thenAccept(result -> minecraft.execute(() -> {
                applyingSkin = false;
                status = result;
                if (minecraft.gui.screen() == this) rebuildWidgets();
            }));
    }

    private static List<Path> files(Path directory, Predicate<Path> filter) {
        try (var contents = Files.list(directory)) {
            return contents.filter(path -> Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS))
                .filter(filter)
                .sorted(Comparator.comparing(path -> path.getFileName().toString(), String.CASE_INSENSITIVE_ORDER))
                .limit(400)
                .toList();
        } catch (IOException missing) {
            return List.of();
        }
    }
}
