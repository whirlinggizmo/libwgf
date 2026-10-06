/* stb_image, compiled once for gfx: PNG, JPEG, BMP, TGA, and GIF decoding from
 * memory (files are read through core's fs). Vendored code, compiled as it comes. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR /* Radiance .hdr is for 3D environments, which milestone 1 has none of */
#define STBI_NO_LINEAR
#include "stb_image.h"
