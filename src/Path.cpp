// Pathfinder (EXPERIMENTAL): steps a dummy copy of the player forward twice - once holding, once released -
// and draws both trajectories (green = hold, red = release).
//
// GD's physics works in "60 fps frame" units: one 240 TPS tick is dt = 0.25. Using real seconds (1/240) made
// the dummy move 60x too slowly, which is why the path looked like it never appeared.
#include "Common.hpp"

using namespace geode::prelude;

namespace kt::path {

namespace {
    constexpr float kTick = 0.25f;
    bool g_sim = false;
    bool g_simDead = false;
    PlayerObject* g_fake = nullptr;
    PlayLayer* g_owner = nullptr;
    constexpr char const* kNodeId = "kt-path-node";

    std::vector<CCPoint> run(PlayLayer* pl, PlayerObject* real, bool hold, int steps) {
        std::vector<CCPoint> pts;
        g_fake->copyAttributes(real);
        auto cp = PlayerCheckpoint::create();
        real->saveToCheckpoint(cp);
        g_fake->loadFromCheckpoint(cp);
        g_fake->m_playerSpeed = real->m_playerSpeed;      // speed portals: the dummy moves as fast as you
        g_fake->m_isPlatformer = real->m_isPlatformer;
        g_fake->setVisible(false);

        g_sim = true;
        g_simDead = false;
        if (hold) g_fake->pushButton(PlayerButton::Jump);
        else g_fake->releaseButton(PlayerButton::Jump);
        pts.push_back(g_fake->getPosition());
        for (int i = 0; i < steps && !g_simDead; ++i) {
            g_fake->update(kTick);
            pl->checkCollisions(g_fake, kTick, false);
            pts.push_back(g_fake->getPosition());
        }
        g_sim = false;
        return pts;
    }
}

bool simulating() { return g_sim; }
// Only real obstacles end the preview. Speed portals and other special objects can report a "death" for the
// dummy too, which used to stop the path right at the portal so nothing was visible.
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
    g_sim = false;
}

void update(PlayLayer* pl) {
    auto node = static_cast<CCDrawNode*>(pl->m_objectLayer->getChildByID(kNodeId));
    if (!node) {
        node = CCDrawNode::create();
        node->setID(kNodeId);
        pl->m_objectLayer->addChild(node, 9998);
    }
    node->clear();
    auto& c = Cfg::get();
    if (!c.pathfinder || !pl->m_player1 || pl->m_player1->m_isDead || pl->m_isPaused) return;
    if (kt::counter::isCounting()) return;

    if (g_owner != pl || !g_fake) {
        reset();
        g_owner = pl;
        g_fake = PlayerObject::create(1, 1, pl, pl->m_objectLayer, false);
        if (!g_fake) return;
        g_fake->retain();
        g_fake->setVisible(false);
    }

    int n = std::max(10, c.pathLen) * 4;   // length is in 60 fps frames -> 4 ticks each
    auto holdPts = run(pl, pl->m_player1, true, n);
    auto freePts = run(pl, pl->m_player1, false, n);
    auto drawLine = [&](std::vector<CCPoint> const& v, ccColor4F col) {
        for (size_t i = 1; i < v.size(); ++i) node->drawSegment(v[i - 1], v[i], 0.9f, col);
    };
    drawLine(holdPts, {0.3f, 1.f, 0.5f, 0.9f});
    drawLine(freePts, {1.f, 0.35f, 0.35f, 0.9f});
}

} // namespace kt::path
