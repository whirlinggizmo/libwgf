/* stb_image's Radiance .hdr decoder alone, private to this file (STB_IMAGE_STATIC), for
 * environments (wgf_gfx_environment.c): a program that makes none links none of it, and
 * the texture loader's stb_image (texture/wgf_gfx_stb_impl.c) stays without HDR. Vendored
 * code, compiled as it comes. */
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_HDR
#include "stb_image.h"

#include "stage/wgf_gfx_environment_priv.h"

bool wgf_gfx_priv_environment_is_hdr(const unsigned char *bytes, int size)
{
    return stbi_is_hdr_from_memory(bytes, size) != 0;
}

float *wgf_gfx_priv_environment_decode_hdr(const unsigned char *bytes, int size, int *width, int *height)
{
    int components;
    return stbi_loadf_from_memory(bytes, size, width, height, &components, 3);
}

void wgf_gfx_priv_environment_hdr_free(float *rgb)
{
    stbi_image_free(rgb);
}
