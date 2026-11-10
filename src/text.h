// Text in Latin Modern Roman (Computer Modern, LaTeX's font). Glyphs are
// rasterized from their outlines (font_data.h) once per pixel size and kept
// in a texture atlas, so text is sharp at any size and needs no font library.
#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

enum class FontStyle { Regular, Bold };

class TextRenderer {
public:
    void init(SDL_Renderer* renderer) { renderer_ = renderer; }
    void shutdown();  // frees the atlases; call before destroying the renderer

    // Sizes are in pixels per em. x is the left edge, baseline the baseline.
    float width(FontStyle style, float size, const std::string& utf8) const;
    float capHeight(float size) const;
    // Queues the text and returns the x where it ends.
    float draw(FontStyle style, float size, float x, float baseline, SDL_FColor colour, const std::string& utf8);
    void flush();  // draws everything queued since the last flush

private:
    struct GlyphImage {
        int left = 0, top = 0, w = 0, h = 0;  // bitmap position relative to the pen on the baseline
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    };
    struct Atlas {
        SDL_Texture* texture = nullptr;
        std::vector<GlyphImage> glyphs;  // same order as the face's glyphs
        std::vector<SDL_Vertex> vertices;
        std::vector<int> indices;
    };

    Atlas& atlas(FontStyle style, int size);

    SDL_Renderer* renderer_ = nullptr;
    std::map<std::pair<int, int>, Atlas> atlases_;  // by (style, pixel size)
};
