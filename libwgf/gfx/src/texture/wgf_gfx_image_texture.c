#include "wgf_image.h"

#include <string.h>

#include "texture/wgf_gfx_image_priv.h"
#include "wgf_log.h"
#include "wgf_texture.h"

/* Images to textures and back, a file of its own so an image that is never made a texture
 * links none of the texture calls, and a texture never read into an image none of this:
 * through the public calls alone, a texture made in code (wgf_texture_create_blank) written
 * with an image's pixels, and an image of a texture's read. */

wgf_image_t wgf_image_create_from_read(wgf_texture_read_t read)
{
    const unsigned char *data = wgf_texture_read_get_data(read);
    unsigned char *pixels;
    wgf_image_t image;
    if (wgf_texture_read_get_status(read) != WGF_TEXTURE_READ_STATUS_DONE || data == NULL) {
        wgf_log_warn("wgf_image_create_from_read: not a read that is DONE");
        return 0;
    }
    image = wgf_gfx_priv_image_add(wgf_texture_read_get_width(read), wgf_texture_read_get_height(read), &pixels,
                                   "wgf_image_create_from_read");
    if (image != 0) memcpy(pixels, data, (size_t)wgf_texture_read_get_size(read));
    return image;
}

wgf_texture_t wgf_texture_create_from_image(wgf_image_t image)
{
    const unsigned char *data = wgf_image_get_data(image);
    wgf_texture_t texture;
    if (data == NULL) {
        wgf_log_error("wgf_texture_create_from_image: not an image that is READY");
        return 0;
    }
    texture = wgf_texture_create_blank(wgf_image_get_width(image), wgf_image_get_height(image));
    if (texture != 0 && !wgf_texture_write(texture, data, wgf_image_get_size(image))) {
        wgf_log_error("wgf_texture_create_from_image: the texture didn't take its pixels");
        wgf_resource_release(texture);
        return 0;
    }
    return texture;
}

bool wgf_texture_write_image(wgf_texture_t texture, wgf_image_t image)
{
    const unsigned char *data = wgf_image_get_data(image);
    if (data == NULL || wgf_image_get_width(image) != wgf_texture_get_width(texture) ||
        wgf_image_get_height(image) != wgf_texture_get_height(texture)) {
        return false;
    }
    return wgf_texture_write(texture, data, wgf_image_get_size(image));
}
