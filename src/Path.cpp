// Pathfinder: steps a dummy copy of the player forward twice - once holding, once released - and draws both
// trajectories (green = hold, red = release). Recomputed every frame and drawn tick by tick, so it is smooth.
//
// The dummy is an exact copy of the real player (every physics field, see PlayerState.hpp) and uses the game's
// own collision, so it reacts to blocks, slopes, pads, orbs, speed portals and gravity portals like the real
// player would. Things that would change the REAL level are switched off for the moment of the preview:
//   - gamemode / dual / mirror portals (they move the ground, split the screen or flip it) - the line ends there,
//   - coins, keys and touch triggers (they would be collected / fired),
//   - breakable blocks act as normal blocks (they would break).
// Objects' "activated" flags and the level's last-portal memory are saved before and put back after, and the
// dummy plays no effects or sounds.
//
// GD's physics works in "60 fps frame" units: one 240 TPS tick is dt = 0.25.
#include "Common.hpp"
#include "PlayerState.hpp"
#include <algorithm>

using namespace geode::prelude;

namespace kt::path {

namespace {
    constexpr float kTick = 0.25f;
    bool g_sim = false;
    bool g_simDead = false;
    PlayerObject* g_fake = nullptr;
    PlayLayer* g_owner = nullptr;
    CCDrawNode* g_node = nullptr;
    std::vector<CCPoint> g_hold, g_free;

    // every object that is not a plain block / slope / spike / decoration, sorted by x
    std::vector<GameObject*> g_inter;
    unsigned g_count = 0;
    bool g_built = false;

    // Everything the preview can change on a real object. Orbs, pads and portals remember that they were used
    // (m_activated / m_activatedByPlayer1/2); before, only m_isActivated was put back, so after the preview had
    // touched an orb or pad the real player could not use it any more and the level state was off.
    struct Saved {
        GameObject* obj;
        EnhancedGameObject* enh;      // null for plain objects
        GameObjectType type;
        bool activated, disabled, disabled2;
        bool enhActivated, byP1, byP2;
    };
    std::vector<Saved> g_saved;
    std::vector<GameObject*> g_enders;      // gamemode / dual / mirror portals inside the preview range

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

    // portals that change the whole level (ground position, dual screen, mirror): the preview ends there
    bool endsPreview(int id) {
        switch (id) {
            case 12: case 13: case 47: case 111: case 660: case 745: case 1331: case 1933:   // gamemodes
            case 286: case 287:                                                              // dual
            case 45: case 46:                                                                // mirror
                return true;
            default: return false;
        }
    }
    // objects the dummy must never touch at all
    bool untouchable(GameObject* o) {
        if (o->m_isTrigger) return true;                                  // touch triggers would fire
        switch (o->m_objectID) {
            case 142: case 1329:                                          // secret / user coins
            case 1275: case 1587: case 1588: case 1589: case 1598: case 1614:   // keys / collectibles
            case 1594:                                                    // toggle orb (switches groups on/off)
                return true;
            default: return endsPreview(o->m_objectID);
        }
    }

    void protect(float x0, float x1) {
        g_saved.clear();
        g_enders.clear();
        auto it = std::lower_bound(g_inter.begin(), g_inter.end(), x0,
                                   [](GameObject* o, float x) { return o->getPositionX() < x; });
        for (; it != g_inter.end() && (*it)->getPositionX() <= x1; ++it) {
            auto o = *it;
            Saved sv{o, typeinfo_cast<EnhancedGameObject*>(o), o->m_objectType, o->m_isActivated, o->m_isDisabled,
                     o->m_isDisabled2, false, false, false};
            if (sv.enh) {
                sv.enhActivated = sv.enh->m_activated;
                sv.byP1 = sv.enh->m_activatedByPlayer1;
                sv.byP2 = sv.enh->m_activatedByPlayer2;
            }
            g_saved.push_back(sv);
            if (o->m_objectType == GameObjectType::Breakable) o->m_objectType = GameObjectType::Solid;
            else if (untouchable(o)) o->m_objectType = GameObjectType::Decoration;
            if (endsPreview(o->m_objectID)) g_enders.push_back(o);
        }
    }
    void unprotect() {
        for (auto& s : g_saved) {
            s.obj->m_objectType = s.type;
            s.obj->m_isActivated = s.activated;
            s.obj->m_isDisabled = s.disabled;
            s.obj->m_isDisabled2 = s.disabled2;
            if (s.enh) {
                s.enh->m_activated = s.enhActivated;
                s.enh->m_activatedByPlayer1 = s.byP1;
                s.enh->m_activatedByPlayer2 = s.byP2;
            }
        }
        g_saved.clear();
    }

    bool hitsEnder() {
        if (g_enders.empty()) return false;
        auto r = g_fake->getObjectRect();
        for (auto o : g_enders) {
            if (r.intersectsRect(o->getObjectRect())) return true;
        }
        return false;
    }

    void run(PlayLayer* pl, PlayerObject* real, bool hold, int steps, std::vector<CCPoint>& pts) {
        pts.clear();
        g_fake->copyAttributes(real);
        copyPlayer(g_fake, real);              // exact physics copy of the real player
        g_fake->m_playEffects = false;         // no particles / sounds from the dummy
        g_fake->setVisible(false);

        g_sim = true;
        g_simDead = false;
        // g_sim must never stay on (the hitboxes stop being redrawn while it is on), whatever happens inside
        struct SimOff { ~SimOff() { g_sim = false; } } simOff;
        if (hold) g_fake->pushButton(PlayerButton::Jump);
        else g_fake->releaseButton(PlayerButton::Jump);
        pts.push_back(g_fake->getPosition());
        for (int i = 0; i < steps && !g_simDead; ++i) {
            g_fake->update(kTick);
            pl->checkCollisions(g_fake, kTick, false);
            pts.push_back(g_fake->getPosition());
            if (hitsEnder()) break;
        }
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
    g_owner = nullptr;
    g_node = nullptr;          // belongs to the old level
    g_inter.clear();
    g_count = 0;
    g_built = false;
    g_hold.clear();
    g_free.clear();
    g_sim = false;
}

void forget() {
    g_fake = nullptr;          // its level was destroyed without telling us: leaking one dummy beats a crash
    reset();
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
    }
    if (!g_node) {
        g_node = CCDrawNode::create();
        pl->m_objectLayer->addChild(g_node, 9998);
    }
    if (!g_node->isVisible()) g_node->setVisible(true);

    ensureCache(pl);
    int steps = std::max(10, c.pathLen) * 4;   // length is in 60 fps frames -> 4 ticks each
    float px = pl->m_player1->getPositionX();
    // what the level remembers about the last portal is restored too
    auto lastPortal1 = pl->m_gameState.m_lastActivatedPortal1;
    auto lastPortal2 = pl->m_gameState.m_lastActivatedPortal2;
    protect(px - 150.f, px + steps * kTick * 12.f + 150.f);
    run(pl, pl->m_player1, true, steps, g_hold);
    run(pl, pl->m_player1, false, steps, g_free);
    unprotect();
    pl->m_gameState.m_lastActivatedPortal1 = lastPortal1;
    pl->m_gameState.m_lastActivatedPortal2 = lastPortal2;
    draw();
}

} // namespace kt::path
