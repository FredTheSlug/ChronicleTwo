#pragma once

#include <functional>
#include <span>

#include "gfx/gfx.hpp"

using mgPortPrimSink = std::function<void(gfx::Primitive, std::span<const gfx::Vertex2D>, const gfx::TextureBinding &,
                                          const gfx::DrawState &)>;
// Replaces mgPortDraw2D as the primitive builder's destination while set; an empty function restores it. For tests.
void mgPortSetPrimSink(mgPortPrimSink sink);
// Registers the host draw that the draw manager replays where retail calls `packet`, replacing any earlier entry for it.
void mgPortSetPacketDraw(const void *packet, std::function<void()> draw);
// Removes the host draw registered for `packet`, if any.
void mgPortClearPacketDraw(const void *packet);
// Runs the host draw registered for `packet`; false when there is none, including a null packet.
bool mgPortPacketDraw(const void *packet);
