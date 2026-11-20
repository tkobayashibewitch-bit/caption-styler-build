// Caption Styler v0.1
// One effect on one text layer: up to 3 outlines, drop shadow, background box.
//
// How it works
//   1. Expand the output buffer (FRAME_SETUP) so outlines/shadow/box fit outside the text.
//   2. Exact Euclidean distance transform of the text's alpha
//      -> every pixel knows how far it is from the glyphs.
//   3. Outline i covers distance range [w0+...+w(i-1), w0+...+wi]  (antialiased).
//   4. Background box = rounded rect around the glyph bounds.
//   5. Shadow = shifted + blurred alpha of (box + outlines + text).
//   Order bottom -> top: shadow, box, outer outline ... inner outline, original text.
//
// Pixel format note: AE effect worlds carry straight (un-premultiplied) color.
// We composite internally in premultiplied float and convert back on write.

#include "CaptionStyler.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace {

struct Stroke { bool on; float r, g, b; float width; };

struct Settings {
    Stroke stroke[NUM_STROKES];
    bool  shOn;  float shR, shG, shB, shOpacity, shDistance, shAngleDeg, shSoftness;
    bool  bxOn;  float bxR, bxG, bxB, bxOpacity, bxPadX, bxPadY, bxRound;
    float scale; // downsample factor (1 = full res)
};

inline float C8(A_u_char v) { return v / 255.0f; }

void ReadSettings(PF_InData* in_data, PF_ParamDef* params[], Settings& s)
{
    float sx = (float)in_data->downsample_x.num / (float)in_data->downsample_x.den;
    float sy = (float)in_data->downsample_y.num / (float)in_data->downsample_y.den;
    s.scale = std::min(sx, sy);

    for (int i = 0; i < NUM_STROKES; ++i) {
        const PF_Pixel& c = params[StrokeParam(i, STK_COLOR)]->u.cd.value;
        s.stroke[i].on    = params[StrokeParam(i, STK_ON)]->u.bd.value != 0;
        s.stroke[i].r = C8(c.red); s.stroke[i].g = C8(c.green); s.stroke[i].b = C8(c.blue);
        s.stroke[i].width = (float)params[StrokeParam(i, STK_WIDTH)]->u.fs_d.value * s.scale;
    }
    const PF_Pixel& sc = params[CS_SH_COLOR]->u.cd.value;
    s.shOn       = params[CS_SH_ON]->u.bd.value != 0;
    s.shR = C8(sc.red); s.shG = C8(sc.green); s.shB = C8(sc.blue);
    s.shOpacity  = (float)params[CS_SH_OPACITY]->u.fs_d.value / 100.0f;
    s.shDistance = (float)params[CS_SH_DISTANCE]->u.fs_d.value * s.scale;
    s.shAngleDeg = (float)FIX_2_FLOAT(params[CS_SH_ANGLE]->u.ad.value);
    s.shSoftness = (float)params[CS_SH_SOFTNESS]->u.fs_d.value * s.scale;

    const PF_Pixel& bc = params[CS_BX_COLOR]->u.cd.value;
    s.bxOn      = params[CS_BX_ON]->u.bd.value != 0;
    s.bxR = C8(bc.red); s.bxG = C8(bc.green); s.bxB = C8(bc.blue);
    s.bxOpacity = (float)params[CS_BX_OPACITY]->u.fs_d.value / 100.0f;
    s.bxPadX    = (float)params[CS_BX_PAD_X]->u.fs_d.value * s.scale;
    s.bxPadY    = (float)params[CS_BX_PAD_Y]->u.fs_d.value * s.scale;
    s.bxRound   = (float)params[CS_BX_ROUND]->u.fs_d.value * s.scale;
}

float TotalStrokeWidth(const Settings& s)
{
    float t = 0;
    for (int i = 0; i < NUM_STROKES; ++i) if (s.stroke[i].on) t += std::max(0.0f, s.stroke[i].width);
    return t;
}

int ComputePad(const Settings& s)
{
    float pad = TotalStrokeWidth(s);
    if (s.bxOn) pad += std::max(s.bxPadX, s.bxPadY);
    if (s.shOn) pad += s.shDistance + s.shSoftness * 3.0f;
    return (int)std::ceil(pad) + 2;
}

// ---------- distance transform (Felzenszwalb & Huttenlocher) ----------
void Edt1D(const float* f, int n, float* d, int* v, float* z)
{
    int k = 0; v[0] = 0; z[0] = -1e20f; z[1] = 1e20f;
    auto meet = [&](int q, int r) {
        return ((f[q] + (float)q * q) - (f[r] + (float)r * r)) / (2.0f * q - 2.0f * r);
    };
    for (int q = 1; q < n; ++q) {
        float s = meet(q, v[k]);
        while (s <= z[k]) { --k; s = meet(q, v[k]); }   // z[0] = -1e20 stops at k = 0
        ++k; v[k] = q; z[k] = s; z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q) ++k;
        int r = v[k];
        d[q] = (float)(q - r) * (q - r) + f[r];
    }
}

// alpha in [0,1]; returns distance in px from each pixel to the glyph edge (0 inside)
void DistanceField(const std::vector<float>& alpha, int W, int H, std::vector<float>& dist)
{
    const float INF = 1e20f;
    dist.assign((size_t)W * H, 0.0f);
    for (size_t i = 0; i < dist.size(); ++i) dist[i] = alpha[i] >= 0.5f ? 0.0f : INF;

    int n = std::max(W, H);
    std::vector<float> f(n), d(n), z(n + 1);
    std::vector<int> v(n);

    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) f[y] = dist[(size_t)y * W + x];
        Edt1D(f.data(), H, d.data(), v.data(), z.data());
        for (int y = 0; y < H; ++y) dist[(size_t)y * W + x] = d[y];
    }
    for (int y = 0; y < H; ++y) {
        float* row = &dist[(size_t)y * W];
        for (int x = 0; x < W; ++x) f[x] = row[x];
        Edt1D(f.data(), W, d.data(), v.data(), z.data());
        for (int x = 0; x < W; ++x) row[x] = d[x];
    }
    // sqrt + sub-pixel correction from the edge pixel's own coverage
    for (size_t i = 0; i < dist.size(); ++i) {
        float a = alpha[i];
        float dd = std::sqrt(dist[i]);
        dist[i] = a >= 0.5f ? 0.0f : std::max(0.0f, dd - a);
    }
}

// ---------- blur helpers ----------
void BoxBlur1D(std::vector<float>& buf, int W, int H, int radius, bool horizontal)
{
    if (radius <= 0) return;
    int len = horizontal ? W : H, lines = horizontal ? H : W;
    std::vector<float> tmp(len);
    float norm = 1.0f / (2 * radius + 1);
    for (int l = 0; l < lines; ++l) {
        auto at = [&](int i) -> float& {
            return horizontal ? buf[(size_t)l * W + i] : buf[(size_t)i * W + l];
        };
        float sum = 0;
        for (int i = -radius; i <= radius; ++i) sum += (i >= 0 && i < len) ? at(i) : 0.0f;
        for (int i = 0; i < len; ++i) {
            tmp[i] = sum * norm;
            int add = i + radius + 1, sub = i - radius;
            if (add < len) sum += at(add);
            if (sub >= 0)  sum -= at(sub);
        }
        for (int i = 0; i < len; ++i) at(i) = tmp[i];
    }
}

void GaussLikeBlur(std::vector<float>& buf, int W, int H, float softness)
{
    int r = (int)std::lround(softness);
    for (int pass = 0; pass < 3; ++pass) {   // 3 box passes ~ gaussian
        BoxBlur1D(buf, W, H, r, true);
        BoxBlur1D(buf, W, H, r, false);
    }
}

inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// ---------- pixel traits ----------
template <typename P> struct Traits;
template <> struct Traits<PF_Pixel8>  { typedef A_u_char  T; static float Max() { return 255.0f; } };
template <> struct Traits<PF_Pixel16> { typedef A_u_short T; static float Max() { return (float)PF_MAX_CHAN16; } };

template <typename P>
inline P* PixelAt(PF_EffectWorld* w, int x, int y)
{
    return reinterpret_cast<P*>(reinterpret_cast<char*>(w->data) + (size_t)y * w->rowbytes) + x;
}

template <typename P>
PF_Err RenderT(const Settings& s, PF_EffectWorld* input, PF_EffectWorld* output)
{
    const float MX = Traits<P>::Max();
    const int W = output->width, H = output->height;
    const int inW = input->width, inH = input->height;
    const int ox = std::max(0, (W - inW) / 2), oy = std::max(0, (H - inH) / 2);
    const size_t N = (size_t)W * H;

    // 1. text alpha in output space + bounds
    std::vector<float> alpha(N, 0.0f);
    int minX = W, minY = H, maxX = -1, maxY = -1;
    for (int y = 0; y < inH; ++y) {
        int yy = y + oy; if (yy >= H) break;
        P* row = PixelAt<P>(input, 0, y);
        for (int x = 0; x < inW; ++x) {
            int xx = x + ox; if (xx >= W) break;
            float a = row[x].alpha / MX;
            alpha[(size_t)yy * W + xx] = a;
            if (a > 0.03f) {
                minX = std::min(minX, xx); maxX = std::max(maxX, xx);
                minY = std::min(minY, yy); maxY = std::max(maxY, yy);
            }
        }
    }

    // empty layer -> clear output
    if (maxX < 0) {
        for (int y = 0; y < H; ++y) {
            P* o = PixelAt<P>(output, 0, y);
            for (int x = 0; x < W; ++x) { o[x].alpha = o[x].red = o[x].green = o[x].blue = 0; }
        }
        return PF_Err_NONE;
    }

    // 2. distance field (only needed when some stroke is on)
    const float total = TotalStrokeWidth(s);
    std::vector<float> dist;
    if (total > 0.0f) DistanceField(alpha, W, H, dist);

    // stroke ranges, inner -> outer
    struct Ring { float r1, cr, cg, cb; };
    std::vector<Ring> rings;
    {
        float acc = 0;
        for (int i = 0; i < NUM_STROKES; ++i) {
            if (!s.stroke[i].on || s.stroke[i].width <= 0) continue;
            acc += s.stroke[i].width;
            rings.push_back({ acc, s.stroke[i].r, s.stroke[i].g, s.stroke[i].b });
        }
    }

    // box geometry
    const float cx = (minX + maxX + 1) * 0.5f, cy = (minY + maxY + 1) * 0.5f;
    const float hx = (maxX - minX + 1) * 0.5f + total + s.bxPadX;
    const float hy = (maxY - minY + 1) * 0.5f + total + s.bxPadY;
    const float rad = std::min(s.bxRound, std::min(hx, hy));

    // 3. styled layer (premultiplied float RGBA): box, rings outer->inner, text
    std::vector<float> R(N), G(N), B(N), A(N);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            size_t i = (size_t)y * W + x;
            float r = 0, g = 0, b = 0, a = 0;

            if (s.bxOn && s.bxOpacity > 0) {
                float qx = std::fabs(x + 0.5f - cx) - (hx - rad);
                float qy = std::fabs(y + 0.5f - cy) - (hy - rad);
                float ox2 = std::max(qx, 0.0f), oy2 = std::max(qy, 0.0f);
                float sd = std::sqrt(ox2 * ox2 + oy2 * oy2) + std::min(std::max(qx, qy), 0.0f) - rad;
                float cov = Clamp01(0.5f - sd) * s.bxOpacity;
                r = s.bxR * cov; g = s.bxG * cov; b = s.bxB * cov; a = cov;
            }

            if (!rings.empty()) {
                float d = dist[i];
                for (int k = (int)rings.size() - 1; k >= 0; --k) {
                    float cov = Clamp01(rings[k].r1 - d + 0.5f);
                    if (cov <= 0) continue;
                    r = rings[k].cr * cov + r * (1 - cov);
                    g = rings[k].cg * cov + g * (1 - cov);
                    b = rings[k].cb * cov + b * (1 - cov);
                    a = cov + a * (1 - cov);
                }
            }

            int ix = x - ox, iy = y - oy;
            if (ix >= 0 && iy >= 0 && ix < inW && iy < inH) {
                const P& p = *PixelAt<P>(input, ix, iy);
                float fa = p.alpha / MX;
                r = (p.red / MX) * fa + r * (1 - fa);
                g = (p.green / MX) * fa + g * (1 - fa);
                b = (p.blue / MX) * fa + b * (1 - fa);
                a = fa + a * (1 - fa);
            }
            R[i] = r; G[i] = g; B[i] = b; A[i] = a;
        }
    }

    // 4. shadow under everything
    std::vector<float> SA;
    if (s.shOn && s.shOpacity > 0) {
        // AE convention: 0 deg = up, clockwise
        float th = s.shAngleDeg * 3.14159265f / 180.0f;
        float dx = std::sin(th) * s.shDistance, dy = -std::cos(th) * s.shDistance;
        SA.assign(N, 0.0f);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float sx = x - dx, sy = y - dy;            // bilinear sample
                int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
                float fx = sx - x0, fy = sy - y0, v = 0;
                for (int j = 0; j < 2; ++j) for (int k = 0; k < 2; ++k) {
                    int xx = x0 + k, yy = y0 + j;
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    v += A[(size_t)yy * W + xx] * (k ? fx : 1 - fx) * (j ? fy : 1 - fy);
                }
                SA[(size_t)y * W + x] = v;
            }
        }
        GaussLikeBlur(SA, W, H, s.shSoftness);
    }

    // 5. write out (straight alpha)
    for (int y = 0; y < H; ++y) {
        P* o = PixelAt<P>(output, 0, y);
        for (int x = 0; x < W; ++x) {
            size_t i = (size_t)y * W + x;
            float r = R[i], g = G[i], b = B[i], a = A[i];
            if (!SA.empty()) {
                float sa = SA[i] * s.shOpacity * (1 - a);
                r += s.shR * sa; g += s.shG * sa; b += s.shB * sa; a += sa;
            }
            if (a > 1e-6f) { r /= a; g /= a; b /= a; } else { r = g = b = 0; }
            typedef typename Traits<P>::T T;
            o[x].alpha = (T)(Clamp01(a) * MX + 0.5f);
            o[x].red   = (T)(Clamp01(r) * MX + 0.5f);
            o[x].green = (T)(Clamp01(g) * MX + 0.5f);
            o[x].blue  = (T)(Clamp01(b) * MX + 0.5f);
        }
    }
    return PF_Err_NONE;
}

// ---------- commands ----------
PF_Err About(PF_InData* in_data, PF_OutData* out_data)
{
    PF_SPRINTF(out_data->return_msg, "%s v%d.%d\r%s",
               PLUGIN_NAME, MAJOR_VERSION, MINOR_VERSION, PLUGIN_DESCRIPTION);
    return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_OutData* out_data)
{
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_EXPAND_BUFFER;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    static const char* names[NUM_STROKES] = { "Outline 1 (inner)", "Outline 2", "Outline 3 (outer)" };
    static const A_u_char defCol[NUM_STROKES][3] = { {232, 52, 78}, {255, 255, 255}, {0, 0, 0} };
    static const bool defOn[NUM_STROKES] = { true, true, false };

    for (int i = 0; i < NUM_STROKES; ++i) {
        AEFX_CLR_STRUCT(def);
        PF_ADD_TOPIC(names[i], StrokeDiskID(i, STK_TOPIC));
        AEFX_CLR_STRUCT(def);
        PF_ADD_CHECKBOXX("Enable", defOn[i], 0, StrokeDiskID(i, STK_ON));
        AEFX_CLR_STRUCT(def);
        PF_ADD_COLOR("Color", defCol[i][0], defCol[i][1], defCol[i][2], StrokeDiskID(i, STK_COLOR));
        AEFX_CLR_STRUCT(def);
        PF_ADD_FLOAT_SLIDERX("Width", 0, 400, 0, 60, 10, PF_Precision_TENTHS, 0, 0, StrokeDiskID(i, STK_WIDTH));
        AEFX_CLR_STRUCT(def);
        PF_END_TOPIC(StrokeDiskID(i, STK_END));
    }

    AEFX_CLR_STRUCT(def); PF_ADD_TOPIC("Drop Shadow", DID_SH_TOPIC);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Enable", TRUE, 0, DID_SH_ON);
    AEFX_CLR_STRUCT(def); PF_ADD_COLOR("Color", 0, 0, 0, DID_SH_COLOR);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Opacity", 0, 100, 0, 100, 60, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, DID_SH_OPACITY);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Distance", 0, 500, 0, 50, 8, PF_Precision_TENTHS, 0, 0, DID_SH_DISTANCE);
    AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Direction", 135, DID_SH_ANGLE);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Softness", 0, 200, 0, 40, 6, PF_Precision_TENTHS, 0, 0, DID_SH_SOFTNESS);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(DID_SH_END);

    AEFX_CLR_STRUCT(def); PF_ADD_TOPIC("Background Box", DID_BX_TOPIC);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOXX("Enable", FALSE, 0, DID_BX_ON);
    AEFX_CLR_STRUCT(def); PF_ADD_COLOR("Color", 17, 17, 17, DID_BX_COLOR);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Opacity", 0, 100, 0, 100, 75, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, DID_BX_OPACITY);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Padding X", 0, 1000, 0, 120, 28, PF_Precision_TENTHS, 0, 0, DID_BX_PAD_X);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Padding Y", 0, 1000, 0, 120, 16, PF_Precision_TENTHS, 0, 0, DID_BX_PAD_Y);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Roundness", 0, 1000, 0, 100, 12, PF_Precision_TENTHS, 0, 0, DID_BX_ROUND);
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(DID_BX_END);

    out_data->num_params = CS_NUM_PARAMS;
    return err;
}

PF_Err FrameSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    Settings s; ReadSettings(in_data, params, s);
    int pad = ComputePad(s);
    out_data->width    = in_data->width  + 2 * pad;
    out_data->height   = in_data->height + 2 * pad;
    out_data->origin.h = (A_short)pad;
    out_data->origin.v = (A_short)pad;
    return PF_Err_NONE;
}

PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    Settings s; ReadSettings(in_data, params, s);
    PF_EffectWorld* input = &params[CS_INPUT]->u.ld;
    if (PF_WORLD_IS_DEEP(output)) return RenderT<PF_Pixel16>(s, input, output);
    return RenderT<PF_Pixel8>(s, input, output);
}

} // namespace

extern "C" DllExport
PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
                                SPBasicSuite* inSPBasicSuitePtr, const char* inHostName, const char* inHostVersion)
{
    PF_Err result = PF_Err_INVALID_CALLBACK;
    result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr,
                                     PLUGIN_NAME, PLUGIN_MATCH_NAME, PLUGIN_CATEGORY,
                                     AE_RESERVED_INFO, "EffectMain", "");
    return result;
}

PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                  PF_ParamDef* params[], PF_LayerDef* output, void* extra)
{
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
        case PF_Cmd_ABOUT:        err = About(in_data, out_data); break;
        case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(out_data); break;
        case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data); break;
        case PF_Cmd_FRAME_SETUP:  err = FrameSetup(in_data, out_data, params); break;
        case PF_Cmd_RENDER:       err = Render(in_data, out_data, params, output); break;
        default: break;
        }
    } catch (const std::bad_alloc&) {
        err = PF_Err_OUT_OF_MEMORY;
    } catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return err;
}
