// HexTacToe - SDL3 front end: board rendering, input and the HUD.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "ai.h"
#include "game.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr double kMinHexPoints = 7.0;
constexpr double kMaxHexPoints = 90.0;

// Text sizes, in points.
constexpr float kNamePt = 20;
constexpr float kBodyPt = 17;
constexpr float kSmallPt = 15.5f;
constexpr float kBannerPt = 40;

// ---------------------------------------------------------------- colours

constexpr SDL_FColor rgb(int r, int g, int b, float a = 1.f) {
    return {r / 255.f, g / 255.f, b / 255.f, a};
}
SDL_FColor withAlpha(SDL_FColor c, float a) {
    c.a = a;
    return c;
}
SDL_FColor mix(SDL_FColor a, SDL_FColor b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

namespace col {
constexpr SDL_FColor background = rgb(17, 20, 26);
constexpr SDL_FColor cell = rgb(36, 41, 51);
constexpr SDL_FColor cellCentre = rgb(42, 48, 59);
constexpr SDL_FColor blue = rgb(40, 116, 236);
constexpr SDL_FColor blueLight = rgb(122, 176, 255);
constexpr SDL_FColor red = rgb(222, 58, 60);
constexpr SDL_FColor redLight = rgb(255, 138, 124);
constexpr SDL_FColor text = rgb(232, 235, 241);
constexpr SDL_FColor textDim = rgb(140, 149, 168);
constexpr SDL_FColor panel = rgb(9, 11, 15, 0.88f);
constexpr SDL_FColor panelSolid = rgb(9, 11, 15);
constexpr SDL_FColor button = rgb(45, 51, 64);
constexpr SDL_FColor buttonHover = rgb(66, 75, 94);
constexpr SDL_FColor white = rgb(255, 255, 255);
}  // namespace col

SDL_FColor playerColour(Player p) { return p == Player::Blue ? col::blue : col::red; }
SDL_FColor playerLight(Player p) { return p == Player::Blue ? col::blueLight : col::redLight; }

// ---------------------------------------------------------------- drawing

// Collects coloured triangles and submits them in one SDL_RenderGeometry call.
// Shapes get a one-pixel feathered rim, which gives cheap anti-aliasing.
class Batch {
public:
    void clear() {
        verts_.clear();
        idx_.clear();
    }

    void flush(SDL_Renderer* r) {
        if (!idx_.empty()) {
            SDL_RenderGeometry(r, nullptr, verts_.data(), int(verts_.size()), idx_.data(), int(idx_.size()));
        }
        clear();
    }

    // Convex polygon with its points in order. `centre` colours the middle,
    // `edge` the rim, so the fill can carry a radial gradient.
    void convex(const SDL_FPoint* p, int n, SDL_FColor edge, SDL_FColor centre, float feather = 1.2f) {
        if (n < 3) return;
        SDL_FPoint c{0, 0};
        for (int i = 0; i < n; ++i) {
            c.x += p[i].x;
            c.y += p[i].y;
        }
        c.x /= float(n);
        c.y /= float(n);

        normals_.resize(n);
        for (int i = 0; i < n; ++i) {
            const SDL_FPoint a = p[i];
            const SDL_FPoint b = p[(i + 1) % n];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            SDL_FPoint nn = len > 1e-6f ? SDL_FPoint{dy / len, -dx / len} : SDL_FPoint{0, 0};
            const float mx = (a.x + b.x) * 0.5f - c.x;
            const float my = (a.y + b.y) * 0.5f - c.y;
            if (nn.x * mx + nn.y * my < 0) nn = {-nn.x, -nn.y};
            normals_[i] = nn;
        }

        const int base = int(verts_.size());
        add(c, centre);
        const float h = feather * 0.5f;
        const SDL_FColor clear = withAlpha(edge, 0.f);
        for (int i = 0; i < n; ++i) {
            const SDL_FPoint n1 = normals_[(i + n - 1) % n];
            const SDL_FPoint n2 = normals_[i];
            const float d = 1.f + n1.x * n2.x + n1.y * n2.y;
            const SDL_FPoint m = d > 1e-3f ? SDL_FPoint{(n1.x + n2.x) / d, (n1.y + n2.y) / d} : n2;
            add({p[i].x - m.x * h, p[i].y - m.y * h}, edge);   // inner rim
            add({p[i].x + m.x * h, p[i].y + m.y * h}, clear);  // outer rim
        }
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            const int in1 = base + 1 + 2 * i;
            const int out1 = in1 + 1;
            const int in2 = base + 1 + 2 * j;
            const int out2 = in2 + 1;
            idx_.insert(idx_.end(), {base, in1, in2, in1, out1, out2, in1, out2, in2});
        }
    }

    void hexagon(SDL_FPoint c, float radius, SDL_FColor edge, SDL_FColor centre) {
        SDL_FPoint p[6];
        for (int i = 0; i < 6; ++i) {
            const float a = (60.f * float(i) - 30.f) * kPi / 180.f;  // pointy-top
            p[i] = {c.x + radius * std::cos(a), c.y + radius * std::sin(a)};
        }
        convex(p, 6, edge, centre);
    }

    void circle(SDL_FPoint c, float radius, SDL_FColor colour) {
        const int n = std::clamp(int(radius * 0.9f), 12, 48);
        pts_.resize(n);
        for (int i = 0; i < n; ++i) {
            const float a = 2.f * kPi * float(i) / float(n);
            pts_[i] = {c.x + radius * std::cos(a), c.y + radius * std::sin(a)};
        }
        convex(pts_.data(), n, colour, colour);
    }

    // A thick line with round ends.
    void capsule(SDL_FPoint a, SDL_FPoint b, float radius, SDL_FColor colour) {
        const float ang = std::atan2(b.y - a.y, b.x - a.x);
        const int seg = std::clamp(int(radius * 0.6f), 6, 20);
        pts_.clear();
        for (int i = 0; i <= seg; ++i) {
            const float t = ang - kPi / 2 + kPi * float(i) / float(seg);
            pts_.push_back({b.x + radius * std::cos(t), b.y + radius * std::sin(t)});
        }
        for (int i = 0; i <= seg; ++i) {
            const float t = ang + kPi / 2 + kPi * float(i) / float(seg);
            pts_.push_back({a.x + radius * std::cos(t), a.y + radius * std::sin(t)});
        }
        convex(pts_.data(), int(pts_.size()), colour, colour);
    }

    void roundedRect(const SDL_FRect& r, float radius, SDL_FColor colour) {
        radius = std::min({radius, r.w * 0.5f, r.h * 0.5f});
        const SDL_FPoint corners[4] = {{r.x + r.w - radius, r.y + r.h - radius},
                                       {r.x + radius, r.y + r.h - radius},
                                       {r.x + radius, r.y + radius},
                                       {r.x + r.w - radius, r.y + radius}};
        constexpr int seg = 8;
        pts_.clear();
        for (int k = 0; k < 4; ++k) {
            for (int i = 0; i <= seg; ++i) {
                const float t = kPi / 2 * float(k) + kPi / 2 * float(i) / float(seg);
                pts_.push_back({corners[k].x + radius * std::cos(t), corners[k].y + radius * std::sin(t)});
            }
        }
        convex(pts_.data(), int(pts_.size()), colour, colour);
    }

private:
    void add(SDL_FPoint p, SDL_FColor c) { verts_.push_back(SDL_Vertex{p, c, {0, 0}}); }

    std::vector<SDL_Vertex> verts_;
    std::vector<int> idx_;
    std::vector<SDL_FPoint> normals_;
    std::vector<SDL_FPoint> pts_;
};

bool inside(const SDL_FRect& r, SDL_FPoint p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h;
}

// The app icon: a small patch of board with a blue and a red line.
SDL_Surface* createIcon(int size) {
    SDL_Surface* surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surface);
    if (!r) {
        SDL_DestroySurface(surface);
        return nullptr;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);

    const float s = float(size);
    const float inset = s * 0.09f;
    Batch b;
    b.roundedRect({inset, inset, s - 2 * inset, s - 2 * inset}, s * 0.2f, rgb(26, 30, 39));
    const float radius = s * 0.078f;
    for (int q = -2; q <= 2; ++q) {
        for (int rr = -2; rr <= 2; ++rr) {
            const Hex h{q, rr};
            if (hexDistance(h, Hex{0, 0}) > 2) continue;
            const Vec2 w = hexToWorld(h);
            const SDL_FPoint c{s * 0.5f + float(w.x) * radius, s * 0.5f + float(w.y) * radius};
            SDL_FColor edge = col::cell;
            SDL_FColor centre = col::cellCentre;
            if (rr == 0) {
                edge = col::blue;
                centre = mix(col::blue, col::blueLight, 0.5f);
            } else if (q + rr == 0 || (q == 1 && rr == -2)) {
                edge = col::red;
                centre = mix(col::red, col::redLight, 0.5f);
            }
            b.hexagon(c, radius * 0.9f, edge, centre);
        }
    }
    b.flush(r);
    SDL_FlushRenderer(r);
    SDL_DestroyRenderer(r);
    return surface;
}

// ---------------------------------------------------------------- the app

enum class Mode { TwoPlayers, ComputerRed, ComputerBlue };

enum class Action { NewGame, Undo, Mode };

struct Button {
    SDL_FRect rect{};
    std::string label;
    Action action = Action::NewGame;
};

struct Camera {
    Vec2 centre;               // world point shown at the middle of the window
    double hexPoints = 30.0;   // hex corner radius in window points
};

class App {
public:
    int run();

private:
    void handleEvent(const SDL_Event& e);
    void handleKey(const SDL_KeyboardEvent& k);
    void click(SDL_FPoint p);
    void update(double dt);
    void updateComputer();
    void render();
    void drawBoard();
    void drawHud();
    void layoutButtons();

    // view helpers (all screen coordinates are in pixels)
    void updateMetrics();
    double zoom() const { return cam_.hexPoints * pxPerPoint_; }
    float ui(float points) const { return points * pxPerPoint_; }
    SDL_FPoint toPixels(float x, float y) const { return {x * pxPerPoint_, y * pxPerPoint_}; }
    SDL_FPoint worldToScreen(Vec2 w) const;
    Vec2 screenToWorld(SDL_FPoint p) const;
    void zoomAt(SDL_FPoint p, double factor);
    void pan(float dxPixels, float dyPixels);
    void centreView();
    bool overUi(SDL_FPoint p) const;

    // game actions
    bool computerPlays(Player p) const;
    bool humanToMove() const { return !game_.isOver() && !computerPlays(game_.toMove()); }
    void newGame();
    void undo();
    void cycleMode();
    void placeAt(Hex h);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool running_ = true;
    Batch batch_;
    TextRenderer text_;

    Game game_;
    Mode mode_ = Mode::TwoPlayers;
    std::vector<Hex> aiQueue_;
    Uint64 aiNextMs_ = 0;
    std::uint32_t aiSeed_ = 0;

    Camera cam_;
    float pxPerPoint_ = 1.f;
    int pw_ = 1;
    int ph_ = 1;

    SDL_FPoint mouse_{-1e6f, -1e6f};
    bool mouseInside_ = false;
    bool leftDown_ = false;
    bool dragging_ = false;
    bool panning_ = false;
    SDL_FPoint pressPos_{};

    bool showHelp_ = true;
    std::vector<Button> buttons_;
    std::vector<SDL_FRect> uiRects_;  // HUD areas that swallow clicks
};

int App::run() {
    SDL_SetAppMetadata("HexTacToe", "1.0.0", "io.github.j-kepler.hextactoe");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer("HexTacToe", 1200, 800, flags, &window_, &renderer_)) {
        std::fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window_, 760, 520);
    SDL_SetRenderVSync(renderer_, 1);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    text_.init(renderer_);
    if (SDL_Surface* icon = createIcon(256)) {
        SDL_SetWindowIcon(window_, icon);
        SDL_DestroySurface(icon);
    }
    aiSeed_ = std::uint32_t(SDL_GetTicksNS() ^ (SDL_GetPerformanceCounter() << 7));

    updateMetrics();
    centreView();

    Uint64 last = SDL_GetTicks();
    while (running_) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool keyPanning = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_UP] ||
                                keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_D] ||
                                keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_S];
        const bool animating = game_.isOver() || computerPlays(game_.toMove()) || keyPanning;
        SDL_Event e;
        // Sleep until something happens when nothing on screen is moving.
        if (!animating && SDL_WaitEventTimeout(&e, 250)) handleEvent(e);
        while (SDL_PollEvent(&e)) handleEvent(e);

        const Uint64 now = SDL_GetTicks();
        update(double(now - last) / 1000.0);
        last = now;
        render();
    }

    text_.shutdown();
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return 0;
}

// ------------------------------------------------------------- view helpers

void App::updateMetrics() {
    int ww = 1;
    int wh = 1;
    SDL_GetWindowSize(window_, &ww, &wh);
    SDL_GetRenderOutputSize(renderer_, &pw_, &ph_);
    pxPerPoint_ = ww > 0 ? float(pw_) / float(ww) : 1.f;
    if (pxPerPoint_ <= 0.f) pxPerPoint_ = 1.f;
}

SDL_FPoint App::worldToScreen(Vec2 w) const {
    const double z = zoom();
    return {float((w.x - cam_.centre.x) * z + pw_ * 0.5), float((w.y - cam_.centre.y) * z + ph_ * 0.5)};
}

Vec2 App::screenToWorld(SDL_FPoint p) const {
    const double z = zoom();
    return {(p.x - pw_ * 0.5) / z + cam_.centre.x, (p.y - ph_ * 0.5) / z + cam_.centre.y};
}

void App::zoomAt(SDL_FPoint p, double factor) {
    const Vec2 before = screenToWorld(p);
    cam_.hexPoints = std::clamp(cam_.hexPoints * factor, kMinHexPoints, kMaxHexPoints);
    const Vec2 after = screenToWorld(p);
    cam_.centre.x += before.x - after.x;
    cam_.centre.y += before.y - after.y;
}

void App::pan(float dx, float dy) {
    cam_.centre.x -= dx / zoom();
    cam_.centre.y -= dy / zoom();
}

// Centre on the tiles, zooming out if they do not fit.
void App::centreView() {
    if (game_.tilesPlaced() == 0) {
        cam_.centre = {0, 0};
        return;
    }
    double minX = 1e300;
    double minY = 1e300;
    double maxX = -1e300;
    double maxY = -1e300;
    for (Hex h : game_.history()) {
        const Vec2 w = hexToWorld(h);
        minX = std::min(minX, w.x);
        maxX = std::max(maxX, w.x);
        minY = std::min(minY, w.y);
        maxY = std::max(maxY, w.y);
    }
    cam_.centre = {(minX + maxX) * 0.5, (minY + maxY) * 0.5};
    const double wPts = pw_ / pxPerPoint_ - 120.0;
    const double hPts = ph_ / pxPerPoint_ - 260.0;
    const double fit = std::min(wPts / (maxX - minX + 4.0), hPts / (maxY - minY + 4.0));
    if (fit < cam_.hexPoints) cam_.hexPoints = std::max(kMinHexPoints, fit);
}

bool App::overUi(SDL_FPoint p) const {
    for (const SDL_FRect& r : uiRects_) {
        if (inside(r, p)) return true;
    }
    return false;
}

// ------------------------------------------------------------- game actions

bool App::computerPlays(Player p) const {
    return (mode_ == Mode::ComputerRed && p == Player::Red) || (mode_ == Mode::ComputerBlue && p == Player::Blue);
}

void App::newGame() {
    game_.reset();
    aiQueue_.clear();
    aiNextMs_ = SDL_GetTicks() + 400;
    cam_.centre = {0, 0};
}

// Takes back the last tile. Against the computer it also takes back the
// computer's tiles, so it is your move again.
void App::undo() {
    aiQueue_.clear();
    if (!game_.undo()) return;
    if (mode_ != Mode::TwoPlayers) {
        while (game_.tilesPlaced() > 0 && computerPlays(game_.toMove())) game_.undo();
    }
}

void App::cycleMode() {
    switch (mode_) {
        case Mode::TwoPlayers: mode_ = Mode::ComputerRed; break;
        case Mode::ComputerRed: mode_ = Mode::ComputerBlue; break;
        case Mode::ComputerBlue: mode_ = Mode::TwoPlayers; break;
    }
    aiQueue_.clear();
    aiNextMs_ = SDL_GetTicks() + 400;
}

void App::placeAt(Hex h) {
    if (!humanToMove()) return;
    if (game_.place(h) && computerPlays(game_.toMove())) aiNextMs_ = SDL_GetTicks() + 450;
}

void App::updateComputer() {
    if (game_.isOver() || !computerPlays(game_.toMove())) {
        aiQueue_.clear();
        return;
    }
    const Uint64 now = SDL_GetTicks();
    if (now < aiNextMs_) return;
    if (aiQueue_.empty()) {
        aiQueue_ = planTurn(game_, aiSeed_++);
        if (aiQueue_.empty()) return;
    }
    const Hex h = aiQueue_.front();
    aiQueue_.erase(aiQueue_.begin());
    if (!game_.place(h)) aiQueue_.clear();  // stale plan: think again next frame
    aiNextMs_ = now + 320;
}

// ------------------------------------------------------------------ input

void App::handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            updateMetrics();
            break;

        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            mouseInside_ = false;
            break;

        case SDL_EVENT_MOUSE_MOTION: {
            const SDL_FPoint p = toPixels(e.motion.x, e.motion.y);
            if (leftDown_ && !dragging_ && std::hypot(p.x - pressPos_.x, p.y - pressPos_.y) > ui(6)) {
                dragging_ = true;
            }
            if (dragging_ || panning_) pan(p.x - mouse_.x, p.y - mouse_.y);
            mouse_ = p;
            mouseInside_ = true;
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const SDL_FPoint p = toPixels(e.button.x, e.button.y);
            mouse_ = p;
            if (e.button.button == SDL_BUTTON_LEFT) {
                leftDown_ = true;
                dragging_ = false;
                pressPos_ = p;
            } else {
                panning_ = true;  // right or middle button drags the board
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const SDL_FPoint p = toPixels(e.button.x, e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (leftDown_ && !dragging_) click(p);
                leftDown_ = false;
                dragging_ = false;
            } else {
                panning_ = false;
            }
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            float dy = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
            dy = std::clamp(dy, -3.f, 3.f);
            zoomAt(toPixels(e.wheel.mouse_x, e.wheel.mouse_y), std::pow(1.12, double(dy)));
            break;
        }

#if SDL_VERSION_ATLEAST(3, 4, 0)
        case SDL_EVENT_PINCH_UPDATE:
            zoomAt(mouseInside_ ? mouse_ : SDL_FPoint{pw_ * 0.5f, ph_ * 0.5f}, double(e.pinch.scale));
            break;
#endif

        case SDL_EVENT_KEY_DOWN:
            handleKey(e.key);
            break;

        default:
            break;
    }
}

void App::handleKey(const SDL_KeyboardEvent& k) {
    const bool command = (k.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    // Only undo and zoom repeat while a key is held down.
    const bool repeats = k.key == SDLK_U || k.key == SDLK_BACKSPACE || k.key == SDLK_Z ||
                         k.key == SDLK_EQUALS || k.key == SDLK_PLUS || k.key == SDLK_KP_PLUS ||
                         k.key == SDLK_MINUS || k.key == SDLK_KP_MINUS;
    if (k.repeat && !repeats) return;
    switch (k.key) {
        case SDLK_N:
            if (!command) newGame();
            break;
        case SDLK_U:
        case SDLK_BACKSPACE:
            undo();
            break;
        case SDLK_Z:
            if (command) undo();
            break;
        case SDLK_M:
            cycleMode();
            break;
        case SDLK_C:
        case SDLK_HOME:
            centreView();
            break;
        case SDLK_H:
            showHelp_ = !showHelp_;
            break;
        case SDLK_F:
        case SDLK_F11: {
            const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(window_, !full);
            break;
        }
        case SDLK_ESCAPE:
            if (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) SDL_SetWindowFullscreen(window_, false);
            break;
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
            zoomAt({pw_ * 0.5f, ph_ * 0.5f}, 1.25);
            break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            zoomAt({pw_ * 0.5f, ph_ * 0.5f}, 0.8);
            break;
        default:
            break;
    }
}

void App::click(SDL_FPoint p) {
    for (const Button& b : buttons_) {
        if (!inside(b.rect, p)) continue;
        switch (b.action) {
            case Action::NewGame: newGame(); break;
            case Action::Undo: undo(); break;
            case Action::Mode: cycleMode(); break;
        }
        return;
    }
    if (overUi(p)) return;
    placeAt(worldToHex(screenToWorld(p)));
}

void App::update(double dt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float dx = 0;
    float dy = 0;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) dx += 1;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) dx -= 1;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) dy += 1;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) dy -= 1;
    const bool modifier = (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if ((dx != 0 || dy != 0) && !modifier) {
        const float speed = ui(700) * float(std::min(dt, 0.05));
        pan(dx * speed, dy * speed);
    }
    updateComputer();
}

// ---------------------------------------------------------------- render

void App::render() {
    updateMetrics();
    SDL_SetRenderDrawColorFloat(renderer_, col::background.r, col::background.g, col::background.b, 1.f);
    SDL_RenderClear(renderer_);

    drawBoard();
    drawHud();
    SDL_RenderPresent(renderer_);
}

void App::drawBoard() {
    const double z = zoom();
    const float gap = std::max(1.f * pxPerPoint_, float(z) * 0.075f);
    const float radius = float(z) - gap;

    const Vec2 tl = screenToWorld({0, 0});
    const Vec2 br = screenToWorld({float(pw_), float(ph_)});
    const int rMin = int(std::floor(tl.y / 1.5)) - 1;
    const int rMax = int(std::ceil(br.y / 1.5)) + 1;

    // Hover preview of the next tile.
    Hex hover{};
    bool showHover = mouseInside_ && humanToMove() && !dragging_ && !panning_ && !overUi(mouse_);
    if (showHover) {
        hover = worldToHex(screenToWorld(mouse_));
        showHover = game_.isEmpty(hover);
    }
    const Player mover = game_.toMove();

    for (int r = rMin; r <= rMax; ++r) {
        const int qMin = int(std::floor(tl.x / kSqrt3 - r * 0.5)) - 1;
        const int qMax = int(std::ceil(br.x / kSqrt3 - r * 0.5)) + 1;
        for (int q = qMin; q <= qMax; ++q) {
            const Hex h{q, r};
            const SDL_FPoint c = worldToScreen(hexToWorld(h));
            const Player p = game_.at(h);
            if (p != Player::None) {
                batch_.hexagon(c, radius, playerColour(p), mix(playerColour(p), playerLight(p), 0.45f));
            } else if (showHover && h == hover) {
                const SDL_FColor base = mix(col::cell, playerColour(mover), 0.45f);
                batch_.hexagon(c, radius, base, mix(base, playerLight(mover), 0.3f));
            } else {
                batch_.hexagon(c, radius, col::cell, col::cellCentre);
            }
        }
    }

    // Dots on the tiles of the last two turns, so you can see what just happened.
    const int turn = game_.isOver() ? Game::turnOfTile(game_.tilesPlaced() - 1) : game_.turnNumber();
    for (int t = turn - 1; t <= turn; ++t) {
        for (Hex h : game_.tilesOfTurn(t)) {
            const SDL_FPoint c = worldToScreen(hexToWorld(h));
            batch_.circle(c, std::max(2.f * pxPerPoint_, float(z) * 0.17f), withAlpha(col::white, 0.85f));
        }
    }

    // The winning line, gently pulsing.
    if (game_.isOver() && !game_.winningLine().empty()) {
        const SDL_FPoint a = worldToScreen(hexToWorld(game_.winningLine().front()));
        const SDL_FPoint b = worldToScreen(hexToWorld(game_.winningLine().back()));
        const float pulse = 0.5f + 0.5f * std::sin(float(SDL_GetTicks()) * 0.005f);
        batch_.capsule(a, b, float(z) * 0.42f, withAlpha(col::white, 0.12f + 0.12f * pulse));
        batch_.capsule(a, b, float(z) * 0.16f, withAlpha(col::white, 0.75f + 0.25f * pulse));
    }
    batch_.flush(renderer_);
}

void App::layoutButtons() {
    const float size = ui(kBodyPt);
    const char* modeLabel = mode_ == Mode::TwoPlayers    ? "Opponent: human"
                            : mode_ == Mode::ComputerRed ? "Computer plays Red"
                                                         : "Computer plays Blue";
    buttons_ = {{{}, "New game", Action::NewGame}, {{}, "Undo", Action::Undo}, {{}, modeLabel, Action::Mode}};

    float x = float(pw_) - ui(16);
    for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
        const float w = std::round(text_.width(FontStyle::Regular, size, it->label) + 2 * ui(14));
        x -= w;
        it->rect = {x, ui(16), w, ui(36)};
        x -= ui(10);
    }
}

void App::drawHud() {
    uiRects_.clear();
    const bool over = game_.isOver();

    // ---- players (top left): a row each, with the one to move highlighted
    struct Row {
        Player player;
        std::string name, tag, count;
        bool active;
    };
    std::vector<Row> rows;
    for (Player p : {Player::Blue, Player::Red}) {
        const int n = int(std::count_if(game_.history().begin(), game_.history().end(),
                                        [&](Hex h) { return game_.at(h) == p; }));
        rows.push_back({p, std::string(playerName(p)) + (over && game_.winner() == p ? " wins" : ""),
                        computerPlays(p) ? "computer" : "", std::to_string(n) + (n == 1 ? " tile" : " tiles"),
                        over ? game_.winner() == p : game_.toMove() == p});
    }

    const float nameSize = ui(kNamePt);
    const float countSize = ui(kBodyPt);
    const float tagSize = ui(kSmallPt);
    const float pad = ui(8);
    const float rowPadX = ui(14);
    const float rowH = ui(44);
    const float rowGap = ui(4);
    const float iconR = ui(10);
    const float pipR = ui(6.5f);
    const float pipStep = ui(16);
    const float nameX = rowPadX + iconR * 2 + ui(10);

    // Width for the widest content, measured in bold so it stays put as the turn changes.
    float leftW = 0;
    float countW = 0;
    for (const Row& row : rows) {
        float w = nameX + text_.width(FontStyle::Bold, nameSize, row.name);
        if (!row.tag.empty()) w += ui(8) + text_.width(FontStyle::Regular, tagSize, row.tag);
        leftW = std::max(leftW, w);
        countW = std::max(countW, text_.width(FontStyle::Regular, countSize, row.count));
    }
    const float rowW = std::round(leftW + ui(18) + 2 * pipStep + ui(8) + countW + rowPadX);
    const SDL_FRect panel{ui(16), ui(16), rowW + 2 * pad, 2 * rowH + rowGap + 2 * pad};
    batch_.roundedRect(panel, ui(12), col::panel);
    uiRects_.push_back(panel);

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        const SDL_FRect r{panel.x + pad, panel.y + pad + float(i) * (rowH + rowGap), rowW, rowH};
        const SDL_FColor c = playerColour(row.player);
        const SDL_FColor light = playerLight(row.player);
        if (row.active) {
            const float border = ui(1.5f);
            batch_.roundedRect(r, ui(8), c);
            batch_.roundedRect({r.x + border, r.y + border, r.w - 2 * border, r.h - 2 * border}, ui(8) - border,
                               mix(col::panelSolid, c, 0.24f));
        }
        const float cy = r.y + rowH * 0.5f;
        batch_.hexagon({r.x + rowPadX + iconR, cy}, iconR, c, mix(c, light, 0.45f));

        const float baseline = cy + text_.capHeight(nameSize) * 0.5f;
        const SDL_FColor ink = row.active ? col::text : col::textDim;
        const float nameEnd = text_.draw(row.active ? FontStyle::Bold : FontStyle::Regular, nameSize, r.x + nameX,
                                         baseline, ink, row.name);
        if (!row.tag.empty()) text_.draw(FontStyle::Regular, tagSize, nameEnd + ui(8), baseline, col::textDim, row.tag);
        const float countX = r.x + r.w - rowPadX - text_.width(FontStyle::Regular, countSize, row.count);
        text_.draw(FontStyle::Regular, countSize, countX, baseline, ink, row.count);

        // Pips for this turn's tiles, lit while they are still to be coloured.
        if (row.active && !over) {
            const int total = game_.tilesPerTurn(game_.turnNumber());
            const int left = game_.tilesLeftThisTurn();
            for (int k = 0; k < total; ++k) {
                const SDL_FPoint pc{countX - ui(18) - pipR - float(total - 1 - k) * pipStep, cy};
                const bool lit = k >= total - left;
                batch_.hexagon(pc, pipR, lit ? c : col::button, lit ? light : col::button);
            }
        }
    }

    // ---- buttons (top right)
    layoutButtons();
    const float buttonSize = ui(kBodyPt);
    for (const Button& b : buttons_) {
        const bool hot = mouseInside_ && inside(b.rect, mouse_);
        batch_.roundedRect(b.rect, ui(8), hot ? col::buttonHover : col::button);
        const float w = text_.width(FontStyle::Regular, buttonSize, b.label);
        text_.draw(FontStyle::Regular, buttonSize, b.rect.x + (b.rect.w - w) * 0.5f,
                   b.rect.y + b.rect.h * 0.5f + text_.capHeight(buttonSize) * 0.5f, col::text, b.label);
        uiRects_.push_back(b.rect);
    }

    // ---- help (bottom left): keys in bold
    float bottom = float(ph_) - ui(16);
    if (showHelp_) {
        using Line = std::vector<std::pair<bool, std::string>>;  // (bold, text) runs
        const std::vector<Line> lines = {
            {{false, "Click a tile to colour it. Six in a row wins."}},
            {{false, "Drag to move the board; scroll or pinch to zoom."}},
            {{true, "N"}, {false, " new game    "}, {true, "U"}, {false, " undo    "}, {true, "M"},
             {false, " opponent    "}, {true, "C"}, {false, " centre    "}, {true, "F"}, {false, " full screen    "},
             {true, "H"}, {false, " hide help"}},
        };
        const float size = ui(kSmallPt);
        const float lineH = std::round(size * 1.45f);
        auto style = [](bool bold) { return bold ? FontStyle::Bold : FontStyle::Regular; };
        float w = 0;
        for (const Line& line : lines) {
            float lw = 0;
            for (const auto& run : line) lw += text_.width(style(run.first), size, run.second);
            w = std::max(w, lw);
        }
        const float padX = ui(14);
        const float padY = ui(12);
        const float cap = text_.capHeight(size);
        const float h = 2 * padY + cap + lineH * float(lines.size() - 1) + size * 0.25f;
        const SDL_FRect box{ui(16), bottom - h, std::round(w + 2 * padX), h};
        batch_.roundedRect(box, ui(8), col::panel);
        uiRects_.push_back(box);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            float x = box.x + padX;
            const float baseline = box.y + padY + cap + lineH * float(i);
            for (const auto& run : lines[i]) {
                x = text_.draw(style(run.first), size, x, baseline, run.first ? col::text : col::textDim, run.second);
            }
        }
        bottom = box.y - ui(12);
    }

    // ---- winner banner (bottom centre)
    if (over) {
        const float headSize = ui(kBannerPt);
        const float subSize = ui(kBodyPt);
        const std::string headline = std::string(playerName(game_.winner())) + " wins!";
        const std::string sub = "Press N or click New game to play again";
        const float headW = text_.width(FontStyle::Bold, headSize, headline);
        const float subW = text_.width(FontStyle::Regular, subSize, sub);
        const float padX = ui(28);
        const float padY = ui(20);
        const float gap = ui(14);
        const float headCap = text_.capHeight(headSize);
        const float subCap = text_.capHeight(subSize);
        const float w = std::round(std::max(headW, subW) + 2 * padX);
        const float h = 2 * padY + headCap + gap + subCap + subSize * 0.25f;
        const SDL_FRect box{std::round((float(pw_) - w) * 0.5f), bottom - h, w, h};
        batch_.roundedRect(box, ui(12), col::panel);
        uiRects_.push_back(box);
        const float headBase = box.y + padY + headCap;
        text_.draw(FontStyle::Bold, headSize, box.x + (w - headW) * 0.5f, headBase, playerLight(game_.winner()),
                   headline);
        text_.draw(FontStyle::Regular, subSize, box.x + (w - subW) * 0.5f, headBase + gap + subCap, col::textDim, sub);
    }

    batch_.flush(renderer_);
    text_.flush();
}

}  // namespace

int main(int, char**) {
    App app;
    return app.run();
}
