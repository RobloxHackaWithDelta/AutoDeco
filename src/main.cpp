// Auto Deco - single-file Geode mod (Geode v5).
// One codebase builds for Windows, Android, macOS and iOS. Geode packs all
// platform binaries into ONE .geode file and picks the right one at runtime,
// so there is no "which platform are you on" question needed.

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <random>
#include <vector>
#include <string>
#include <cmath>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Platform name (shown in the popup title, handy to confirm which build runs)
// ---------------------------------------------------------------------------
static const char* platformName() {
#if defined(GEODE_IS_WINDOWS)
    return "Windows";
#elif defined(GEODE_IS_ANDROID)
    return "Android";
#elif defined(GEODE_IS_IOS)
    return "iOS";
#elif defined(GEODE_IS_MACOS)
    return "macOS";
#else
    return "Unknown";
#endif
}

// ---------------------------------------------------------------------------
// Style presets. Edit / add your own here, this is the only part you need to
// touch to add more styles.
//   Scatter = random placement over the screen
//   Grid    = evenly spaced cells with a little jitter
//   Rays    = thin bars radiating out from the screen center
// ---------------------------------------------------------------------------
enum class Layout { Scatter, Grid, Rays };

struct StylePreset {
    std::string name;
    Layout layout;
    std::vector<int> ids;            // object IDs used as deco shapes
    std::vector<ccColor3B> palette;  // object colors (one color channel each)
    ccColor3B bg;                    // background color
    int count;                       // objects per screen
    float minScale;
    float maxScale;
    float stretch;                   // x scale multiplier (1 = square-ish)
    float baseRot;                   // fixed rotation in degrees
    bool spinny;                     // random rotation instead of baseRot
};

static ccColor3B C(int r, int g, int b) {
    return ccColor3B{ (GLubyte)r, (GLubyte)g, (GLubyte)b };
}

// NOTE: these palettes are my approximation of each level's vibe, not exact.
static std::vector<StylePreset> const& getPresets() {
    static std::vector<StylePreset> presets = {
        { "Thinking Space 2", Layout::Scatter, { 1 },
          { C(120,170,255), C(190,140,255), C(90,230,255), C(255,255,255) },
          C(10,12,40), 90, 0.12f, 0.45f, 1.f, 45.f, false },

        { "Angelicide", Layout::Rays, { 40 },
          { C(255,250,225), C(255,225,120), C(255,255,255) },
          C(60,45,20), 36, 0.15f, 0.40f, 6.f, 0.f, false },

        { "Society", Layout::Grid, { 1 },
          { C(220,40,50), C(30,30,35), C(140,140,150) },
          C(18,18,22), 28, 0.5f, 1.0f, 1.f, 0.f, false },

        { "Neon", Layout::Scatter, { 1 },
          { C(255,40,180), C(40,240,255), C(170,255,60) },
          C(5,5,15), 60, 0.2f, 0.8f, 1.f, 45.f, false },

        { "Spooky", Layout::Scatter, { 1, 40 },
          { C(255,120,20), C(140,60,200), C(90,200,60) },
          C(12,6,20), 50, 0.2f, 0.9f, 1.f, 0.f, true },

        { "Minimal", Layout::Grid, { 1 },
          { C(70,70,80), C(150,150,160) },
          C(235,235,240), 12, 0.6f, 1.4f, 1.f, 0.f, false },

        { "Sunset Glow", Layout::Rays, { 40 },
          { C(255,150,60), C(255,80,140), C(170,80,255) },
          C(40,10,50), 30, 0.15f, 0.40f, 8.f, 0.f, false },

        { "Deep Ocean", Layout::Scatter, { 1, 40 },
          { C(20,160,200), C(40,220,200), C(10,90,160) },
          C(4,20,40), 55, 0.15f, 0.70f, 1.f, 0.f, false },
    };
    return presets;
}

// ---------------------------------------------------------------------------
// Decorating logic
// ---------------------------------------------------------------------------
static std::vector<Ref<GameObject>> g_lastBatch;

static void placeColorTrigger(LevelEditorLayer* lel, int channel, ccColor3B col, CCPoint pos) {
    auto obj = lel->createObject(899, pos, false);
    if (!obj) return;
    if (auto trig = typeinfo_cast<EffectGameObject*>(obj)) {
        trig->m_triggerTargetColor = col;
        trig->m_targetColorID = channel;
        trig->m_duration = 0.f;
    }
    g_lastBatch.push_back(obj);
}

static void placeDeco(LevelEditorLayer* lel, int id, CCPoint pos, float rot,
                      float sx, float sy, int channel, ccColor3B col) {
    auto obj = lel->createObject(id, pos, false);
    if (!obj) return;
    obj->m_isNoTouch = true;  // decoration only, never blocks the player
    obj->setRotation(rot);
    obj->updateCustomScaleX(sx);
    obj->updateCustomScaleY(sy);
    if (obj->m_baseColor) obj->m_baseColor->m_colorID = channel;
    obj->setObjectColor(col);
    g_lastBatch.push_back(obj);
}

static void applyPreset(EditorUI* ui, StylePreset const& p) {
    auto lel = ui->m_editorLayer;
    if (!lel || !lel->m_objectLayer) return;

    // Visible area of the editor in object coordinates
    auto layer = lel->m_objectLayer;
    float scale = layer->getScale();
    if (scale <= 0.f) scale = 1.f;
    auto win = CCDirector::get()->getWinSize();
    float x0 = -layer->getPositionX() / scale;
    float y0 = -layer->getPositionY() / scale;
    float w = win.width / scale;
    float h = win.height / scale;

    std::mt19937 rng{ std::random_device{}() };
    auto rnd = [&](float a, float b) {
        return std::uniform_real_distribution<float>(a, b)(rng);
    };
    auto pick = [&](int n) {
        return (int)std::uniform_int_distribution<int>(0, n - 1)(rng);
    };

    g_lastBatch.clear();

    // Color triggers: channel 1000 is the background, 101+ are our palette
    placeColorTrigger(lel, 1000, p.bg, CCPoint(x0, y0 + h));
    for (int i = 0; i < (int)p.palette.size(); ++i) {
        placeColorTrigger(lel, 101 + i, p.palette[i], CCPoint(x0, y0 + h + 30.f * (i + 1)));
    }

    auto makeOne = [&](CCPoint pos, float rot) {
        int id = p.ids[pick((int)p.ids.size())];
        int ci = pick((int)p.palette.size());
        float s = rnd(p.minScale, p.maxScale);
        placeDeco(lel, id, pos, rot, s * p.stretch, s, 101 + ci, p.palette[ci]);
    };

    if (p.layout == Layout::Scatter) {
        for (int n = 0; n < p.count; ++n) {
            CCPoint pos(rnd(x0, x0 + w), rnd(y0, y0 + h));
            float rot = p.spinny ? rnd(0.f, 360.f) : p.baseRot;
            makeOne(pos, rot);
        }
    }
    else if (p.layout == Layout::Grid) {
        int cols = std::max(1, (int)std::round(std::sqrt(p.count * w / h)));
        int rows = std::max(1, (int)std::ceil((float)p.count / cols));
        float cw = w / cols;
        float ch = h / rows;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                CCPoint pos(x0 + cw * (c + 0.5f) + rnd(-cw * 0.15f, cw * 0.15f),
                            y0 + ch * (r + 0.5f) + rnd(-ch * 0.15f, ch * 0.15f));
                float rot = p.spinny ? rnd(0.f, 360.f) : p.baseRot;
                makeOne(pos, rot);
            }
        }
    }
    else { // Rays
        CCPoint center(x0 + w * 0.5f, y0 + h * 0.5f);
        for (int n = 0; n < p.count; ++n) {
            float ang = rnd(0.f, 6.2831853f);
            float dist = rnd(0.f, w * 0.5f);
            CCPoint pos(center.x + std::cos(ang) * dist, center.y + std::sin(ang) * dist);
            float rotDeg = -ang * 57.29578f;
            makeOne(pos, rotDeg);
        }
    }

    Notification::create("Applied: " + p.name, NotificationIcon::Success)->show();
}

static void removeLastBatch(EditorUI* ui) {
    auto lel = ui->m_editorLayer;
    if (!lel) return;
    for (auto& o : g_lastBatch) {
        if (o && o->getParent()) lel->removeObject(o.data(), true);
    }
    g_lastBatch.clear();
    Notification::create("Removed last batch", NotificationIcon::Success)->show();
}

// ---------------------------------------------------------------------------
// Popup with one button per preset
// ---------------------------------------------------------------------------
class AutoDecoPopup : public Popup {
protected:
    EditorUI* m_ui = nullptr;

    bool init(EditorUI* ui) {
        if (!Popup::init(360.f, 240.f)) return false;
        m_ui = ui;
        this->setTitle(std::string("Auto Deco (") + platformName() + ")");

        auto menu = CCMenu::create();
        menu->setContentSize({ 330.f, 140.f });
        menu->setLayout(
            RowLayout::create()
                ->setGrowCrossAxis(true)
                ->setCrossAxisOverflow(false)
                ->setGap(6.f)
        );

        auto const& presets = getPresets();
        for (int i = 0; i < (int)presets.size(); ++i) {
            auto spr = ButtonSprite::create(presets[i].name.c_str());
            spr->setScale(0.5f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(AutoDecoPopup::onPreset));
            btn->setTag(i);
            menu->addChild(btn);
        }
        menu->updateLayout();
        m_mainLayer->addChildAtPosition(menu, Anchor::Center, { 0.f, 8.f });

        auto removeMenu = CCMenu::create();
        auto removeSpr = ButtonSprite::create(
            "Remove last batch", 150, true, "bigFont.fnt", "GJ_button_06.png", 30.f, 0.6f);
        auto removeBtn = CCMenuItemSpriteExtra::create(
            removeSpr, this, menu_selector(AutoDecoPopup::onRemove));
        removeMenu->addChild(removeBtn);
        m_mainLayer->addChildAtPosition(removeMenu, Anchor::Bottom, { 0.f, 28.f });

        return true;
    }

    void onPreset(CCObject* sender) {
        int idx = static_cast<CCNode*>(sender)->getTag();
        auto const& presets = getPresets();
        if (idx >= 0 && idx < (int)presets.size()) {
            applyPreset(m_ui, presets[idx]);
        }
        this->onClose(nullptr);
    }

    void onRemove(CCObject*) {
        removeLastBatch(m_ui);
        this->onClose(nullptr);
    }

public:
    static AutoDecoPopup* create(EditorUI* ui) {
        auto ret = new AutoDecoPopup();
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
        if (auto popup = AutoDecoPopup::create(this)) {
            popup->show();
        }
    }
};
