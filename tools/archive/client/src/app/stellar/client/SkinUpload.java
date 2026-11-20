// SPDX-License-Identifier: GPL-3.0-only
package app.stellar.client;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.UUID;
import javax.imageio.ImageIO;

final class SkinUpload {
    private SkinUpload() {}

    static String apply(String token, Path skin, boolean slim) {
        if (token == null || token.isBlank() || token.equals("0") || token.equals("-"))
            return "Sign in to Minecraft to apply a profile skin";
        try {
            if (Files.size(skin) > 128 * 1024) return "Skin PNG is too large";
            byte[] texture = Files.readAllBytes(skin);
            var image = ImageIO.read(new ByteArrayInputStream(texture));
            if (image == null || image.getWidth() != 64 || (image.getHeight() != 64 && image.getHeight() != 32))
                return "Choose a 64×64 or 64×32 skin PNG";
            String boundary = "Stellar-" + UUID.randomUUID();
            String prefix = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"variant\"\r\n\r\n"
                + (slim ? "slim" : "classic") + "\r\n--" + boundary
                + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"skin.png\""
                + "\r\nContent-Type: image/png\r\n\r\n";
            var request = HttpRequest.newBuilder(URI.create("https://api.minecraftservices.com/minecraft/profile/skins"))
                .timeout(Duration.ofSeconds(15))
                .header("Authorization", "Bearer " + token)
                .header("Content-Type", "multipart/form-data; boundary=" + boundary)
                .POST(HttpRequest.BodyPublishers.concat(
                    HttpRequest.BodyPublishers.ofString(prefix, StandardCharsets.UTF_8),
                    HttpRequest.BodyPublishers.ofByteArray(texture),
                    HttpRequest.BodyPublishers.ofString("\r\n--" + boundary + "--\r\n")))
                .build();
            int status = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(7)).build()
                .send(request, HttpResponse.BodyHandlers.discarding()).statusCode();
            if (status >= 200 && status < 300) return "Skin applied. Rejoin the world to refresh.";
            if (status == 401 || status == 403) return "Sign in again to apply a Minecraft skin";
            return "Minecraft could not apply this skin (HTTP " + status + ")";
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            return "Skin request cancelled";
        } catch (IOException | IllegalArgumentException failure) {
            return "Could not read the skin or reach Minecraft Services";
        }
    }
}
