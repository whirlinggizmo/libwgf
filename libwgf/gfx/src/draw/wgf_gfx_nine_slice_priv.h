#ifndef WGF_GFX_NINE_SLICE_PRIV_H
#define WGF_GFX_NINE_SLICE_PRIV_H

/* A nine-slice's grid: a box x0..x1, y0..y1 drawn from a texture's region (its texture
 * coordinates u0..u1, v0..v1; its size in texels w by h), its borders left, top, right, and
 * bottom texels in from the region's edges. The corners keep their size (one texel a unit),
 * scaled down together when the box is smaller than its borders; the edges stretch along, the
 * middle both ways. Into four positions and four coordinates each way, quad (i, j) from i to
 * i + 1 across and j to j + 1 down. */
static inline void wgf_gfx_priv_nine_slice(float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                                           float v1, float w, float h, const float border[4], float xs[4], float ys[4],
                                           float us[4], float vs[4])
{
    const float bw = border[0] + border[2], bh = border[1] + border[3];
    const float box_w = x1 - x0 < 0 ? x0 - x1 : x1 - x0, box_h = y1 - y0 < 0 ? y0 - y1 : y1 - y0;
    float k = 1.0f;
    if (bw > box_w && bw > 0.0f) k = box_w / bw;
    if (bh > box_h && bh > 0.0f && box_h / bh < k) k = box_h / bh;
    xs[0] = x0, xs[1] = x0 + border[0] * k, xs[2] = x1 - border[2] * k, xs[3] = x1;
    ys[0] = y0, ys[1] = y0 + border[1] * k, ys[2] = y1 - border[3] * k, ys[3] = y1;
    us[0] = u0, us[3] = u1, vs[0] = v0, vs[3] = v1;
    us[1] = w > 0.0f ? u0 + (u1 - u0) * border[0] / w : u0;
    us[2] = w > 0.0f ? u1 - (u1 - u0) * border[2] / w : u1;
    vs[1] = h > 0.0f ? v0 + (v1 - v0) * border[1] / h : v0;
    vs[2] = h > 0.0f ? v1 - (v1 - v0) * border[3] / h : v1;
}

#endif /* WGF_GFX_NINE_SLICE_PRIV_H */
