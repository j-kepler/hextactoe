#include "text.h"

#include "font_data.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using fontdata::Face;
using fontdata::Glyph;

const Face& faceOf(FontStyle style) { return style == FontStyle::Bold ? fontdata::kBold : fontdata::kRegular; }

int pixelSize(float size) { return std::max(1, int(std::lround(size))); }

int glyphIndex(const Face& f, std::uint32_t code) {
    const Glyph* end = f.glyphs + f.glyphCount;
    const Glyph* g = std::lower_bound(f.glyphs, end, code, [](const Glyph& a, std::uint32_t c) { return a.code < c; });
    if (g != end && g->code == code) return int(g - f.glyphs);
    return code == '?' ? 0 : glyphIndex(f, '?');
}

int kerning(const Face& f, std::uint32_t left, std::uint32_t right) {
    const fontdata::Kern* end = f.kerns + f.kernCount;
    const fontdata::Kern* k = std::lower_bound(f.kerns, end, std::make_pair(left, right),
                                               [](const fontdata::Kern& a, const std::pair<std::uint32_t, std::uint32_t>& b) {
                                                   return std::make_pair(a.left, a.right) < b;
                                               });
    return k != end && k->left == left && k->right == right ? k->value : 0;
}

std::uint32_t nextCode(const std::string& s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    std::uint32_t code = c & (0x3F >> extra);
    while (extra-- > 0 && i < s.size()) code = (code << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    return code;
}

// Walks a string, calling f(glyph index, pen x) for each character.
template <typename F>
float layout(const Face& f, float size, const std::string& s, F&& visit) {
    const float scale = float(pixelSize(size)) / float(f.unitsPerEm);
    float pen = 0;
    std::uint32_t prev = 0;
    for (std::size_t i = 0; i < s.size();) {
        const std::uint32_t code = nextCode(s, i);
        const int g = glyphIndex(f, code);
        if (prev) pen += float(kerning(f, prev, f.glyphs[g].code)) * scale;
        visit(g, pen);
        pen += float(f.glyphs[g].advance) * scale;
        prev = f.glyphs[g].code;
    }
    return pen;
}

// Exact-area coverage rasterizer (the signed-area accumulation method used by
// font-rs): each edge adds its area to the cells it crosses, and a running sum
// along each row gives the coverage of every pixel.
class Rasterizer {
public:
    Rasterizer(int w, int h) : w_(w), h_(h), stride_(w + 2), acc_(std::size_t(stride_) * std::size_t(h), 0.f) {}

    void line(SDL_FPoint p0, SDL_FPoint p1) {
        if (std::fabs(p0.y - p1.y) < 1e-6f) return;
        float dir = 1.f;
        if (p0.y > p1.y) {
            std::swap(p0, p1);
            dir = -1.f;
        }
        const float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
        float x = p0.x;
        if (p0.y < 0.f) x -= p0.y * dxdy;
        const int yEnd = std::min(h_, int(std::ceil(p1.y)));
        for (int y = std::max(0, int(p0.y)); y < yEnd; ++y) {
            const float dy = std::min(float(y + 1), p1.y) - std::max(float(y), p0.y);
            const float xNext = x + dxdy * dy;
            const float d = dy * dir;
            const float xa = std::max(0.f, std::min(x, xNext));
            const float xb = std::max(0.f, std::max(x, xNext));
            const float xaFloor = std::floor(xa);
            const int xai = int(xaFloor);
            const float xbCeil = std::ceil(xb);
            const int xbi = std::min(int(xbCeil), stride_ - 1);
            float* a = &acc_[std::size_t(y) * std::size_t(stride_)];
            if (xbi <= xai + 1) {
                const float xmf = 0.5f * (x + xNext) - xaFloor;
                a[xai] += d - d * xmf;
                a[std::min(xai + 1, stride_ - 1)] += d * xmf;
            } else {
                const float s = 1.f / (xb - xa);
                const float xaf = xa - xaFloor;
                const float a0 = 0.5f * s * (1.f - xaf) * (1.f - xaf);
                const float xbf = xb - xbCeil + 1.f;
                const float am = 0.5f * s * xbf * xbf;
                a[xai] += d * a0;
                if (xbi == xai + 2) {
                    a[xai + 1] += d * (1.f - a0 - am);
                } else {
                    const float a1 = s * (1.5f - xaf);
                    a[xai + 1] += d * (a1 - a0);
                    for (int xi = xai + 2; xi < xbi - 1; ++xi) a[xi] += d * s;
                    const float a2 = a1 + float(xbi - xai - 3) * s;
                    a[xbi - 1] += d * (1.f - a2 - am);
                }
                a[xbi] += d * am;
            }
            x = xNext;
        }
    }

    void quad(SDL_FPoint p0, SDL_FPoint p1, SDL_FPoint p2) {
        const float dd = std::hypot(p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y);
        const int n = std::clamp(int(std::ceil(std::sqrt(dd / (4.f * kTolerance)))), 1, 64);
        SDL_FPoint prev = p0;
        for (int i = 1; i <= n; ++i) {
            const float t = float(i) / float(n);
            const float u = 1.f - t;
            const SDL_FPoint p{u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y};
            line(prev, p);
            prev = p;
        }
    }

    void cubic(SDL_FPoint p0, SDL_FPoint p1, SDL_FPoint p2, SDL_FPoint p3) {
        const float dd = std::max(std::hypot(p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y),
                                  std::hypot(p1.x - 2 * p2.x + p3.x, p1.y - 2 * p2.y + p3.y));
        const int n = std::clamp(int(std::ceil(std::sqrt(0.75f * dd / kTolerance))), 1, 64);
        SDL_FPoint prev = p0;
        for (int i = 1; i <= n; ++i) {
            const float t = float(i) / float(n);
            const float u = 1.f - t;
            const float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, e = t * t * t;
            const SDL_FPoint p{a * p0.x + b * p1.x + c * p2.x + e * p3.x, a * p0.y + b * p1.y + c * p2.y + e * p3.y};
            line(prev, p);
            prev = p;
        }
    }

    // Coverage of pixel (x, y) is written to out[y * pitch + x * 4 + 3].
    void writeAlpha(std::uint8_t* out, int pitch) const {
        for (int y = 0; y < h_; ++y) {
            const float* a = &acc_[std::size_t(y) * std::size_t(stride_)];
            float sum = 0;
            for (int x = 0; x < w_; ++x) {
                sum += a[x];
                // A slight boost: light text on a dark background otherwise looks thin.
                const float c = std::pow(std::min(1.f, std::fabs(sum)), 0.8f);
                out[y * pitch + x * 4 + 3] = std::uint8_t(c * 255.f + 0.5f);
            }
        }
    }

private:
    static constexpr float kTolerance = 0.15f;  // pixels
    int w_, h_, stride_;
    std::vector<float> acc_;
};

}  // namespace

void TextRenderer::shutdown() {
    for (auto& [key, a] : atlases_) SDL_DestroyTexture(a.texture);
    atlases_.clear();
}

float TextRenderer::width(FontStyle style, float size, const std::string& s) const {
    return layout(faceOf(style), size, s, [](int, float) {});
}

float TextRenderer::capHeight(float size) const {
    return float(pixelSize(size)) * float(fontdata::kRegular.capHeight) / float(fontdata::kRegular.unitsPerEm);
}

float TextRenderer::draw(FontStyle style, float size, float x, float baseline, SDL_FColor colour, const std::string& s) {
    Atlas& a = atlas(style, pixelSize(size));
    const float bx = std::round(x);
    const float by = std::round(baseline);
    const float end = layout(faceOf(style), size, s, [&](int g, float pen) {
        const GlyphImage& im = a.glyphs[std::size_t(g)];
        if (im.w == 0) return;
        const float x0 = std::round(bx + pen) + float(im.left);
        const float y0 = by - float(im.top);
        const float x1 = x0 + float(im.w);
        const float y1 = y0 + float(im.h);
        const int base = int(a.vertices.size());
        a.vertices.push_back({{x0, y0}, colour, {im.u0, im.v0}});
        a.vertices.push_back({{x1, y0}, colour, {im.u1, im.v0}});
        a.vertices.push_back({{x1, y1}, colour, {im.u1, im.v1}});
        a.vertices.push_back({{x0, y1}, colour, {im.u0, im.v1}});
        a.indices.insert(a.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    });
    return bx + end;
}

void TextRenderer::flush() {
    for (auto& [key, a] : atlases_) {
        if (!a.indices.empty() && a.texture) {
            SDL_RenderGeometry(renderer_, a.texture, a.vertices.data(), int(a.vertices.size()), a.indices.data(),
                               int(a.indices.size()));
        }
        a.vertices.clear();
        a.indices.clear();
    }
}

TextRenderer::Atlas& TextRenderer::atlas(FontStyle style, int size) {
    const auto key = std::make_pair(int(style), size);
    auto found = atlases_.find(key);
    if (found != atlases_.end()) return found->second;
    if (atlases_.size() > 32) shutdown();  // e.g. after moving between displays many times

    const Face& f = faceOf(style);
    const float scale = float(size) / float(f.unitsPerEm);
    Atlas& a = atlases_[key];
    a.glyphs.resize(std::size_t(f.glyphCount));

    // Bitmap boxes with a pixel of padding, packed into rows.
    const int atlasW = size > 40 ? 1024 : 512;
    std::vector<SDL_Point> origin(std::size_t(f.glyphCount));
    int x = 1, y = 1, rowH = 0;
    for (int i = 0; i < f.glyphCount; ++i) {
        const Glyph& g = f.glyphs[i];
        GlyphImage& im = a.glyphs[std::size_t(i)];
        if (g.pathLength == 0) continue;  // space
        im.left = int(std::floor(float(g.xMin) * scale)) - 1;
        im.top = int(std::ceil(float(g.yMax) * scale)) + 1;
        im.w = int(std::ceil(float(g.xMax) * scale)) + 1 - im.left;
        im.h = im.top - (int(std::floor(float(g.yMin) * scale)) - 1);
        if (x + im.w + 1 > atlasW) {
            x = 1;
            y += rowH + 1;
            rowH = 0;
        }
        origin[std::size_t(i)] = {x, y};
        x += im.w + 1;
        rowH = std::max(rowH, im.h);
    }
    const int atlasH = y + rowH + 1;

    SDL_Surface* surface = SDL_CreateSurface(atlasW, atlasH, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return a;
    auto* pixels = static_cast<std::uint8_t*>(surface->pixels);
    for (int row = 0; row < atlasH; ++row) {
        std::uint8_t* p = pixels + row * surface->pitch;
        for (int col = 0; col < atlasW; ++col) {
            p[col * 4 + 0] = p[col * 4 + 1] = p[col * 4 + 2] = 255;
            p[col * 4 + 3] = 0;
        }
    }

    for (int i = 0; i < f.glyphCount; ++i) {
        const Glyph& g = f.glyphs[i];
        GlyphImage& im = a.glyphs[std::size_t(i)];
        if (im.w == 0) continue;
        Rasterizer r(im.w, im.h);
        auto pt = [&](const std::int16_t* v) {
            return SDL_FPoint{float(v[0]) * scale - float(im.left), float(im.top) - float(v[1]) * scale};
        };
        const std::int16_t* p = f.path + g.pathStart;
        const std::int16_t* end = p + g.pathLength;
        SDL_FPoint start{}, cur{};
        bool open = false;
        while (p < end) {
            const int op = *p++;
            if (op == 0) {
                if (open) r.line(cur, start);
                start = cur = pt(p);
                open = true;
                p += 2;
            } else if (op == 1) {
                const SDL_FPoint q = pt(p);
                r.line(cur, q);
                cur = q;
                p += 2;
            } else if (op == 2) {
                const SDL_FPoint q = pt(p + 2);
                r.quad(cur, pt(p), q);
                cur = q;
                p += 4;
            } else {
                const SDL_FPoint q = pt(p + 4);
                r.cubic(cur, pt(p), pt(p + 2), q);
                cur = q;
                p += 6;
            }
        }
        if (open) r.line(cur, start);

        const SDL_Point o = origin[std::size_t(i)];
        r.writeAlpha(pixels + o.y * surface->pitch + o.x * 4, surface->pitch);
        im.u0 = float(o.x) / float(atlasW);
        im.v0 = float(o.y) / float(atlasH);
        im.u1 = float(o.x + im.w) / float(atlasW);
        im.v1 = float(o.y + im.h) / float(atlasH);
    }

    a.texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (a.texture) {
        SDL_SetTextureBlendMode(a.texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(a.texture, SDL_SCALEMODE_NEAREST);
    }
    return a;
}
