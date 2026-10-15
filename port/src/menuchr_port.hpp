#pragma once

// Draws the banner naming the character a quick change (L3, R3) switched to, over the game frame.
void PortDrawQuickChangeBanner();

// Drops a banner still showing, as a mode change and its loading screen begin.
void PortCancelQuickChangeBanner();

// Plays the effect on the character a quick change switched to: a fade from white, with sparkles in dungeons.
void PortStepQuickChangeEffect();
