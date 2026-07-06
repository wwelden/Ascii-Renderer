/* The single translation unit that compiles stb_image's implementation.
   Kept separate so the vendored code's warnings stay out of our strict
   -Wall -Wextra -Wpedantic build (see the Makefile's per-object override). */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR      /* no radiance/HDR loader; we only need 8-bit images */
#define STBI_NO_LINEAR
#include "stb_image.h"
