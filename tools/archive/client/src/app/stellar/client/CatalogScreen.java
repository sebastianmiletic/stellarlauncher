// SPDX-License-Identifier: GPL-3.0-only
package app.stellar.client;

import com.google.gson.JsonParser;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.SharedConstants;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.components.Tooltip;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;
import net.minecraft.client.gui.screens.ConfirmLinkScreen;

final class CatalogScreen extends Screen {
    private record Project(String slug, String title, String description) {}

    private final Screen parent;
    private final boolean shaders;
    private EditBox search;
    private List<Project> projects = List.of();
    private String query = "";
    private String status = "Search Modrinth";
    private int offset;
    private int requestNumber;
    private boolean opened;

    CatalogScreen(Screen parent, boolean shaders) {
        super(Component.literal(shaders ? "Browse shaders" : "Browse mods"));
        this.parent = parent;
        this.shaders = shaders;
    }

    @Override
    protected void init() {
        int span = Math.min(320, width - 20);
        int left = (width - span) / 2;
        search = addRenderableWidget(new EditBox(font, left, 27, span - 65, 20, Component.literal("Search Modrinth")));
        search.setMaxLength(100);
        search.setValue(query);
        addRenderableWidget(Button.builder(Component.literal("Search"), button -> search())
            .bounds(left + span - 60, 27, 60, 20).build());
        int rows = Math.max(1, (height - 93) / 22);
        offset = Math.max(0, Math.min(offset, projects.size() - 1));
        for (int i = offset; i < Math.min(offset + rows, projects.size()); i++) {
            Project project = projects.get(i);
            var widget = addRenderableWidget(Button.builder(Component.literal(font.plainSubstrByWidth(project.title(), span - 16)),
                button -> ConfirmLinkScreen.confirmLinkNow(this, URI.create("https://modrinth.com/" + (shaders ? "shader/" : "mod/") + project.slug())))
                .bounds(left, 52 + (i - offset) * 22, span, 20).build());
            widget.setTooltip(Tooltip.create(Component.literal(project.description() + "\nOpen project on Modrinth")));
        }
        var previous = addRenderableWidget(Button.builder(Component.literal("Prev"), button -> {
            offset -= rows;
            rebuildWidgets();
        }).bounds(left, height - 26, 48, 20).build());
        previous.active = offset > 0;
        var next = addRenderableWidget(Button.builder(Component.literal("Next"), button -> {
            offset += rows;
            rebuildWidgets();
        }).bounds(left + span - 48, height - 26, 48, 20).build());
        next.active = offset + rows < projects.size();
        addRenderableWidget(Button.builder(Component.literal("Back"), button -> onClose())
            .bounds(width / 2 - 40, height - 26, 80, 20).build());
        if (!opened) {
            opened = true;
            search();
        }
    }

    private void search() {
        query = search.getValue().strip();
        String version = SharedConstants.getCurrentVersion().name();
        if (!version.matches("[a-zA-Z0-9. _-]+")) return;
        String facets = shaders ? "[[\"project_type:shader\"]]"
            : "[[\"project_type:mod\"],[\"categories:fabric\"],[\"versions:" + version + "\"]]";
        URI uri = URI.create("https://api.modrinth.com/v2/search?limit=40&query="
            + URLEncoder.encode(query, StandardCharsets.UTF_8) + "&facets=" + URLEncoder.encode(facets, StandardCharsets.UTF_8));
        int request = ++requestNumber;
        status = searchStatus(true);
        HttpRequest httpRequest = HttpRequest.newBuilder(uri).timeout(Duration.ofSeconds(12))
            .header("User-Agent", "sebastianmiletic/StellarLauncher (Minecraft client)").build();
        Network.client.sendAsync(httpRequest, HttpResponse.BodyHandlers.ofString())
            .thenApply(response -> {
                if (response.statusCode() != 200) throw new IllegalStateException("Modrinth returned " + response.statusCode());
                var hits = JsonParser.parseString(response.body()).getAsJsonObject().getAsJsonArray("hits");
                var results = new ArrayList<Project>();
                for (var item : hits) {
                    var hit = item.getAsJsonObject();
                    String slug = hit.get("slug").getAsString();
                    if (slug.matches("[a-zA-Z0-9_-]+"))
                        results.add(new Project(slug, hit.get("title").getAsString(), hit.get("description").getAsString()));
                }
                return results;
            }).whenComplete((results, error) -> minecraft.execute(() -> {
                if (request != requestNumber || minecraft.gui.screen() != this) return;
                query = search.getValue();
                projects = error == null ? results : List.of();
                status = error != null ? "Modrinth is unavailable · Try searching again"
                    : projects.isEmpty() ? "No matching projects" : searchStatus(false);
                offset = 0;
                rebuildWidgets();
            }));
    }

    private String searchStatus(boolean loading) {
        if (loading) return "Searching Modrinth…";
        return shaders ? "Check Iris and Minecraft support on the project page" : "Fabric · Minecraft " + SharedConstants.getCurrentVersion().name();
    }

    private static final class Network {
        private static final HttpClient client = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(8)).build();
    }

    @Override
    public void extractBackground(GuiGraphicsExtractor graphics, int x, int y, float delta) {
        graphics.fill(0, 0, width, height, 0xFF161619);
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor graphics, int x, int y, float delta) {
        super.extractRenderState(graphics, x, y, delta);
        graphics.centeredText(font, getTitle(), width / 2, 10, 0xFFF0EFF3);
        graphics.centeredText(font, font.plainSubstrByWidth(status, width - 12), width / 2, height - 40, 0xFFCBC8D2);
    }

    @Override
    public void onClose() {
        minecraft.gui.setScreen(parent);
    }
}
