/* gfx-cubemap's mirrors: the cubemap `sky` reflected about the surface, toward the camera,
 * times `tint` (linear), more of it at a grazing angle (Schlick's Fresnel from `reflectance`
 * face on), unlit: a chrome or colored glass ball in the sky it stands in. */
@fs fs
@include_block wgf_surface
layout(binding=2) uniform params {
    vec4 tint;        /* linear rgb: what the reflection is seen through */
    float reflectance; /* 0..1: how much is reflected face on; all of it at a grazing angle */
};
layout(binding=0) uniform textureCube sky;
layout(binding=0) uniform sampler sky_smp;

void main() {
    vec3 n = normalize(wgf_normal);
    vec3 v = normalize(wgf_world_pos - wgf_camera_position()); /* from the camera */
    vec3 reflected = wgf_srgb_to_linear(texture(samplerCube(sky, sky_smp), reflect(v, n)).rgb);
    float fresnel = reflectance + (1.0 - reflectance) * pow(1.0 - clamp(dot(-v, n), 0.0, 1.0), 5.0);
    wgf_output(reflected * mix(tint.rgb, vec3(1.0), fresnel), 1.0);
}
@end
