// Pathfinder: steps a dummy copy of the player forward twice - once holding, once released - and draws both
// trajectories (green = hold, red = release).
//
// Why portals / pads / orbs / speed acted strangely before: the dummy used the game's own collision check, so
// when its preview touched a portal, pad, orb or speed portal, the REAL level activated it (sound, effect, and
// the object counted as "used"). When the real player got there, it was already used up.
// Now, while the dummy is simulated, every object except blocks, slopes and spikes is switched off for a
// moment (and switched back right after), so the preview can never touch the level. Speed and gravity portals
// are applied to the dummy by this file itself; at a gamemode / dual / teleport portal the preview line ends.
//
// GD's physics works in "60 fps frame" units: one 240 TPS tick is dt = 0.25.
// Performance: the preview is recomputed every 3rd frame (drawn in between), and only every 2nd tick is drawn.
#include "Common.hpp"
#include <algorithm>

using namespace geode::prelude;

namespace kt::path {

namespace {
    constexpr float kTick = 0.25f;
    constexpr int kEvery = 3;
    bool g_sim = false;
    bool g_simDead = false;
    PlayerObject* g_fake = nullptr;
    PlayerCheckpoint* g_cp = nullptr;
    PlayLayer* g_owner = nullptr;
    CCDrawNode* g_node = nullptr;
    int g_frame = 0;
    std::vector<CCPoint> g_hold, g_free;

    // objects the dummy must never activate (portals, pads, orbs, coins, triggers...), sorted by x
    std::vector<GameObject*> g_inter;
    unsigned g_count = 0;
    bool g_built = false;

    struct Swap {
        GameObject* obj;
        GameObjectType type;
    };
    std::vector<Swap> g_swapped;
    std::vector<GameObject*> g_portals;     // speed / gravity / mode portals inside the preview range
    std::vector<char> g_used;

    bool isBlock(GameObjectType t) {
        switch (t) {
            case GameObjectType::Solid:
            case GameObjectType::Slope:
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard:
            case GameObjectType::Decoration: return true;
            default: return false;
        }
    }

    void ensureCache(PlayLayer* pl) {
        unsigned n = pl->m_objects ? pl->m_objects->count() : 0;
        if (g_built && n == g_count) return;
        g_built = true;
        g_count = n;
        g_inter.clear();
        if (!pl->m_objects) return;
        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (obj && !isBlock(obj->m_objectType)) g_inter.push_back(obj);
        }
        std::sort(g_inter.begin(), g_inter.end(),
                  [](GameObject* a, GameObject* b) { return a->getPositionX() < b->getPositionX(); });
    }

    enum class Portal { None, Speed, Gravity, End };
    Portal portalKind(int id) {
        switch (id) {
            case 200: case 201: case 202: case 203: case 1334: return Portal::Speed;
            case 10: case 11: case 2926: return Portal::Gravity;
            case 12: case 13: case 47: case 111: case 660: case 745: case 1331: case 1933:   // gamemodes
            case 286: case 287:                                                              // dual
            case 747: return Portal::End;                                                    // teleport
            default: return Portal::None;
        }
    }
    float speedOf(int id) {
        switch (id) {
            case 200: return 0.7f;
            case 202: return 1.1f;
            case 203: return 1.3f;
            case 1334: return 1.6f;
            default: return 0.9f;
        }
    }

    // switch off everything the dummy could activate between x0 and x1
    void neutralize(float x0, float x1) {
        g_swapped.clear();
        g_portals.clear();
        auto it = std::lower_bound(g_inter.begin(), g_inter.end(), x0,
                                   [](GameObject* o, float x) { return o->getPositionX() < x; });
        for (; it != g_inter.end() && (*it)->getPositionX() <= x1; ++it) {
            auto o = *it;
            g_swapped.push_back({o, o->m_objectType});
            // breakable blocks still stop the dummy like a block, but can never break
            o->m_objectType = o->m_objectType == GameObjectType::Breakable ? GameObjectType::Solid : GameObjectType::Decoration;
            if (portalKind(o->m_objectID) != Portal::None) g_portals.push_back(o);
        }
    }
    void restore() {
        for (auto& s : g_swapped) s.obj->m_objectType = s.type;
        g_swapped.clear();
    }

    // returns false when the preview has to end here
    bool touchPortals() {
        if (g_portals.empty()) return true;
        auto r = g_fake->getObjectRect();
        for (size_t i = 0; i < g_portals.size(); ++i) {
            if (g_used[i]) continue;
            auto o = g_portals[i];
            if (!r.intersectsRect(o->getObjectRect())) continue;
            g_used[i] = 1;
            int id = o->m_objectID;
            switch (portalKind(id)) {
                case Portal::Speed: g_fake->m_playerSpeed = speedOf(id); break;
                case Portal::Gravity: {
                    bool flip = id == 11 ? true : id == 10 ? false : !g_fake->m_isUpsideDown;
                    if (flip != g_fake->m_isUpsideDown) g_fake->flipGravity(flip, true);
                    break;
                }
                case Portal::End: return false;
                default: break;
            }
        }
        return true;
    }

    void run(PlayLayer* pl, PlayerObject* real, bool hold, int steps, std::vector<CCPoint>& pts) {
        pts.clear();
        g_fake->copyAttributes(real);
        real->saveToCheckpoint(g_cp);
        g_fake->loadFromCheckpoint(g_cp);
        g_fake->m_playerSpeed = real->m_playerSpeed;      // speed portals already passed: same speed as you
        g_fake->m_isPlatformer = real->m_isPlatformer;
        g_fake->setVisible(false);
        g_used.assign(g_portals.size(), 0);

        g_sim = true;
        g_simDead = false;
        if (hold) g_fake->pushButton(PlayerButton::Jump);
        else g_fake->releaseButton(PlayerButton::Jump);
        pts.push_back(g_fake->getPosition());
        for (int i = 0; i < steps && !g_simDead; ++i) {
            g_fake->update(kTick);
            pl->checkCollisions(g_fake, kTick, false);
            bool go = touchPortals();
            if (i % 2 == 1 || !go || g_simDead) pts.push_back(g_fake->getPosition());
            if (!go) break;
        }
        g_sim = false;
    }

    void draw() {
        g_node->clear();
        auto line = [](std::vector<CCPoint> const& v, ccColor4F col) {
            for (size_t i = 1; i < v.size(); ++i) g_node->drawSegment(v[i - 1], v[i], 0.9f, col);
        };
        line(g_hold, {0.3f, 1.f, 0.5f, 0.9f});
        line(g_free, {1.f, 0.35f, 0.35f, 0.9f});
    }
}

bool simulating() { return g_sim; }

// Only real obstacles end the preview.
void onSimDeath(GameObject* obj) {
    if (obj) {
        switch (obj->m_objectType) {
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard:
            case GameObjectType::Solid:
            case GameObjectType::Slope:
            case GameObjectType::Breakable: break;
            default: return;
        }
    }
    g_simDead = true;
}

void reset() {
    if (g_fake) {
        g_fake->release();
        g_fake = nullptr;
    }
    if (g_cp) {
        g_cp->release();
        g_cp = nullptr;
    }
    g_owner = nullptr;
    g_node = nullptr;          // belongs to the old level
    g_inter.clear();
    g_count = 0;
    g_built = false;
    g_hold.clear();
    g_free.clear();
    g_sim = false;
}

void update(PlayLayer* pl) {
    auto& c = Cfg::get();
    bool on = c.pathfinder && pl->m_player1 && !pl->m_player1->m_isDead && !pl->m_isPaused && !counter::isCounting();
    if (!on) {
        if (g_node && g_node->isVisible()) g_node->setVisible(false);
        return;
    }

    if (g_owner != pl || !g_fake) {
        reset();
        g_owner = pl;
        g_fake = PlayerObject::create(1, 1, pl, pl->m_objectLayer, false);
        if (!g_fake) return;
        g_fake->retain();
        g_fake->setVisible(false);
        g_cp = PlayerCheckpoint::create();
        g_cp->retain();
    }
    if (!g_node) {
        g_node = CCDrawNode::create();
        pl->m_objectLayer->addChild(g_node, 9998);
    }
    if (!g_node->isVisible()) g_node->setVisible(true);

    // recompute every 3rd frame only (in between the last result stays on screen)
    if (++g_frame % kEvery != 0 && !g_hold.empty()) return;

    ensureCache(pl);
    int steps = std::max(10, c.pathLen) * 4;   // length is in 60 fps frames -> 4 ticks each
    float px = pl->m_player1->getPositionX();
    neutralize(px - 150.f, px + steps * kTick * 12.f + 150.f);
    run(pl, pl->m_player1, true, steps, g_hold);
    run(pl, pl->m_player1, false, steps, g_free);
    restore();
    draw();
}

} // namespace kt::path
