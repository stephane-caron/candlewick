#pragma once

#ifndef CANDLEWICK_WITH_FFMPEG_SUPPORT
#error "Including this file requires candlewick to be built with FFmpeg support"
#endif
#include <SDL3/SDL_stdinc.h>
#include <string>
#include <memory>
#include "candlewick/core/Tags.h"

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace candlewick {
namespace media {

  struct VideoRecorderImpl;

  class VideoRecorder {
  private:
    std::unique_ptr<VideoRecorderImpl> impl_;

  public:
    struct Settings {
      int fps = 30;
      // default: 2.5 Mb/s
      long bit_rate = 2500000u;
      int outputWidth;
      int outputHeight;
    };

    /// \brief Constructor which will not open the file or stream.
    explicit VideoRecorder(NoInitT);
    VideoRecorder(VideoRecorder &&);
    VideoRecorder &operator=(VideoRecorder &&);

    bool initialized() const { return impl_ != nullptr; }
    VideoRecorder(Uint32 width, Uint32 height, const std::string &filename,
                  Settings settings);

    VideoRecorder(Uint32 width, Uint32 height, const std::string &filename)
        : VideoRecorder(width, height, filename,
                        Settings{
                            .outputWidth = int(width),
                            .outputHeight = int(height),
                        }) {}

    Uint32 frameCounter() const;
    void writeFrame(const Uint8 *data, size_t payloadSize,
                    AVPixelFormat avPixelFormat);
    ~VideoRecorder();
  };

} // namespace media
} // namespace candlewick
