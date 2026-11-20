// SPDX-License-Identifier: GPL-3.0-only
package app.stellar.client.mixin;

import app.stellar.client.ToolsScreen;

import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.PauseScreen;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.network.chat.Component;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin({TitleScreen.class, PauseScreen.class})
abstract class MenuMixin extends Screen {
    protected MenuMixin(Component title) {
        super(title);
    }

    @Inject(method = "init", at = @At("TAIL"))
    private void stellar$addTools(CallbackInfo callback) {
        if ((Object) this instanceof PauseScreen && children().isEmpty()) {
            return;
        }
        addRenderableWidget(Button.builder(Component.literal("Stellar"), button ->
                minecraft.gui.setScreen(new ToolsScreen(this)))
            .bounds(Math.max(4, width - 86), 8, 78, 20).build());
    }
}
