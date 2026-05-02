#include "sdl_image_decoder.hpp"

#include "stb_image_decoder.hpp"

namespace jvmpoc {

void installSdlImageDecoder() {
    installStbImageDecoder();
}

} // namespace jvmpoc
