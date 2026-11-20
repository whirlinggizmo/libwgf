/* gfx-cubemap's sky: its cubemap `sky` along the direction from the camera, unlit, so a box
 * around the camera, seen from inside (double sided), shows the sky every way it looks,
 * however near the box is. raylib's skybox example, as a custom shader of libwgf's. */
@fs fs
@include_block wgf_surface
layout(binding=0) uniform textureCube sky;
layout(binding=0) uniform sampler sky_smp;

void main() {
    vec3 dir = wgf_world_pos - wgf_camera_position();
    wgf_output(wgf_srgb_to_linear(texture(samplerCube(sky, sky_smp), dir).rgb), 1.0);
}
@end
