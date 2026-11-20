// SPDX-License-Identifier: GPL-3.0-only
package app.stellar.client.mixin;

import app.stellar.client.Performance;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(GameRenderer.class)
abstract class RenderMixin {
    @Inject(method = "render", at = @At("TAIL"))
    private void stellar$sampleFrame(CallbackInfo callback) {
        Performance.frame(Minecraft.getInstance());
    }
}
