// Auto Deco - Art Agent (single-file Geode v5 mod).
// Select blocks in the level editor, tap "Deco", describe what to draw,
// preview it, then Keep it. This version draws with a built-in test
// generator (no AI key needed). The generator lives in namespace deco and
// does not touch Geode, so a real AI service can be plugged in later.

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <vector>
#include <string>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <algorithm>

using namespace geode::prelude;

// ===========================================================================
// PURE LOGIC: test generator (no Geode types in here)
// ===========================================================================
namespace deco {

struct RGB { unsigned char r, g, b; };
struct Prim { float x, y, w, h, rot; int ci; };  // center, size, rotation (deg, clockwise), palette index
struct Art {
    std::vector<Prim> prims;
    std::vector<RGB> palette;
    RGB bg{ 30, 60, 120 };
    std::string theme;
};

enum class Theme { Bricks, Stars, Fire, Water, Grass, Rays, Neon, Ice, Generic };

static const size_t kMaxPrims = 6000;

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

static void setupLook(Art& a, Theme t, bool society, uint32_t hh) {
    switch (t) {
        case Theme::Bricks:
            if (society) {
                a.theme = "Society (bricks)";
                a.palette = { rgb(150,30,36), rgb(100,22,28), rgb(55,55,62), rgb(120,120,130), rgb(80,80,88) };
                a.bg = rgb(20, 20, 24);
            }
            else {
                a.theme = "Bricks";
                a.palette = { rgb(150,148,136), rgb(122,120,108), rgb(98,96,88), rgb(88,124,52), rgb(62,98,42) };
                a.bg = rgb(60, 110, 190);
            }
            break;
        case Theme::Stars:
            a.theme = "Space";
            a.palette = { rgb(255,255,255), rgb(150,190,255), rgb(190,150,255), rgb(110,230,255) };
            a.bg = rgb(10, 12, 40);
            break;
        case Theme::Fire:
            a.theme = "Fire";
            a.palette = { rgb(255,230,90), rgb(255,150,40), rgb(230,60,30), rgb(120,25,20) };
            a.bg = rgb(30, 10, 10);
            break;
        case Theme::Water:
            a.theme = "Water";
            a.palette = { rgb(170,235,255), rgb(70,180,230), rgb(30,120,200), rgb(15,70,150) };
            a.bg = rgb(8, 25, 60);
            break;
        case Theme::Grass:
            a.theme = "Grass";
            a.palette = { rgb(140,210,70), rgb(90,170,50), rgb(55,125,40), rgb(30,85,35), rgb(255,220,90) };
            a.bg = rgb(120, 190, 235);
            break;
        case Theme::Rays:
            a.theme = "Heavenly rays";
            a.palette = { rgb(255,250,225), rgb(255,225,120), rgb(255,255,255), rgb(255,200,80) };
            a.bg = rgb(60, 45, 20);
            break;
        case Theme::Neon:
            a.theme = "Neon";
            a.palette = { rgb(255,40,180), rgb(40,240,255), rgb(255,255,255), rgb(170,255,60) };
            a.bg = rgb(6, 6, 18);
            break;
        case Theme::Ice:
            a.theme = "Ice";
            a.palette = { rgb(225,248,255), rgb(160,220,250), rgb(100,180,235), rgb(255,255,255) };
            a.bg = rgb(15, 35, 70);
            break;
        default: {
            a.theme = "Abstract";
            float h = (float)(hh % 360u) / 360.f;
            a.palette = { hsv(h, 0.6f, 0.95f), hsv(h + 0.08f, 0.7f, 0.8f),
                          hsv(h + 0.5f, 0.5f, 0.9f), hsv(h + 0.15f, 0.3f, 1.f) };
            a.bg = hsv(h, 0.5f, 0.15f);
            break;
        }
    }
}

// Fills a.prims for the region [x0,x1] x [y0,y1]. d = detail step
// (small = fine detail, many pieces).
static void build(Art& a, Theme t, bool moss, bool society,
                  float x0, float y0, float x1, float y1, float d, Rng& r) {
    float W = x1 - x0, H = y1 - y0;
    auto full = [&]() { return a.prims.size() > kMaxPrims; };
    auto add = [&](float x, float y, float w, float h, float rot, int ci) {
        if (w < 1.5f || h < 1.5f) return;
        x = std::min(std::max(x, x0), x1);
        y = std::min(std::max(y, y0), y1);
        a.prims.push_back(Prim{ x, y, w, h, rot, ci });
    };
    float sd = std::sqrt(std::max(d, 0.6f));

    switch (t) {
        case Theme::Bricks: {
            float bw = std::max(8.f, 36.f * d);
            float bh = std::max(5.f, 18.f * d);
            float gap = std::max(1.f, 2.f * d);
            int row = 0;
            for (float y = y0; y < y1 && !full(); y += bh, ++row) {
                float off = (row % 2) ? bw * 0.5f : 0.f;
                for (float x = x0 - off; x < x1 && !full(); x += bw) {
                    float l = std::max(x, x0), rr = std::min(x + bw, x1);
                    float tp = std::max(y, y0), bt = std::min(y + bh, y1);
                    if (rr - l < 4.f || bt - tp < 3.f) continue;
                    int ci = r.pick(3);
                    if (moss && r.f() < 0.28f) ci = 3 + r.pick(2);
                    add((l + rr) * 0.5f, (tp + bt) * 0.5f, rr - l - gap, bt - tp - gap, 0.f, ci);
                }
            }
            if (moss) {
                float step = std::max(5.f, 9.f * d);
                for (float x = x0; x < x1 && !full(); x += step) {
                    if (r.f() < 0.65f) {
                        float s = r.range(4.f, 9.f) * sd;
                        add(x, y1 - r.range(0.f, 4.f), s * 1.6f, s, r.range(-20.f, 20.f), 3 + r.pick(2));
                    }
                }
            }
            (void)society;
        } break;

        case Theme::Stars: {
            float cell = std::max(10.f, 24.f * d);
            float nf = std::min(W * H / (cell * cell) * 1.2f, (float)kMaxPrims) + 4.f;
            for (int i = 0; i < (int)nf && !full(); ++i) {
                float s = r.range(2.5f, 6.f) * sd;
                if (r.pick(14) == 0) s *= 2.4f;
                add(r.range(x0, x1), r.range(y0, y1), s, s, 45.f, r.pick(4));
            }
        } break;

        case Theme::Fire: {
            float colW = std::max(8.f, 22.f * d);
            for (float x = x0 + colW * 0.5f; x < x1 && !full(); x += colW) {
                float hgt = H * r.range(0.35f, 1.f);
                float y = y0 + colW * 0.55f;
                while (y < y0 + hgt && !full()) {
                    float tt = (y - y0) / hgt;
                    float s = colW * 1.1f * (1.f - 0.75f * tt);
                    int ci = tt < 0.25f ? 0 : tt < 0.55f ? 1 : tt < 0.85f ? 2 : 3;
                    add(x + r.range(-colW * 0.15f, colW * 0.15f), y, s, s, 45.f + r.range(-10.f, 10.f), ci);
                    y += std::max(2.f, s * 0.6f);
                }
            }
        } break;

        case Theme::Water: {
            float rowH = std::max(5.f, 14.f * d);
            float segW = std::max(10.f, 30.f * d);
            int row = 0;
            for (float y = y0 + rowH * 0.5f; y < y1 && !full(); y += rowH, ++row) {
                float tt = (y - y0) / H;
                int ci = tt > 0.75f ? 0 : tt > 0.5f ? 1 : tt > 0.25f ? 2 : 3;
                for (float x = x0; x < x1 && !full(); x += segW) {
                    float ph = x * 0.06f + row * 1.3f;
                    float yy = y + std::sin(ph) * rowH * 0.35f;
                    float slope = std::cos(ph) * 0.06f * rowH * 0.35f;
                    float w = std::min(segW, x1 - x) + 1.f;
                    add(x + w * 0.5f, yy, w, rowH * 0.7f, -std::atan(slope) * 57.29578f, ci);
                }
            }
        } break;

        case Theme::Grass: {
            float step = std::max(4.f, 7.f * d);
            for (float x = x0 + step * 0.5f; x < x1 && !full(); x += step) {
                float hgt = H * r.range(0.12f, 0.5f);
                add(x, y0 + hgt * 0.5f, step * 0.85f, hgt, r.range(-14.f, 14.f), r.pick(4));
                if (r.pick(9) == 0) add(x, y0 + hgt + 3.f, step * 1.2f, step * 1.2f, 45.f, 4);
            }
        } break;

        case Theme::Rays: {
            int n = std::min(std::max(8, (int)(30.f / d)), 220);
            float cx = (x0 + x1) * 0.5f, cy = y1;
            for (int i = 0; i < n && !full(); ++i) {
                float rad = r.range(-80.f, 80.f) * 0.0174533f;
                float sn = std::sin(rad), cs = std::cos(rad);
                float L = std::sqrt(W * W + H * H) * r.range(0.5f, 1.f);
                if (cs > 0.01f) L = std::min(L, H / cs);
                if (sn > 0.01f) L = std::min(L, (x1 - cx) / sn);
                else if (sn < -0.01f) L = std::min(L, (cx - x0) / -sn);
                float thick = r.range(3.f, 9.f) * std::max(0.6f, d);
                float theta = std::atan2(-cs, sn);
                add(cx + sn * L * 0.5f, cy - cs * L * 0.5f, L, thick, -theta * 57.29578f, r.pick(4));
            }
            for (int i = 0; i < n * 2 && !full(); ++i) {
                float s = r.range(2.f, 5.f) * sd;
                add(r.range(x0, x1), r.range(y0, y1), s, s, 45.f, r.pick(4));
            }
        } break;

        case Theme::Neon: {
            float gap = std::max(14.f, 40.f * d);
            float th = std::max(1.5f, 3.f * sd);
            int k = 0;
            for (float y = y0 + gap * 0.5f; y < y1 && !full(); y += gap, ++k)
                add((x0 + x1) * 0.5f, y, W, th, 0.f, k % 2);
            k = 0;
            for (float x = x0 + gap * 0.5f; x < x1 && !full(); x += gap, ++k)
                add(x, (y0 + y1) * 0.5f, th, H, 0.f, k % 2);
            for (float y = y0 + gap * 0.5f; y < y1 && !full(); y += gap)
                for (float x = x0 + gap * 0.5f; x < x1 && !full(); x += gap)
                    if (r.pick(3) == 0) add(x, y, th * 2.4f, th * 2.4f, 45.f, 2);
        } break;

        case Theme::Ice: {
            float step = std::max(6.f, 14.f * d);
            for (float x = x0 + step * 0.5f; x < x1 && !full(); x += step) {
                if (r.pick(3) == 0) continue;
                float hgt = H * r.range(0.15f, 0.7f);
                float w = r.range(5.f, 12.f) * std::max(0.7f, d);
                add(x + r.range(-step * 0.3f, step * 0.3f), y0 + hgt * 0.5f, w, hgt,
                    r.range(-12.f, 12.f), r.pick(3));
            }
            float cell = std::max(10.f, 26.f * d);
            float nf = std::min(W * H / (cell * cell), (float)kMaxPrims);
            for (int i = 0; i < (int)nf && !full(); ++i) {
                float s = r.range(2.f, 5.f) * sd;
                add(r.range(x0, x1), r.range(y0, y1), s, s, 45.f, 3);
            }
        } break;

        default: {
            float cell = std::max(12.f, 28.f * d);
            float nf = std::min(W * H / (cell * cell) * 0.9f, (float)kMaxPrims) + 3.f;
            for (int i = 0; i < (int)nf && !full(); ++i) {
                float s = r.range(4.f, 12.f) * sd;
                float x = r.range(x0, x1), y = r.range(y0, y1);
                int kind = r.pick(3);
                if (kind == 0) add(x, y, s, s, 45.f, r.pick(4));
                else if (kind == 1) add(x, y, s * 1.6f, s, r.range(0.f, 180.f), r.pick(4));
                else add(x, y, s * 3.f, s * 0.5f, r.range(0.f, 180.f), r.pick(4));
            }
        } break;
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
        setupLook(a, t, society, hh);
        Rng r(seed);
        build(a, t, moss, society, -e, -e, W + e, H + e, d, r);
        bool fits = a.prims.size() <= (size_t)limit;
        if (fits && !a.prims.empty()) return a;
        last = std::move(a);
    }
    if (last.prims.size() > (size_t)limit) last.prims.resize((size_t)limit);
    if (last.prims.empty()) last.prims.push_back(Prim{ W * 0.5f, H * 0.5f, W * 0.8f, H * 0.8f, 0.f, 0 });
    return last;
}

} // namespace deco

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
        placeColorTrigger(lel, 101 + i, ccColor3B{ c.r, c.g, c.b }, CCPoint(ox - 45.f, oy + 30.f * i));
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
// Art Agent panel
// ---------------------------------------------------------------------------
class ArtAgentPopup : public Popup {
protected:
    static constexpr float kPW = 200.f;  // preview width
    static constexpr float kPH = 110.f;  // preview height

    EditorUI* m_ui = nullptr;
    TextInput* m_input = nullptr;
    CCDrawNode* m_draw = nullptr;
    CCLayerColor* m_bgLayer = nullptr;
    CCLabelBMFont* m_selLabel = nullptr;
    CCLabelBMFont* m_lines[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
    CCLabelBMFont* m_countLabel = nullptr;
    CCLabelBMFont* m_limitLabel = nullptr;

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
        if (i >= 0 && i < 5 && m_lines[i]) m_lines[i]->setString(text.c_str());
    }

    void refreshLabels() {
        int limit = kLimits[m_limitIdx];
        setLine(0, "Prompt: " + clip(m_prompt, 26));
        setLine(1, "Theme: " + m_art.theme);
        setLine(2, "Result: " + std::to_string(m_art.prims.size()) + " objects");
        setLine(3, "Variant " + std::to_string(m_variant) + "  |  Mode: " + (m_over ? "Over" : "Inside"));
        m_countLabel->setString((std::to_string(m_art.prims.size()) + " / " + std::to_string(limit) + " objects").c_str());
        m_limitLabel->setString(("Limit " + std::to_string(limit)).c_str());
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

        // outline of the selected area
        ccColor4F ol{ 1.f, 1.f, 1.f, 0.55f };
        CCPoint a = mp(0.f, 0.f), b = mp(m_W, 0.f), c = mp(m_W, m_H), d = mp(0.f, m_H);
        m_draw->drawSegment(a, b, 0.6f, ol);
        m_draw->drawSegment(b, c, 0.6f, ol);
        m_draw->drawSegment(c, d, 0.6f, ol);
        m_draw->drawSegment(d, a, 0.6f, ol);

        int n = (int)m_art.palette.size();
        if (n == 0) return;
        for (auto const& p : m_art.prims) {
            auto col = m_art.palette[((p.ci % n) + n) % n];
            ccColor4F cf{ col.r / 255.f, col.g / 255.f, col.b / 255.f, 1.f };
            float len = std::max(p.w, p.h), thick = std::min(p.w, p.h);
            if (len - thick < 0.5f) {
                m_draw->drawDot(mp(p.x, p.y), std::max(0.6f, thick * 0.5f * s), cf);
            }
            else {
                float axis = p.rot + (p.h > p.w ? 90.f : 0.f);
                float rad = -axis * 0.0174533f;
                float half = (len - thick) * 0.5f;
                float dx = std::cos(rad) * half, dy = std::sin(rad) * half;
                m_draw->drawSegment(mp(p.x - dx, p.y - dy), mp(p.x + dx, p.y + dy),
                                    std::max(0.6f, thick * 0.5f * s), cf);
            }
        }
    }

    void generate() {
        m_art = deco::generateArt(m_prompt, m_W, m_H, kLimits[m_limitIdx], m_over, m_variant);
        m_have = true;
        m_bgLayer->setColor(ccColor3B{ m_art.bg.r, m_art.bg.g, m_art.bg.b });
        redraw();
        refreshLabels();
        setLine(4, "Status: drawn, press Keep to place it");
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
        m_selLabel = makeLabel(root, sel, 20.f, 236.f, 0.55f, true);
        makeLabel(root, "Type what to draw on the selection.", 20.f, 218.f, 0.45f, true);
        float ly = 196.f;
        for (int i = 0; i < 5; ++i) {
            m_lines[i] = makeLabel(root, "", 20.f, ly, 0.5f, true);
            ly -= 18.f;
        }
        setLine(0, "Prompt: -");
        setLine(1, "Theme: -");
        setLine(2, "Result: 0 objects");
        setLine(3, "Variant 1  |  Mode: Inside");
        setLine(4, "Status: ready");

        m_input = TextInput::create(150.f, "Describe what to draw...", "chatFont.fnt");
        m_input->setMaxCharCount(80);
        m_input->setPosition({ 100.f, 58.f });
        root->addChild(m_input);

        // ---- right column: preview ----
        auto holder = CCNode::create();
        holder->setContentSize({ kPW, kPH });
        holder->setAnchorPoint({ 0.f, 0.f });
        holder->setPosition({ 225.f, 130.f });
        root->addChild(holder);
        m_bgLayer = CCLayerColor::create(ccColor4B{ 30, 60, 120, 255 }, kPW, kPH);
        holder->addChild(m_bgLayer);
        m_draw = CCDrawNode::create();
        holder->addChild(m_draw, 1);

        m_countLabel = makeLabel(root, "0 / 500 objects", 325.f, 118.f, 0.5f, false);
        m_limitLabel = makeLabel(root, "Limit 500", 325.f, 96.f, 0.55f, false);

        // ---- buttons ----
        auto menu = CCMenu::create();
        menu->setContentSize({ 440.f, 280.f });
        menu->setPosition({ 0.f, 0.f });
        root->addChild(menu);

        auto addBtn = [&](char const* text, float scale, float x, float y, SEL_MenuHandler cb) {
            auto spr = ButtonSprite::create(text);
            spr->setScale(scale);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
            btn->setPosition({ x, y });
            menu->addChild(btn);
        };
        addBtn("Go", 0.5f, 196.f, 58.f, menu_selector(ArtAgentPopup::onGo));
        addBtn("Undo", 0.45f, 50.f, 96.f, menu_selector(ArtAgentPopup::onUndo));
        addBtn("<", 0.5f, 250.f, 96.f, menu_selector(ArtAgentPopup::onLimitDown));
        addBtn(">", 0.5f, 400.f, 96.f, menu_selector(ArtAgentPopup::onLimitUp));
        addBtn("Inside", 0.45f, 290.f, 70.f, menu_selector(ArtAgentPopup::onInside));
        addBtn("Over", 0.45f, 360.f, 70.f, menu_selector(ArtAgentPopup::onOver));
        addBtn("Another variant", 0.4f, 280.f, 38.f, menu_selector(ArtAgentPopup::onVariant));
        addBtn("Keep", 0.5f, 385.f, 38.f, menu_selector(ArtAgentPopup::onKeep));

        refreshLabels();
        setLine(0, "Prompt: -");
        setLine(1, "Theme: -");
        return true;
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
        setLine(4, "Status: kept (" + std::to_string(m_art.prims.size()) + " objects)");
        Notification::create("Kept " + std::to_string(m_art.prims.size()) + " objects",
                             NotificationIcon::Success)->show();
    }

    void onUndo(CCObject*) {
        removeLastBatch(m_ui);
        setLine(4, "Status: undone");
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
        if (m_have) generate(); else refreshLabels();
    }

    void onOver(CCObject*) {
        m_over = true;
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
// Editor button
// ---------------------------------------------------------------------------
class $modify(AutoDecoEditorUI, EditorUI) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;

        g_lastBatch.clear();

        auto spr = ButtonSprite::create("Deco");
        spr->setScale(0.6f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(AutoDecoEditorUI::onAutoDeco));
        btn->setID("auto-deco-button"_spr);

        CCNode* menu = this->getChildByID("undo-menu");
        if (menu) {
            menu->addChild(btn);
            menu->updateLayout();
        }
        else {
            // fallback: own menu in the top-left corner
            auto own = CCMenu::create();
            auto win = CCDirector::get()->getWinSize();
            own->setPosition({ 40.f, win.height - 60.f });
            own->addChild(btn);
            this->addChild(own);
        }
        return true;
    }

    void onAutoDeco(CCObject*) {
        if (auto popup = ArtAgentPopup::create(this)) {
            popup->show();
        }
    }
};
