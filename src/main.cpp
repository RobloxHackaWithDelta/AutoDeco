// Auto Deco - Art Agent (single-file Geode v5 mod).
// A draggable circle button opens the Art Agent panel in the level editor.
// Select blocks, describe what to draw, preview, then Keep it.
// First run shows an "AI picker"; Settings lets you change AI and color.
// Drawing currently uses a built-in generator (no AI key needed). The
// generator lives in namespace deco and does not touch Geode, so a real AI
// service can be plugged in later.

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <vector>
#include <string>
#include <functional>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <algorithm>

using namespace geode::prelude;

// ===========================================================================
// PURE LOGIC: built-in generator (no Geode types in here)
// ===========================================================================
namespace deco {

struct RGB { unsigned char r, g, b; };
// center, size, rotation (deg, clockwise), palette index
struct Prim { float x, y, w, h, rot; int ci; };
struct Art {
    std::vector<Prim> prims;
    std::vector<RGB> palette;
    RGB bg{ 24, 24, 32 };
    std::string theme;
};

enum class Theme { Bricks, Stars, Fire, Water, Grass, Rays, Neon, Ice, Generic };

static const size_t kMaxPrims = 6000;
static const size_t kMaxColors = 24;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (float)(next() & 0xFFFFFFu) / 16777216.f; }
    float range(float a, float b) { return a + (b - a) * f(); }
    int pick(int n) { return n <= 1 ? 0 : (int)(next() % (uint32_t)n); }
};

static RGB rgb(int r, int g, int b) {
    return RGB{ (unsigned char)r, (unsigned char)g, (unsigned char)b };
}

static RGB hsv(float h, float s, float v) {
    h = h - std::floor(h);
    float c = v * s;
    float hp = h * 6.f;
    float x = c * (1.f - std::fabs(std::fmod(hp, 2.f) - 1.f));
    float r1 = 0.f, g1 = 0.f, b1 = 0.f;
    switch ((int)hp) {
        case 0: r1 = c; g1 = x; break;
        case 1: r1 = x; g1 = c; break;
        case 2: g1 = c; b1 = x; break;
        case 3: g1 = x; b1 = c; break;
        case 4: r1 = x; b1 = c; break;
        default: r1 = c; b1 = x; break;
    }
    float m = v - c;
    return rgb((int)((r1 + m) * 255.f), (int)((g1 + m) * 255.f), (int)((b1 + m) * 255.f));
}

static RGB mix(RGB a, RGB b, float t) {
    return rgb((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t), (int)(a.b + (b.b - a.b) * t));
}

static uint32_t hashStr(std::string const& str) {
    uint32_t h = 2166136261u;
    for (unsigned char ch : str) { h ^= ch; h *= 16777619u; }
    return h;
}

static std::vector<std::string> words(std::string const& s) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char ch : s) {
        if (std::isalnum(ch)) {
            cur.push_back((char)std::tolower(ch));
        }
        else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// true if any word starts with the given prefix
static bool kw(std::vector<std::string> const& ws, char const* prefix) {
    std::string p(prefix);
    for (auto const& w : ws) {
        if (w.size() >= p.size() && w.compare(0, p.size(), p) == 0) return true;
    }
    return false;
}

static Theme detect(std::vector<std::string> const& w, bool& moss, bool& society) {
    moss = kw(w, "moss");
    society = kw(w, "society");
    if (kw(w, "angel") || kw(w, "holy") || kw(w, "heaven") || kw(w, "gold") || kw(w, "sunbeam"))
        return Theme::Rays;
    if (kw(w, "thinking") || kw(w, "star") || kw(w, "space") || kw(w, "galax") ||
        kw(w, "night") || kw(w, "cosmic"))
        return Theme::Stars;
    if (society || moss || kw(w, "brick") || kw(w, "stone") || kw(w, "wall") ||
        kw(w, "castle") || kw(w, "rock") || kw(w, "ruin"))
        return Theme::Bricks;
    if (kw(w, "fire") || kw(w, "lava") || kw(w, "flame") || kw(w, "hell") || kw(w, "burn"))
        return Theme::Fire;
    if (kw(w, "water") || kw(w, "ocean") || kw(w, "sea") || kw(w, "wave") ||
        kw(w, "rain") || kw(w, "river"))
        return Theme::Water;
    if (kw(w, "grass") || kw(w, "forest") || kw(w, "tree") || kw(w, "nature") ||
        kw(w, "leaf") || kw(w, "jungle") || kw(w, "garden"))
        return Theme::Grass;
    if (kw(w, "neon") || kw(w, "cyber") || kw(w, "city") || kw(w, "grid") ||
        kw(w, "tron") || kw(w, "synth"))
        return Theme::Neon;
    if (kw(w, "ice") || kw(w, "snow") || kw(w, "frozen") || kw(w, "crystal") || kw(w, "winter"))
        return Theme::Ice;
    return Theme::Generic;
}

static const float kDeg = 57.29578f;
static const float kRad = 0.0174533f;

// Drawing context: collects primitives, clipped to the region.
struct Ctx {
    Art* a = nullptr;
    Rng* r = nullptr;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, W = 0, H = 0;
    float d = 1.f;   // detail step (bigger = coarser, fewer pieces)
    float sd = 1.f;  // sqrt of d
    bool moss = false, society = false;

    bool full() const { return a->prims.size() > kMaxPrims; }

    // adds n palette colors blending p -> q, returns index of the first
    int ramp(RGB p, RGB q, int n) {
        int first = (int)a->palette.size();
        for (int i = 0; i < n; ++i) {
            float t = n > 1 ? (float)i / (float)(n - 1) : 0.f;
            if (a->palette.size() < kMaxColors) a->palette.push_back(mix(p, q, t));
        }
        return first;
    }
    int col(RGB c) { return ramp(c, c, 1); }

    void add(float x, float y, float w, float h, float rot, int ci) {
        if (full()) return;
        if (std::fabs(rot) < 0.01f) {
            float l = std::max(x - w * 0.5f, x0), rr = std::min(x + w * 0.5f, x1);
            float b = std::max(y - h * 0.5f, y0), t = std::min(y + h * 0.5f, y1);
            if (rr - l < 1.f || t - b < 1.f) return;
            a->prims.push_back(Prim{ (l + rr) * 0.5f, (b + t) * 0.5f, rr - l, t - b, 0.f, ci });
            return;
        }
        if (w < 1.f || h < 1.f) return;
        x = std::min(std::max(x, x0), x1);
        y = std::min(std::max(y, y0), y1);
        a->prims.push_back(Prim{ x, y, w, h, rot, ci });
    }
    void rect(float l, float b, float rr, float t, int ci) {
        add((l + rr) * 0.5f, (b + t) * 0.5f, rr - l, t - b, 0.f, ci);
    }
    void line(float ax, float ay, float bx, float by, float th, int ci) {
        float dx = bx - ax, dy = by - ay;
        float L = std::sqrt(dx * dx + dy * dy);
        if (L < 1.f) return;
        add((ax + bx) * 0.5f, (ay + by) * 0.5f, L, th, -std::atan2(dy, dx) * kDeg, ci);
    }
    // filled circle made of horizontal slices. gradN > 1: color runs down
    // through gradN palette entries starting at ci. slits: sunset style gaps.
    void disc(float cx, float cy, float rad, int ci, int gradN = 1, bool slits = false) {
        if (rad < 1.f) return;
        float sh = std::max(1.8f, 3.2f * d);
        if (rad * 2.f / sh > 48.f) sh = rad * 2.f / 48.f;
        int n = std::max(1, (int)std::ceil(rad * 2.f / sh));
        sh = rad * 2.f / (float)n;
        for (int k = 0; k < n; ++k) {
            float yy = cy + rad - sh * ((float)k + 0.5f);
            float dy = yy - cy;
            float half = std::sqrt(std::max(0.f, rad * rad - dy * dy));
            float frac = (float)k / (float)std::max(1, n - 1);
            if (slits && frac > 0.5f && (k % 3) == 2) continue;
            int cc = ci;
            if (gradN > 1) cc = ci + std::min(gradN - 1, (int)(frac * (float)gradN));
            add(cx, yy, half * 2.f, sh + 0.6f, 0.f, cc);
        }
    }
    // vertical gradient backdrop, palette entries c0..c0+n-1 from top to bottom
    void sky(int c0, int n) {
        float sh = H / (float)n;
        for (int i = 0; i < n; ++i) {
            rect(x0, y1 - sh * (float)(i + 1) - 0.5f, x1, y1 - sh * (float)i, c0 + i);
        }
    }
};

// ---------------------------------------------------------------- bricks
static void buildBricks(Ctx& c) {
    Rng& r = *c.r;
    int mort, sh0;
    if (c.society) {
        c.a->theme = "Society (bricks)";
        c.a->bg = rgb(22, 7, 9);
        mort = c.col(rgb(22, 7, 9));
        sh0 = c.ramp(rgb(86, 14, 22), rgb(212, 64, 68), 5);
    }
    else {
        c.a->theme = c.moss ? "Mossy bricks" : "Bricks";
        c.a->bg = rgb(34, 34, 32);
        mort = c.col(rgb(34, 34, 32));
        sh0 = c.ramp(rgb(76, 74, 70), rgb(176, 174, 164), 5);
    }
    int ms0 = c.ramp(rgb(30, 84, 36), rgb(128, 196, 72), 3);

    c.rect(c.x0, c.y0, c.x1, c.y1, mort);
    float bw = std::max(10.f, 38.f * c.d), bh = std::max(6.f, 19.f * c.d);
    float g = std::max(0.6f, c.d);
    float cx = (c.x0 + c.x1) * 0.5f, cy = (c.y0 + c.y1) * 0.5f;
    int row = 0;
    for (float y = c.y0; y < c.y1 && !c.full(); y += bh, ++row) {
        float off = (row % 2) ? bw * 0.5f : 0.f;
        for (float x = c.x0 - off; x < c.x1 && !c.full(); x += bw) {
            float l = std::max(x, c.x0) + g, rr = std::min(x + bw, c.x1) - g;
            float b = y + g, t = std::min(y + bh, c.y1) - g;
            if (rr - l < 3.f || t - b < 3.f) continue;
            float dn = std::max(std::fabs(((l + rr) * 0.5f - cx) / (c.W * 0.5f)),
                                std::fabs(((b + t) * 0.5f - cy) / (c.H * 0.5f)));
            int s = 1 + r.pick(3);
            if (dn > 0.72f) s -= 1;
            if (dn > 0.92f) s -= 1;
            if (r.f() < 0.08f) s += 1;
            s = std::min(4, std::max(0, s));
            c.rect(l, b, rr, t, sh0 + s);
            if (t - b > 5.f) {
                float hh = std::max(1.2f, (t - b) * 0.24f);
                c.rect(l, t - hh, rr, t, sh0 + std::min(4, s + 1));
                c.rect(l, b, rr, b + hh, sh0 + std::max(0, s - 1));
            }
        }
    }
    // pillars
    if (c.W >= 140.f && !c.full()) {
        int n = 2 + (int)(c.W / 300.f);
        float pw = std::max(14.f, c.W * 0.05f);
        float cap = std::max(5.f, pw * 0.4f);
        for (int i = 0; i < n; ++i) {
            float px = c.x0 + c.W * ((float)i + 0.5f) / (float)n;
            c.rect(px - pw * 0.5f, c.y0, px + pw * 0.5f, c.y1, sh0);
            c.rect(px - pw * 0.5f, c.y0, px - pw * 0.2f, c.y1, sh0 + 3);
            c.rect(px + pw * 0.3f, c.y0, px + pw * 0.5f, c.y1, mort);
            c.rect(px - pw * 0.65f, c.y1 - cap, px + pw * 0.65f, c.y1, sh0 + 3);
            c.rect(px - pw * 0.65f, c.y0, px + pw * 0.65f, c.y0 + cap, sh0 + 3);
        }
    }
    if (c.society) {
        // dark frame top and bottom
        c.rect(c.x0, c.y1 - c.H * 0.07f, c.x1, c.y1, mort);
        c.rect(c.x0, c.y0, c.x1, c.y0 + c.H * 0.07f, mort);
        c.rect(c.x0, c.y1 - c.H * 0.07f - 2.f, c.x1, c.y1 - c.H * 0.07f, sh0 + 1);
        c.rect(c.x0, c.y0 + c.H * 0.07f, c.x1, c.y0 + c.H * 0.07f + 2.f, sh0 + 1);
    }
    if (c.moss) {
        float step = std::max(5.f, 9.f * c.d);
        for (float x = c.x0; x < c.x1 && !c.full(); x += step) {
            if (r.f() < 0.72f) {
                float len = c.H * r.range(0.05f, 0.24f);
                float w = step * 0.9f;
                c.rect(x, c.y1 - len, x + w, c.y1, ms0 + r.pick(2));
                c.rect(x + w * 0.15f, c.y1 - len * 0.7f, x + w * 0.4f, c.y1, ms0 + 2);
            }
            if (r.f() < 0.5f) {
                float h = c.H * r.range(0.02f, 0.09f);
                c.rect(x, c.y0, x + step * 0.9f, c.y0 + h, ms0 + r.pick(3));
            }
        }
    }
}

// ----------------------------------------------------------------- stars
static void buildStars(Ctx& c) {
    Rng& r = *c.r;
    c.a->theme = "Space";
    c.a->bg = rgb(5, 6, 28);
    int sk = c.ramp(rgb(5, 6, 28), rgb(54, 28, 98), 7);
    int sc[4];
    sc[0] = c.col(rgb(255, 255, 255));
    sc[1] = c.col(rgb(160, 200, 255));
    sc[2] = c.col(rgb(205, 160, 255));
    sc[3] = c.col(rgb(120, 236, 255));
    int gl0 = c.col(rgb(44, 52, 118));
    int gl1 = c.col(rgb(78, 42, 128));
    int nb = c.ramp(rgb(74, 32, 124), rgb(196, 74, 176), 3);
    int pl = c.ramp(rgb(34, 44, 104), rgb(160, 180, 240), 3);
    int ringc = c.col(rgb(196, 206, 255));
    int lnc = c.col(rgb(76, 86, 160));
    float mn = std::min(c.W, c.H);

    c.sky(sk, 7);

    // nebula
    int blobs = 1 + r.pick(2);
    for (int i = 0; i < blobs; ++i) {
        float nx = c.x0 + c.W * r.range(0.15f, 0.85f), ny = c.y0 + c.H * r.range(0.15f, 0.7f);
        float R = mn * r.range(0.22f, 0.34f);
        c.disc(nx, ny, R, nb);
        c.disc(nx + R * 0.05f, ny + R * 0.04f, R * 0.68f, nb + 1);
        c.disc(nx + R * 0.1f, ny + R * 0.08f, R * 0.38f, nb + 2);
    }

    // stars
    float cell = std::max(10.f, 22.f * c.d);
    int count = (int)std::min(c.W * c.H / (cell * cell), (float)kMaxPrims) + 4;
    for (int i = 0; i < count && !c.full(); ++i) {
        float x = r.range(c.x0, c.x1), y = r.range(c.y0, c.y1);
        int ci = sc[r.pick(4)];
        float roll = r.f();
        if (roll < 0.68f) {
            float s = r.range(2.2f, 4.f) * c.sd;
            c.add(x, y, s, s, 45.f, ci);
        }
        else if (roll < 0.94f) {
            float s = r.range(4.f, 6.5f) * c.sd;
            c.add(x, y, s * 2.4f, s * 2.4f, 45.f, r.pick(2) ? gl0 : gl1);
            c.add(x, y, s, s, 45.f, ci);
        }
        else {
            float s = r.range(4.5f, 7.f) * c.sd;
            c.add(x, y, s * 5.f, s * 0.5f, 0.f, ci);
            c.add(x, y, s * 0.5f, s * 5.f, 0.f, ci);
            c.add(x, y, s * 1.4f, s * 1.4f, 45.f, sc[0]);
        }
    }

    // constellation
    {
        int pts = 5 + r.pick(2);
        float px = c.x0 + c.W * r.range(0.08f, 0.5f), py = c.y0 + c.H * r.range(0.3f, 0.9f);
        for (int i = 0; i < pts; ++i) {
            float nx = std::min(std::max(px + c.W * r.range(0.06f, 0.15f), c.x0 + 4.f), c.x1 - 4.f);
            float ny = std::min(std::max(py + c.H * r.range(-0.14f, 0.14f), c.y0 + 4.f), c.y1 - 4.f);
            if (i > 0) c.line(px, py, nx, ny, std::max(1.2f, 1.4f * c.sd), lnc);
            c.add(nx, ny, 3.6f * c.sd, 3.6f * c.sd, 45.f, sc[1]);
            px = nx;
            py = ny;
        }
    }

    // ringed planet
    {
        float pr = mn * 0.15f;
        float px = c.x0 + c.W * r.range(0.62f, 0.86f), py = c.y0 + c.H * r.range(0.5f, 0.72f);
        c.disc(px, py, pr, pl);
        c.disc(px - pr * 0.1f, py + pr * 0.1f, pr * 0.86f, pl + 1);
        c.disc(px - pr * 0.3f, py + pr * 0.28f, pr * 0.5f, pl + 2);
        c.add(px, py, pr * 3.4f, std::max(2.f, pr * 0.13f), -16.f, ringc);
    }
}

// ------------------------------------------------- heavenly rays (angel)
static void buildRays(Ctx& c) {
    Rng& r = *c.r;
    c.a->theme = "Heavenly rays";
    c.a->bg = rgb(255, 248, 222);
    int sk = c.ramp(rgb(255, 248, 222), rgb(122, 82, 30), 6);
    int ry = c.ramp(rgb(255, 254, 240), rgb(255, 200, 96), 3);
    int hl = c.ramp(rgb(255, 212, 84), rgb(255, 246, 196), 2);
    int fe = c.ramp(rgb(255, 255, 255), rgb(226, 190, 112), 3);
    c.sky(sk, 6);

    float cx = (c.x0 + c.x1) * 0.5f;
    float hr = std::min(c.W, c.H) * 0.12f;
    float hy = c.y1 - c.H * 0.2f;

    // rays
    int n = std::min(std::max(8, (int)(26.f / c.d)), 120);
    for (int i = 0; i < n && !c.full(); ++i) {
        float al = r.range(-80.f, 80.f) * kRad;
        float sn = std::sin(al), cs = std::cos(al);
        float L = 1e9f;
        if (cs > 0.02f) L = std::min(L, (hy - c.y0) / cs);
        if (sn > 0.02f) L = std::min(L, (c.x1 - cx) / sn);
        else if (sn < -0.02f) L = std::min(L, (cx - c.x0) / -sn);
        L *= r.range(0.55f, 1.f);
        if (L < 4.f || L > 1e8f) continue;
        float th = r.range(2.5f, 8.f) * std::max(0.7f, c.d);
        float phi = std::atan2(-cs, sn);
        c.add(cx + sn * L * 0.5f, hy - cs * L * 0.5f, L, th, -phi * kDeg, ry + r.pick(3));
    }

    // glow + halo
    c.disc(cx, hy, hr * 1.7f, ry);
    c.disc(cx, hy, hr, hl);
    c.disc(cx, hy, hr * 0.8f, hl + 1);

    // wings (back row first)
    float WR = std::min(c.W, c.H) * 0.36f;
    float py = hy - hr * 1.6f;
    for (int row = 2; row >= 0; --row) {
        for (int k = 0; k < 6; ++k) {
            float a = 10.f + (float)k * 15.f;
            float len = WR * (1.f - 0.1f * (float)k) * (1.f - 0.2f * (float)row);
            float rad = a * kRad;
            for (int side = -1; side <= 1; side += 2) {
                float pvx = cx + (float)side * hr * 0.9f;
                float ux = (float)side * std::cos(rad), uy = -std::sin(rad);
                c.add(pvx + ux * len * 0.5f, py + uy * len * 0.5f, len, len * 0.1f,
                      side > 0 ? a : -a, fe + row);
            }
        }
    }

    // sparkles
    int sp = std::min((int)(c.W * c.H / std::max(300.f, 600.f * c.d)), 200) + 4;
    for (int i = 0; i < sp && !c.full(); ++i) {
        float s = r.range(2.f, 4.5f) * c.sd;
        c.add(r.range(c.x0, c.x1), r.range(c.y0, c.y1), s, s, 45.f, r.pick(2) ? ry : hl + 1);
    }
}

// ------------------------------------------------------------------ fire
static void buildFire(Ctx& c) {
    Rng& r = *c.r;
    c.a->theme = "Fire";
    c.a->bg = rgb(8, 3, 4);
    int bg = c.ramp(rgb(8, 3, 4), rgb(72, 14, 10), 5);
    int fl = c.ramp(rgb(122, 18, 12), rgb(255, 246, 172), 5);
    c.sky(bg, 5);
    c.rect(c.x0, c.y0, c.x1, c.y0 + c.H * 0.07f, fl + 1);

    float colW = std::max(10.f, 26.f * c.d);
    float ph = r.range(0.f, 6.28f);
    for (float x = c.x0 + colW * 0.5f; x < c.x1 && !c.full(); x += colW * 0.8f) {
        float h0 = c.H * (0.3f + 0.6f * (0.5f + 0.5f * std::sin(x * 0.045f + ph)) * r.range(0.7f, 1.f));
        for (int k = 0; k < 4 && !c.full(); ++k) {
            float hk = h0 * (1.f - 0.2f * (float)k);
            float wk = colW * (1.15f - 0.24f * (float)k);
            float y = c.y0 + wk * 0.3f;
            while (y < c.y0 + hk && !c.full()) {
                float t = (y - c.y0) / hk;
                float s = wk * (1.f - 0.8f * t) * 0.75f;
                if (s < 1.2f) break;
                int ci = fl + k;
                if (k == 3 && t < 0.35f) ci = fl + 4;
                c.add(x + r.range(-colW * 0.12f, colW * 0.12f), y, s, s, 45.f, ci);
                y += std::max(1.5f, s * 0.8f);
            }
        }
    }
    float cell = std::max(12.f, 30.f * c.d);
    int emb = (int)std::min(c.W * c.H / (cell * cell) * 0.5f, 400.f) + 3;
    for (int i = 0; i < emb && !c.full(); ++i) {
        float y = c.y0 + c.H * std::pow(r.f(), 0.6f);
        float s = r.range(2.f, 4.f) * c.sd;
        c.add(r.range(c.x0, c.x1), y, s, s, 45.f, fl + 2 + r.pick(2));
    }
}

// ----------------------------------------------------------------- water
static void buildWater(Ctx& c) {
    Rng& r = *c.r;
    c.a->theme = "Water";
    c.a->bg = rgb(6, 24, 70);
    int dp = c.ramp(rgb(150, 226, 250), rgb(6, 24, 70), 8);
    int cr = c.col(rgb(196, 242, 255));
    int md = c.col(rgb(84, 172, 226));
    int rayc = c.col(rgb(112, 192, 236));
    int bub = c.col(rgb(214, 246, 255));
    c.sky(dp, 8);

    int ns = 3 + r.pick(4);
    for (int i = 0; i < ns; ++i) {
        float w = std::max(5.f, c.W * r.range(0.02f, 0.06f));
        c.add(c.x0 + c.W * r.range(0.f, 1.f), c.y0 + c.H * 0.5f, w, c.H * 1.05f, r.range(8.f, 24.f), rayc);
    }

    float phase = r.range(0.f, 6.28f);
    float segW = std::max(10.f, 26.f * c.d);
    for (int k = 0; k < 3 && !c.full(); ++k) {
        float yb = c.y1 - c.H * (0.07f + 0.075f * (float)k);
        float A = c.H * 0.025f;
        float th = std::max(2.5f, 6.f * c.d) * (1.f - 0.15f * (float)k);
        for (float x = c.x0; x < c.x1 && !c.full(); x += segW) {
            float p = x * 0.055f + (float)k * 1.9f + phase;
            float yy = yb + A * std::sin(p);
            float slope = A * 0.055f * std::cos(p);
            float w = segW + 1.f;
            c.add(x + w * 0.5f, yy, w, th, -std::atan(slope) * kDeg, k == 0 ? cr : md);
        }
    }
    for (int col = 0; col < 3; ++col) {
        float bx = c.x0 + c.W * r.range(0.1f, 0.9f);
        for (int j = 0; j < 9 && !c.full(); ++j) {
            float s = (2.f + (float)j * 0.4f) * c.sd;
            c.add(bx + r.range(-4.f, 4.f), c.y0 + c.H * 0.08f + (float)j * c.H * 0.09f, s, s, 45.f, bub);
        }
    }
}

// ----------------------------------------------------------------- grass
static float hillY(float x, float base, float amp, float f1, float p1) {
    return base + amp * (0.6f * std::sin(x * f1 + p1) + 0.4f * std::sin(x * f1 * 2.3f + p1 * 1.7f));
}

static void buildGrass(Ctx& c) {
    Rng& r = *c.r;
    c.a->theme = "Grass";
    c.a->bg = rgb(104, 181, 244);
    int sk = c.ramp(rgb(104, 181, 244), rgb(220, 240, 255), 5);
    int sunc0 = c.col(rgb(255, 244, 168));
    int sunc1 = c.col(rgb(255, 214, 70));
    int hf = c.col(rgb(122, 170, 150));
    int hm = c.col(rgb(78, 150, 70));
    int hn = c.col(rgb(48, 118, 50));
    int bl = c.ramp(rgb(30, 92, 36), rgb(130, 204, 70), 3);
    int trunk = c.col(rgb(92, 60, 40));
    int cn = c.ramp(rgb(36, 110, 48), rgb(110, 180, 64), 2);
    int fl0 = c.col(rgb(255, 226, 84));
    int fl1 = c.col(rgb(255, 120, 170));
    c.sky(sk, 5);

    float mn = std::min(c.W, c.H);
    float sx = c.x0 + c.W * 0.78f, sy = c.y0 + c.H * 0.76f;
    c.disc(sx, sy, mn * 0.15f, sunc0);
    c.disc(sx, sy, mn * 0.1f, sunc1);

    float cs = std::max(4.f, 8.f * c.d);
    float p1 = r.range(0.f, 6.28f), p2 = r.range(0.f, 6.28f), p3 = r.range(0.f, 6.28f);
    float rim = std::max(1.5f, 3.f * c.d);
    for (float x = c.x0; x < c.x1 && !c.full(); x += cs) {
        float xm = x + cs * 0.5f;
        c.rect(x, c.y0, x + cs + 0.5f, c.y0 + hillY(xm, c.H * 0.40f, c.H * 0.1f, 0.02f, p1), hf);
    }
    for (float x = c.x0; x < c.x1 && !c.full(); x += cs) {
        float xm = x + cs * 0.5f;
        c.rect(x, c.y0, x + cs + 0.5f, c.y0 + hillY(xm, c.H * 0.28f, c.H * 0.08f, 0.035f, p2), hm);
    }
    for (float x = c.x0; x < c.x1 && !c.full(); x += cs) {
        float xm = x + cs * 0.5f;
        float top = c.y0 + hillY(xm, c.H * 0.17f, c.H * 0.06f, 0.05f, p3);
        c.rect(x, c.y0, x + cs + 0.5f, top, hn);
        c.rect(x, top - rim, x + cs + 0.5f, top, bl + 2);
    }
    // trees
    int nt = 2 + r.pick(3);
    for (int i = 0; i < nt && !c.full(); ++i) {
        float tx = c.x0 + c.W * r.range(0.08f, 0.7f);
        float base = c.y0 + hillY(tx, c.H * 0.28f, c.H * 0.08f, 0.035f, p2);
        float th = c.H * r.range(0.1f, 0.16f), tw = std::max(3.f, c.H * 0.03f);
        c.rect(tx - tw * 0.5f, base - 2.f, tx + tw * 0.5f, base + th, trunk);
        float cr = c.H * r.range(0.07f, 0.1f);
        c.disc(tx, base + th + cr * 0.5f, cr, cn);
        c.disc(tx - cr * 0.25f, base + th + cr * 0.7f, cr * 0.6f, cn + 1);
    }
    // blades and flowers on the front hill
    float step = std::max(4.f, 7.f * c.d);
    for (float x = c.x0 + step * 0.5f; x < c.x1 && !c.full(); x += step) {
        float top = c.y0 + hillY(x, c.H * 0.17f, c.H * 0.06f, 0.05f, p3);
        float h = c.H * r.range(0.03f, 0.1f);
        c.add(x, top + h * 0.5f - 1.f, step * 0.7f, h, r.range(-14.f, 14.f), bl + r.pick(3));
        if (r.pick(10) == 0) {
            float s = std::max(3.f, 4.f * c.sd);
            c.add(x, top + h + s * 0.4f, s, s, 45.f, r.pick(2) ? fl0 : fl1);
        }
    }
}

// ------------------------------------------------------------------ neon
static void buildNeon(Ctx& c) {
    Rng& r = *c.r;
    c.a->theme = "Neon city";
    c.a->bg = rgb(10, 4, 34);
    int sk = c.ramp(rgb(10, 4, 34), rgb(112, 22, 122), 6);
    int sun = c.ramp(rgb(255, 234, 92), rgb(255, 58, 150), 5);
    int b0 = c.col(rgb(14, 8, 30));
    int b1 = c.col(rgb(28, 14, 54));
    int w0 = c.ramp(rgb(84, 242, 255), rgb(255, 84, 204), 2);
    int w2 = c.col(rgb(255, 232, 92));
    int fl = c.col(rgb(14, 6, 32));
    int gA = c.col(rgb(255, 60, 190));
    int gB = c.col(rgb(60, 230, 255));
    float hy = c.y0 + c.H * 0.36f;
    float cx = (c.x0 + c.x1) * 0.5f;
    c.sky(sk, 6);

    float R = std::min(c.W, c.H) * 0.26f;
    c.disc(cx, hy + R * 0.5f, R, sun, 5, true);

    float x = c.x0;
    int bi = 0;
    while (x < c.x1 && !c.full()) {
        float bwid = std::max(8.f, c.W * r.range(0.04f, 0.09f));
        float bhgt = c.H * r.range(0.1f, 0.34f);
        float xr = std::min(x + bwid, c.x1);
        c.rect(x, hy, xr, hy + bhgt, (bi % 2) ? b1 : b0);
        if (r.pick(4) == 0) c.rect(x + bwid * 0.45f, hy + bhgt, x + bwid * 0.55f, hy + bhgt + c.H * 0.05f, b0);
        float sp = std::max(3.5f, 7.f * c.d), ww = sp * 0.45f;
        for (float wy = hy + sp; wy < hy + bhgt - sp * 0.5f && !c.full(); wy += sp) {
            for (float wx = x + sp * 0.6f; wx < xr - ww && !c.full(); wx += sp) {
                if (r.pick(3) == 0) {
                    int cc = r.pick(4);
                    c.rect(wx, wy, wx + ww, wy + ww * 1.2f, cc == 3 ? w2 : w0 + (cc % 2));
                }
            }
        }
        x += bwid * r.range(0.92f, 1.05f);
        ++bi;
    }
    c.rect(c.x0, c.y0, c.x1, hy, fl);
    float th = std::max(1.2f, 1.8f * c.sd);
    c.rect(c.x0, hy - th, c.x1, hy + th, gA);
    int nh = 8;
    for (int k = 1; k <= nh; ++k) {
        float y = hy - (hy - c.y0) * std::pow((float)k / (float)nh, 1.8f);
        c.rect(c.x0, y - th * 0.5f, c.x1, y + th * 0.5f, gA);
    }
    int m = 14;
    for (int i = -m / 2; i <= m / 2; ++i) {
        float xb = cx + (float)i * (c.W / (float)m) * 1.3f;
        if (xb < c.x0 || xb > c.x1) continue;
        c.line(cx, hy, xb, c.y0, th, gB);
    }
}

// ------------------------------------------------------------------- ice
static void buildIce(Ctx& c) {
    Rng& r = *c.r;
    c.a->theme = "Ice";
    c.a->bg = rgb(8, 16, 48);
    int sk = c.ramp(rgb(8, 16, 48), rgb(72, 132, 192), 6);
    int cr = c.ramp(rgb(58, 128, 200), rgb(226, 248, 255), 4);
    int a0 = c.col(rgb(60, 232, 170));
    int a1 = c.col(rgb(92, 202, 255));
    int a2 = c.col(rgb(172, 124, 255));
    int sn = c.col(rgb(255, 255, 255));
    int gr = c.col(rgb(212, 238, 250));
    c.sky(sk, 6);

    float step = std::max(5.f, 9.f * c.d);
    float ph = r.range(0.f, 6.28f);
    int idx = 0;
    for (float x = c.x0; x < c.x1 && !c.full(); x += step, ++idx) {
        for (int k = 0; k < 2; ++k) {
            float cyb = c.y1 - c.H * (0.16f + 0.1f * (float)k) + std::sin(x * 0.045f + (float)k * 2.f + ph) * c.H * 0.05f;
            float hgt = c.H * (k == 0 ? 0.12f + 0.08f * std::sin(x * 0.09f) : 0.04f + 0.03f * std::sin(x * 0.11f + 1.f));
            c.rect(x, cyb - hgt, x + step + 0.5f, cyb, k == 0 ? ((idx % 3) ? a1 : a0) : a2);
        }
    }
    float gh0 = c.H * 0.12f;
    for (float x = c.x0; x < c.x1 && !c.full(); x += step) {
        c.rect(x, c.y0, x + step + 0.5f, c.y0 + gh0 + c.H * 0.03f * std::sin(x * 0.04f + ph), gr);
    }
    int nc = std::min(std::max((int)(c.W / 50.f), 3), 14);
    for (int i = 0; i < nc && !c.full(); ++i) {
        float px = c.x0 + c.W * ((float)i + r.range(0.2f, 0.8f)) / (float)nc;
        float hgt = c.H * r.range(0.25f, 0.62f);
        float wc = std::max(6.f, std::min(c.W * 0.08f, hgt * 0.28f));
        float base = c.y0 + gh0 * 0.6f;
        int t = 1 + r.pick(2);
        float bodyTop = base + hgt * 0.72f;
        c.rect(px - wc * 0.5f, base, px + wc * 0.5f, bodyTop, cr + t);
        c.add(px, bodyTop, wc * 0.72f, wc * 0.72f, 45.f, cr + t);
        c.rect(px - wc * 0.5f, base, px - wc * 0.18f, bodyTop, cr + 3);
        c.rect(px + wc * 0.28f, base, px + wc * 0.5f, bodyTop, cr);
    }
    int ns = (int)std::min(c.W * c.H / std::max(400.f, 800.f * c.d), 300.f) + 4;
    for (int i = 0; i < ns && !c.full(); ++i) {
        float s = r.range(2.f, 4.f) * c.sd;
        c.add(r.range(c.x0, c.x1), r.range(c.y0, c.y1), s, s, 45.f, sn);
    }
}

// --------------------------------------------------------------- abstract
static void buildGeneric(Ctx& c, uint32_t hh) {
    Rng& r = *c.r;
    c.a->theme = "Abstract";
    float h = (float)(hh % 360u) / 360.f;
    c.a->bg = hsv(h, 0.65f, 0.12f);
    int bg = c.ramp(hsv(h, 0.65f, 0.12f), hsv(h + 0.06f, 0.6f, 0.34f), 6);
    int ac = c.ramp(hsv(h + 0.55f, 0.7f, 0.5f), hsv(h + 0.55f, 0.3f, 1.f), 5);
    int bd = c.ramp(hsv(h + 0.08f, 0.8f, 0.6f), hsv(h + 0.08f, 0.4f, 1.f), 4);
    c.sky(bg, 6);
    float mn = std::min(c.W, c.H);

    int nb = 3 + r.pick(3);
    for (int i = 0; i < nb; ++i) {
        c.add(c.x0 + c.W * r.range(0.1f, 0.9f), c.y0 + c.H * r.range(0.1f, 0.9f), mn * 0.9f,
              std::max(4.f, c.H * r.range(0.05f, 0.14f)), r.range(28.f, 48.f), bd + r.pick(2));
    }
    float cx = c.x0 + c.W * r.range(0.35f, 0.65f), cy = c.y0 + c.H * r.range(0.4f, 0.6f);
    float R = mn * 0.3f;
    for (int k = 0; k < 5; ++k) c.disc(cx, cy, R * (1.f - 0.18f * (float)k), ac + k);
    int dots = 12;
    for (int i = 0; i < dots && !c.full(); ++i) {
        float an = (float)i / (float)dots * 6.2832f;
        float s = (4.f + (float)(i % 3) * 2.f) * c.sd;
        c.add(cx + std::cos(an) * R * 1.35f, cy + std::sin(an) * R * 1.35f, s, s, 45.f, bd + r.pick(4));
    }
    float cell = std::max(12.f, 28.f * c.d);
    int nf = (int)std::min(c.W * c.H / (cell * cell) * 0.25f, 200.f) + 3;
    for (int i = 0; i < nf && !c.full(); ++i) {
        float s = r.range(3.f, 8.f) * c.sd;
        c.add(r.range(c.x0, c.x1), r.range(c.y0, c.y1), s, s, 45.f, bd + r.pick(4));
    }
}

// Region is [0,W] x [0,H] (plus 10% on each side when over = true).
// Draws with as much detail as fits under the object limit.
static Art generateArt(std::string const& prompt, float W, float H, int limit, bool over, int variant) {
    auto ws = words(prompt);
    bool moss = false, society = false;
    Theme t = detect(ws, moss, society);
    uint32_t hh = hashStr(prompt);
    uint32_t seed = hh * 2654435761u + (uint32_t)variant * 40503u + 17u;
    limit = std::max(limit, 20);
    W = std::max(W, 10.f);
    H = std::max(H, 10.f);
    float e = over ? 0.1f * std::min(W, H) : 0.f;

    Art last;
    float d = 0.4f;
    for (int attempt = 0; attempt < 16; ++attempt, d *= 1.25f) {
        Art a;
        Rng r(seed);
        Ctx c;
        c.a = &a;
        c.r = &r;
        c.x0 = -e; c.y0 = -e; c.x1 = W + e; c.y1 = H + e;
        c.W = W + 2.f * e; c.H = H + 2.f * e;
        c.d = d;
        c.sd = std::sqrt(std::max(d, 0.6f));
        c.moss = moss;
        c.society = society;
        switch (t) {
            case Theme::Bricks: buildBricks(c); break;
            case Theme::Stars: buildStars(c); break;
            case Theme::Fire: buildFire(c); break;
            case Theme::Water: buildWater(c); break;
            case Theme::Grass: buildGrass(c); break;
            case Theme::Rays: buildRays(c); break;
            case Theme::Neon: buildNeon(c); break;
            case Theme::Ice: buildIce(c); break;
            default: buildGeneric(c, hh); break;
        }
        bool fits = a.prims.size() <= (size_t)limit;
        if (fits && !a.prims.empty()) return a;
        last = std::move(a);
    }
    if (last.prims.size() > (size_t)limit) last.prims.resize((size_t)limit);
    if (last.palette.empty()) last.palette.push_back(rgb(120, 120, 140));
    if (last.prims.empty()) last.prims.push_back(Prim{ W * 0.5f, H * 0.5f, W * 0.8f, H * 0.8f, 0.f, 0 });
    return last;
}

} // namespace deco

// ===========================================================================
// SETTINGS (saved between sessions)
// ===========================================================================
struct AccentColor { char const* name; ccColor3B c; };
static const AccentColor kAccents[] = {
    { "Green",  { 80, 220, 110 } },
    { "Blue",   { 70, 150, 255 } },
    { "Pink",   { 255, 90, 180 } },
    { "Purple", { 170, 100, 255 } },
    { "Orange", { 255, 160, 50 } },
    { "Red",    { 240, 70, 70 } },
    { "Cyan",   { 60, 230, 230 } },
    { "White",  { 235, 235, 235 } },
};
static const int kAccentCount = 8;
static char const* const kAiNames[3] = { "OpenAI", "Claude", "ChatGPT" };
static char const* const kAiRows[3] = { "1: OpenAI", "2: Claude (BEST)", "3: ChatGPT" };

static int getAI() {
    int v = Mod::get()->getSavedValue<int>("ai", 1);
    return (v < 0 || v > 2) ? 1 : v;
}
static void setAI(int v) { Mod::get()->setSavedValue<int>("ai", v); }
static int getAccentIdx() {
    int v = Mod::get()->getSavedValue<int>("accent", 0);
    return (v < 0 || v >= kAccentCount) ? 0 : v;
}
static void setAccentIdx(int v) { Mod::get()->setSavedValue<int>("accent", v); }
static ccColor3B accentColor() { return kAccents[getAccentIdx()].c; }
static ccColor4F accentF() {
    auto a = accentColor();
    return ccColor4F{ a.r / 255.f, a.g / 255.f, a.b / 255.f, 1.f };
}
static bool isSetupDone() { return Mod::get()->getSavedValue<bool>("setup-done", false); }
static void markSetupDone() { Mod::get()->setSavedValue<bool>("setup-done", true); }

static void refreshOrb();  // defined after AiOrb

// ===========================================================================
// GEODE GLUE
// ===========================================================================
static std::vector<Ref<GameObject>> g_lastBatch;

static void placeColorTrigger(LevelEditorLayer* lel, int channel, ccColor3B col, CCPoint pos) {
    auto obj = lel->createObject(899, pos, true);
    if (!obj) return;
    if (auto trig = typeinfo_cast<EffectGameObject*>(obj)) {
        trig->m_triggerTargetColor = col;
        trig->m_targetGroupID = channel;
        trig->m_duration = 0.f;
    }
    g_lastBatch.push_back(obj);
}

static void placeDeco(LevelEditorLayer* lel, int id, CCPoint pos, float rot,
                      float sx, float sy, int channel, ccColor3B col) {
    auto obj = lel->createObject(id, pos, true);
    if (!obj) return;
    obj->m_isNoTouch = true;  // decoration only, never blocks the player
    obj->setRotation(rot);
    obj->updateCustomScaleX(sx);
    obj->updateCustomScaleY(sy);
    if (obj->m_baseColor) obj->m_baseColor->m_colorID = channel;
    obj->setObjectColor(col);
    g_lastBatch.push_back(obj);
}

static void keepArt(EditorUI* ui, deco::Art const& art, float ox, float oy) {
    auto lel = ui->m_editorLayer;
    int n = (int)art.palette.size();
    if (!lel || n == 0) return;
    g_lastBatch.clear();
    for (int i = 0; i < n; ++i) {
        auto c = art.palette[i];
        // color triggers in columns of 8, just left of the drawing
        float tx = ox - 45.f - 30.f * (float)(i / 8);
        float ty = oy + 30.f * (float)(i % 8);
        placeColorTrigger(lel, 101 + i, ccColor3B{ c.r, c.g, c.b }, CCPoint(tx, ty));
    }
    for (auto const& p : art.prims) {
        int ci = ((p.ci % n) + n) % n;
        auto c = art.palette[ci];
        placeDeco(lel, 1, CCPoint(ox + p.x, oy + p.y), p.rot, p.w / 30.f, p.h / 30.f,
                  101 + ci, ccColor3B{ c.r, c.g, c.b });
    }
}

static void removeLastBatch(EditorUI* ui) {
    auto lel = ui->m_editorLayer;
    if (!lel) return;
    for (auto& o : g_lastBatch) {
        if (o && o->getParent()) lel->removeObject(o.data(), true);
    }
    g_lastBatch.clear();
}

static CCLabelBMFont* makeLabel(CCNode* parent, std::string const& text, float x, float y,
                                float scale, bool left) {
    auto l = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    l->setScale(scale);
    l->setAnchorPoint({ left ? 0.f : 0.5f, 0.5f });
    l->setPosition({ x, y });
    parent->addChild(l);
    return l;
}

static std::string clip(std::string const& s, size_t n) {
    return s.size() <= n ? s : s.substr(0, n) + "...";
}

static const int kLimits[] = { 100, 200, 300, 500, 800, 1000, 1500, 2000 };
static const int kLimitCount = 8;

// ---------------------------------------------------------------------------
// Settings / AI picker popup
// ---------------------------------------------------------------------------
class SettingsPopup : public Popup {
protected:
    bool m_first = false;
    std::vector<ButtonSprite*> m_aiSprs;
    std::vector<CCLayerColor*> m_aiBars;
    std::vector<CCDrawNode*> m_swatches;
    std::function<void()> m_onClosed;

    void refreshAI() {
        int cur = getAI();
        for (int i = 0; i < (int)m_aiSprs.size(); ++i) {
            m_aiBars[i]->setVisible(i == cur);
            m_aiSprs[i]->setColor(i == cur ? ccColor3B{ 255, 255, 255 } : ccColor3B{ 105, 105, 105 });
        }
    }

    void refreshColor() {
        int cur = getAccentIdx();
        for (int i = 0; i < (int)m_swatches.size(); ++i) {
            auto cc = kAccents[i].c;
            m_swatches[i]->clear();
            if (i == cur) m_swatches[i]->drawDot({ 15.f, 15.f }, 15.f, ccColor4F{ 1.f, 1.f, 1.f, 1.f });
            m_swatches[i]->drawDot({ 15.f, 15.f }, i == cur ? 11.5f : 13.5f,
                                   ccColor4F{ cc.r / 255.f, cc.g / 255.f, cc.b / 255.f, 1.f });
        }
        for (auto bar : m_aiBars) bar->setColor(accentColor());
    }

    bool init(bool first, std::function<void()> cb) {
        float W = first ? 320.f : 340.f;
        float H = first ? 270.f : 320.f;
        if (!Popup::init(W, H)) return false;
        m_first = first;
        m_onClosed = std::move(cb);
        this->setTitle(first ? "Choose your AI" : "Settings");
        if (first && m_closeBtn) m_closeBtn->setVisible(false);

        auto root = CCNode::create();
        root->setContentSize({ W, H });
        root->setAnchorPoint({ 0.f, 0.f });
        m_mainLayer->addChildAtPosition(root, Anchor::BottomLeft);

        float rowY0 = first ? 170.f : 238.f;
        if (first) {
            makeLabel(root, "Which AI would you prefer to deco?", W * 0.5f, 212.f, 0.6f, false);
        }
        else {
            makeLabel(root, "AI", W * 0.5f, 270.f, 0.65f, false);
        }

        // highlight bars first so the buttons draw on top
        for (int i = 0; i < 3; ++i) {
            auto bar = CCLayerColor::create(ccColor4B{ 255, 255, 255, 255 }, 216.f, 40.f);
            bar->setPosition({ W * 0.5f - 108.f, rowY0 - 38.f * (float)i - 20.f });
            root->addChild(bar);
            m_aiBars.push_back(bar);
        }

        auto menu = CCMenu::create();
        menu->setContentSize({ W, H });
        menu->setPosition({ 0.f, 0.f });
        root->addChild(menu);

        for (int i = 0; i < 3; ++i) {
            auto spr = ButtonSprite::create(kAiRows[i], 200, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.7f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SettingsPopup::onPickAI));
            btn->setTag(i);
            btn->setPosition({ W * 0.5f, rowY0 - 38.f * (float)i });
            menu->addChild(btn);
            m_aiSprs.push_back(spr);
        }

        if (!first) {
            makeLabel(root, "Color", W * 0.5f, 124.f, 0.65f, false);
            for (int i = 0; i < kAccentCount; ++i) {
                auto node = CCNode::create();
                node->setContentSize({ 30.f, 30.f });
                node->setAnchorPoint({ 0.5f, 0.5f });
                auto dn = CCDrawNode::create();
                node->addChild(dn);
                m_swatches.push_back(dn);
                auto btn = CCMenuItemSpriteExtra::create(node, this, menu_selector(SettingsPopup::onPickColor));
                btn->setTag(i);
                btn->setPosition({ 34.f + 38.f * (float)i, 96.f });
                menu->addChild(btn);
            }
        }

        makeLabel(root, "Drawing is built-in for now (no AI key connected).", W * 0.5f, first ? 66.f : 64.f, 0.4f, false);

        auto doneSpr = ButtonSprite::create("Done", 90, true, "goldFont.fnt", "GJ_button_01.png", 30.f, 0.8f);
        auto doneBtn = CCMenuItemSpriteExtra::create(doneSpr, this, menu_selector(SettingsPopup::onDone));
        doneBtn->setPosition({ W * 0.5f, 34.f });
        menu->addChild(doneBtn);

        refreshAI();
        refreshColor();
        return true;
    }

    void onPickAI(CCObject* sender) {
        int i = static_cast<CCNode*>(sender)->getTag();
        setAI(i);
        refreshAI();
    }

    void onPickColor(CCObject* sender) {
        int i = static_cast<CCNode*>(sender)->getTag();
        setAccentIdx(i);
        refreshColor();
        refreshOrb();
    }

    void onDone(CCObject*) {
        if (m_first) markSetupDone();
        this->onClose(nullptr);
    }

    void onClose(CCObject* sender) override {
        this->retain();
        Popup::onClose(sender);
        auto cb = std::move(m_onClosed);
        m_onClosed = nullptr;
        this->release();
        if (cb) cb();
    }

public:
    static SettingsPopup* create(bool first, std::function<void()> cb) {
        auto ret = new SettingsPopup();
        if (ret->init(first, std::move(cb))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Art Agent panel
// ---------------------------------------------------------------------------
class ArtAgentPopup : public Popup {
protected:
    static constexpr float kPW = 200.f;  // preview width
    static constexpr float kPH = 110.f;  // preview height
    static constexpr float kPX = 225.f;  // preview left
    static constexpr float kPY = 130.f;  // preview bottom

    EditorUI* m_ui = nullptr;
    TextInput* m_input = nullptr;
    CCDrawNode* m_draw = nullptr;
    CCDrawNode* m_frame = nullptr;
    CCLayerColor* m_bgLayer = nullptr;
    CCLabelBMFont* m_selLabel = nullptr;
    CCLabelBMFont* m_lines[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    CCLabelBMFont* m_countLabel = nullptr;
    CCLabelBMFont* m_limitLabel = nullptr;
    ButtonSprite* m_insideSpr = nullptr;
    ButtonSprite* m_overSpr = nullptr;

    deco::Art m_art;
    bool m_have = false;
    bool m_over = false;
    int m_variant = 1;
    int m_limitIdx = 3;
    std::string m_prompt;

    float m_ox = 0.f, m_oy = 0.f, m_W = 100.f, m_H = 100.f;
    int m_selCount = 0;

    void computeArea() {
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        m_selCount = 0;
        auto consider = [&](GameObject* o) {
            if (!o) return;
            auto p = o->getPosition();
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
            ++m_selCount;
        };
        auto arr = m_ui->getSelectedObjects();
        if (arr && arr->count() > 0) {
            for (unsigned int i = 0; i < arr->count(); ++i) {
                consider(static_cast<GameObject*>(arr->objectAtIndex(i)));
            }
        }
        else {
            consider(m_ui->m_selectedObject);
        }

        if (m_selCount > 0) {
            m_ox = minX - 15.f;
            m_oy = minY - 15.f;
            m_W = (maxX - minX) + 30.f;
            m_H = (maxY - minY) + 30.f;
        }
        else {
            // nothing selected: use the middle of the visible screen
            auto lel = m_ui->m_editorLayer;
            float scale = 1.f, px = 0.f, py = 0.f;
            if (lel && lel->m_objectLayer) {
                scale = lel->m_objectLayer->getScale();
                if (scale <= 0.f) scale = 1.f;
                px = lel->m_objectLayer->getPositionX();
                py = lel->m_objectLayer->getPositionY();
            }
            auto win = CCDirector::get()->getWinSize();
            float x0 = -px / scale, y0 = -py / scale;
            float w = win.width / scale, h = win.height / scale;
            m_ox = x0 + w * 0.2f;
            m_oy = y0 + h * 0.2f;
            m_W = w * 0.6f;
            m_H = h * 0.6f;
        }
    }

    void setLine(int i, std::string const& text) {
        if (i >= 0 && i < 6 && m_lines[i]) m_lines[i]->setString(text.c_str());
    }

    std::string aiLine() {
        return std::string("AI: ") + kAiNames[getAI()] + " (built-in drawing)";
    }

    void refreshLabels() {
        int limit = kLimits[m_limitIdx];
        setLine(0, aiLine());
        setLine(1, "Prompt: " + (m_prompt.empty() ? std::string("-") : clip(m_prompt, 24)));
        setLine(2, "Theme: " + (m_have ? m_art.theme : std::string("-")));
        setLine(3, "Result: " + std::to_string(m_have ? m_art.prims.size() : 0) + " objects");
        setLine(4, "Variant " + std::to_string(m_variant) + "  |  Mode: " + (m_over ? "Over" : "Inside"));
        m_countLabel->setString((std::to_string(m_have ? m_art.prims.size() : 0) + " / " + std::to_string(limit) + " objects").c_str());
        m_limitLabel->setString(("Limit " + std::to_string(limit)).c_str());
    }

    void updateModeButtons() {
        m_insideSpr->setColor(m_over ? ccColor3B{ 110, 110, 110 } : ccColor3B{ 255, 255, 255 });
        m_overSpr->setColor(m_over ? ccColor3B{ 255, 255, 255 } : ccColor3B{ 110, 110, 110 });
    }

    void drawFrame() {
        m_frame->clear();
        auto c = accentF();
        float x = kPX - 3.f, y = kPY - 3.f, w = kPW + 6.f, h = kPH + 6.f;
        m_frame->drawSegment({ x, y }, { x + w, y }, 1.2f, c);
        m_frame->drawSegment({ x + w, y }, { x + w, y + h }, 1.2f, c);
        m_frame->drawSegment({ x + w, y + h }, { x, y + h }, 1.2f, c);
        m_frame->drawSegment({ x, y + h }, { x, y }, 1.2f, c);
        m_frame->drawSegment({ 20.f, 245.f }, { 420.f, 245.f }, 0.8f, c);
        m_countLabel->setColor(accentColor());
        m_limitLabel->setColor(accentColor());
    }

    void redraw() {
        m_draw->clear();
        if (!m_have) return;
        float e = m_over ? 0.1f * std::min(m_W, m_H) : 0.f;
        float tw = m_W + 2.f * e, th = m_H + 2.f * e;
        float s = std::min(kPW / tw, kPH / th) * 0.92f;
        float offX = (kPW - tw * s) * 0.5f, offY = (kPH - th * s) * 0.5f;
        auto mp = [&](float x, float y) {
            return CCPoint(offX + (x + e) * s, offY + (y + e) * s);
        };

        int n = (int)m_art.palette.size();
        if (n == 0) return;
        for (auto const& p : m_art.prims) {
            auto col = m_art.palette[((p.ci % n) + n) % n];
            ccColor4F cf{ col.r / 255.f, col.g / 255.f, col.b / 255.f, 1.f };
            CCPoint ctr = mp(p.x, p.y);
            float hw = p.w * 0.5f * s, hh = p.h * 0.5f * s;
            if (hw < 0.3f && hh < 0.3f) {
                m_draw->drawDot(ctr, 0.4f, cf);
                continue;
            }
            float ang = -p.rot * 0.0174533f;
            float cs = std::cos(ang), sn = std::sin(ang);
            CCPoint v[4];
            float lx[4] = { -hw, hw, hw, -hw };
            float ly[4] = { -hh, -hh, hh, hh };
            for (int k = 0; k < 4; ++k) {
                v[k] = CCPoint(ctr.x + lx[k] * cs - ly[k] * sn, ctr.y + lx[k] * sn + ly[k] * cs);
            }
            m_draw->drawPolygon(v, 4, cf, 0.f, ccColor4F{ 0.f, 0.f, 0.f, 0.f });
        }

        // outline of the selected area
        ccColor4F ol{ 1.f, 1.f, 1.f, 0.5f };
        CCPoint a = mp(0.f, 0.f), b = mp(m_W, 0.f), c2 = mp(m_W, m_H), d2 = mp(0.f, m_H);
        m_draw->drawSegment(a, b, 0.5f, ol);
        m_draw->drawSegment(b, c2, 0.5f, ol);
        m_draw->drawSegment(c2, d2, 0.5f, ol);
        m_draw->drawSegment(d2, a, 0.5f, ol);
    }

    void generate() {
        m_art = deco::generateArt(m_prompt, m_W, m_H, kLimits[m_limitIdx], m_over, m_variant);
        m_have = true;
        m_bgLayer->setColor(ccColor3B{ m_art.bg.r, m_art.bg.g, m_art.bg.b });
        redraw();
        refreshLabels();
        setLine(5, "Status: drawn, press Keep to place it");
    }

    bool init(EditorUI* ui) {
        if (!Popup::init(440.f, 280.f)) return false;
        m_ui = ui;
        this->setTitle("Art Agent");
        computeArea();

        auto root = CCNode::create();
        root->setContentSize({ 440.f, 280.f });
        root->setAnchorPoint({ 0.f, 0.f });
        m_mainLayer->addChildAtPosition(root, Anchor::BottomLeft);

        // ---- left column: selection + status ----
        std::string sel = m_selCount > 0
            ? "Selection: " + std::to_string(m_selCount) + " objects, " +
              std::to_string((int)m_W) + " x " + std::to_string((int)m_H)
            : std::string("No selection: using the screen");
        m_selLabel = makeLabel(root, sel, 20.f, 231.f, 0.55f, true);
        makeLabel(root, "Type what to draw on the selection.", 20.f, 215.f, 0.45f, true);
        float ly = 196.f;
        for (int i = 0; i < 6; ++i) {
            m_lines[i] = makeLabel(root, "", 20.f, ly, 0.5f, true);
            ly -= 16.f;
        }

        m_input = TextInput::create(150.f, "Describe what to draw...", "chatFont.fnt");
        m_input->setMaxCharCount(80);
        m_input->setPosition({ 100.f, 58.f });
        root->addChild(m_input);

        // ---- right column: preview ----
        auto holder = CCNode::create();
        holder->setContentSize({ kPW, kPH });
        holder->setAnchorPoint({ 0.f, 0.f });
        holder->setPosition({ kPX, kPY });
        root->addChild(holder);
        m_bgLayer = CCLayerColor::create(ccColor4B{ 24, 24, 32, 255 }, kPW, kPH);
        holder->addChild(m_bgLayer);
        m_draw = CCDrawNode::create();
        holder->addChild(m_draw, 1);

        m_frame = CCDrawNode::create();
        root->addChild(m_frame, 2);

        m_countLabel = makeLabel(root, "0 / 500 objects", 325.f, 118.f, 0.5f, false);
        m_limitLabel = makeLabel(root, "Limit 500", 325.f, 96.f, 0.55f, false);

        // ---- buttons ----
        auto menu = CCMenu::create();
        menu->setContentSize({ 440.f, 280.f });
        menu->setPosition({ 0.f, 0.f });
        root->addChild(menu);

        auto addBtn = [&](char const* text, float scale, float x, float y, SEL_MenuHandler cb) -> ButtonSprite* {
            auto spr = ButtonSprite::create(text);
            spr->setScale(scale);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
            btn->setPosition({ x, y });
            menu->addChild(btn);
            return spr;
        };
        addBtn("Go", 0.5f, 196.f, 58.f, menu_selector(ArtAgentPopup::onGo));
        addBtn("Undo", 0.45f, 50.f, 90.f, menu_selector(ArtAgentPopup::onUndo));
        addBtn("<", 0.5f, 250.f, 96.f, menu_selector(ArtAgentPopup::onLimitDown));
        addBtn(">", 0.5f, 400.f, 96.f, menu_selector(ArtAgentPopup::onLimitUp));
        m_insideSpr = addBtn("Inside", 0.45f, 290.f, 70.f, menu_selector(ArtAgentPopup::onInside));
        m_overSpr = addBtn("Over", 0.45f, 360.f, 70.f, menu_selector(ArtAgentPopup::onOver));
        addBtn("Another variant", 0.4f, 280.f, 38.f, menu_selector(ArtAgentPopup::onVariant));
        addBtn("Keep", 0.5f, 385.f, 38.f, menu_selector(ArtAgentPopup::onKeep));
        addBtn("Settings", 0.4f, 392.f, 262.f, menu_selector(ArtAgentPopup::onSettings));

        updateModeButtons();
        drawFrame();
        refreshLabels();
        setLine(5, "Status: ready");
        return true;
    }

public:
    void applyAccent() {
        drawFrame();
        refreshLabels();
    }

protected:
    void onSettings(CCObject*) {
        Ref<ArtAgentPopup> self = this;
        auto p = SettingsPopup::create(false, [self]() { self->applyAccent(); });
        if (p) p->show();
    }

    void onGo(CCObject*) {
        std::string text = m_input->getString();
        if (text.empty()) {
            Notification::create("Type what to draw first", NotificationIcon::Error)->show();
            return;
        }
        m_prompt = text;
        m_variant = 1;
        generate();
    }

    void onVariant(CCObject*) {
        if (m_prompt.empty()) {
            Notification::create("Type what to draw first", NotificationIcon::Error)->show();
            return;
        }
        ++m_variant;
        generate();
    }

    void onKeep(CCObject*) {
        if (!m_have) {
            Notification::create("Draw something first", NotificationIcon::Error)->show();
            return;
        }
        keepArt(m_ui, m_art, m_ox, m_oy);
        setLine(5, "Status: kept (" + std::to_string(m_art.prims.size()) + " objects)");
        Notification::create("Kept " + std::to_string(m_art.prims.size()) + " objects",
                             NotificationIcon::Success)->show();
    }

    void onUndo(CCObject*) {
        removeLastBatch(m_ui);
        setLine(5, "Status: undone");
        Notification::create("Removed last batch", NotificationIcon::Success)->show();
    }

    void onLimitDown(CCObject*) {
        if (m_limitIdx > 0) --m_limitIdx;
        if (m_have) generate(); else refreshLabels();
    }

    void onLimitUp(CCObject*) {
        if (m_limitIdx < kLimitCount - 1) ++m_limitIdx;
        if (m_have) generate(); else refreshLabels();
    }

    void onInside(CCObject*) {
        m_over = false;
        updateModeButtons();
        if (m_have) generate(); else refreshLabels();
    }

    void onOver(CCObject*) {
        m_over = true;
        updateModeButtons();
        if (m_have) generate(); else refreshLabels();
    }

public:
    static ArtAgentPopup* create(EditorUI* ui) {
        auto ret = new ArtAgentPopup();
        if (ret->init(ui)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Draggable circle button (tap = open Art Agent, drag = move it)
// ---------------------------------------------------------------------------
class AiOrb : public CCLayer {
protected:
    static constexpr float kR = 22.f;
    static inline AiOrb* s_orb = nullptr;

    EditorUI* m_ui = nullptr;
    CCDrawNode* m_draw = nullptr;
    CCLabelBMFont* m_label = nullptr;
    bool m_hit = false;
    bool m_drag = false;
    CCPoint m_grab;
    CCPoint m_touchStart;

    ~AiOrb() {
        if (s_orb == this) s_orb = nullptr;
    }

    bool init(EditorUI* ui) {
        if (!CCLayer::init()) return false;
        m_ui = ui;
        m_draw = CCDrawNode::create();
        this->addChild(m_draw);
        m_label = CCLabelBMFont::create("AI", "bigFont.fnt");
        m_label->setScale(0.5f);
        this->addChild(m_label, 1);

        this->setTouchMode(kCCTouchesOneByOne);
        this->setTouchEnabled(true);

        auto win = CCDirector::get()->getWinSize();
        double sx = Mod::get()->getSavedValue<double>("orb-x", -1.0);
        double sy = Mod::get()->getSavedValue<double>("orb-y", -1.0);
        if (sx < 0.0 || sy < 0.0) {
            sx = win.width - 38.0;
            sy = win.height * 0.5;
        }
        float px = std::min(std::max((float)sx, kR), win.width - kR);
        float py = std::min(std::max((float)sy, kR), win.height - kR);
        this->setPosition({ px, py });

        s_orb = this;
        refresh();
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, -300, true);
    }

    bool canTouch() {
        if (!m_ui || !m_ui->isVisible() || !this->isVisible()) return false;
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return false;
        if (scene->getChildByType<FLAlertLayer>(0)) return false;
        if (scene->getChildByType<EditorPauseLayer>(0)) return false;
        return true;
    }

    bool ccTouchBegan(CCTouch* t, CCEvent*) override {
        if (!canTouch()) return false;
        auto p = this->convertToNodeSpace(t->getLocation());
        if (std::sqrt(p.x * p.x + p.y * p.y) > kR + 6.f) return false;
        m_hit = true;
        m_drag = false;
        m_touchStart = t->getLocation();
        auto par = this->getParent();
        CCPoint loc = par ? par->convertToNodeSpace(t->getLocation()) : t->getLocation();
        m_grab = this->getPosition() - loc;
        this->setScale(0.92f);
        return true;
    }

    void ccTouchMoved(CCTouch* t, CCEvent*) override {
        if (!m_hit) return;
        CCPoint cur = t->getLocation();
        float dx = cur.x - m_touchStart.x, dy = cur.y - m_touchStart.y;
        if (!m_drag && dx * dx + dy * dy > 64.f) m_drag = true;
        if (m_drag) {
            auto par = this->getParent();
            CCPoint loc = par ? par->convertToNodeSpace(cur) : cur;
            CCPoint np = loc + m_grab;
            auto win = CCDirector::get()->getWinSize();
            np.x = std::min(std::max(np.x, kR), win.width - kR);
            np.y = std::min(std::max(np.y, kR), win.height - kR);
            this->setPosition(np);
        }
    }

    void ccTouchEnded(CCTouch*, CCEvent*) override {
        if (!m_hit) return;
        m_hit = false;
        this->setScale(1.f);
        if (m_drag) {
            auto p = this->getPosition();
            Mod::get()->setSavedValue<double>("orb-x", (double)p.x);
            Mod::get()->setSavedValue<double>("orb-y", (double)p.y);
        }
        else if (m_ui) {
            if (auto popup = ArtAgentPopup::create(m_ui)) popup->show();
        }
    }

    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        m_hit = false;
        this->setScale(1.f);
    }

public:
    void refresh() {
        auto a = accentColor();
        float r = a.r / 255.f, g = a.g / 255.f, b = a.b / 255.f;
        m_draw->clear();
        m_draw->drawDot({ 0.f, 0.f }, kR + 3.f, ccColor4F{ 0.f, 0.f, 0.f, 0.5f });
        m_draw->drawDot({ 0.f, 0.f }, kR, ccColor4F{ r * 0.55f, g * 0.55f, b * 0.55f, 1.f });
        m_draw->drawDot({ 0.f, 0.f }, kR - 3.f, ccColor4F{ r, g, b, 1.f });
        m_draw->drawDot({ -kR * 0.28f, kR * 0.3f }, kR * 0.3f, ccColor4F{ 1.f, 1.f, 1.f, 0.28f });
    }

    static void refreshAll() {
        if (s_orb) s_orb->refresh();
    }

    static AiOrb* create(EditorUI* ui) {
        auto ret = new AiOrb();
        if (ret->init(ui)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

static void refreshOrb() { AiOrb::refreshAll(); }

// ---------------------------------------------------------------------------
// Editor hook
// ---------------------------------------------------------------------------
class $modify(AutoDecoEditorUI, EditorUI) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;

        g_lastBatch.clear();

        if (auto orb = AiOrb::create(this)) {
            orb->setID("ai-orb"_spr);
            this->addChild(orb, 1000);
        }

        // first time only: ask which AI to use as soon as the editor is up
        if (!isSetupDone()) {
            this->runAction(CCSequence::create(
                CCDelayTime::create(0.4f),
                CCCallFunc::create(this, callfunc_selector(AutoDecoEditorUI::showFirstRun)),
                nullptr));
        }
        return true;
    }

    void showFirstRun() {
        if (isSetupDone()) return;
        if (auto p = SettingsPopup::create(true, nullptr)) p->show();
    }
};

