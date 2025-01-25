#pragma once

#include "Renderer.h"
#include <concepts>

namespace candlewick {

/// The Scene concept requires that there exist functions to render the
/// scene. provided a Renderer and Camera, and that there is a function to
/// release the resources of the Scene.
///
/// \sa Renderer
/// \sa Camera
template <typename T>
concept Scene = requires(T t, Renderer &r, const Camera &camera) {
  { t.render(r, camera) } -> std::same_as<void>;
  { t.release() } -> std::same_as<void>;
};

} // namespace candlewick
