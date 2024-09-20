#pragma once

#include <SDL3/SDL_gpu.h>

namespace candlewick {

struct Device {

  Device(SDL_GPUShaderFormat format_flags, bool debug_mode = false);
  Device(const Device &) = delete;

  operator SDL_GPUDevice *() { return _device; }
  operator bool() const { return _device; }

  const char *driverName() { return driver; }

  void destroy() {
    SDL_DestroyGPUDevice(_device);
    _device = nullptr;
  }

private:
  SDL_GPUDevice *_device;
  const char *driver;
};

} // namespace candlewick
