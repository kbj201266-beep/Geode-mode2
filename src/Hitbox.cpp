#include "Common.hpp"
#include <deque>

using namespace geode::prelude;

namespace kt::hitbox {

namespace {
    std::deque<CCRect> g_trail1, g_trail2;
    constexpr char const* kNodeId = "kt-hitbox-node";

    CCDrawNode* getNode(PlayLayer* pl) {
        auto parent = pl->m_objectLayer;
        auto node = static_cast<CCDrawNode*>(parent->getChildByID(kNodeId));
        if (!node) {
            node = CCDrawNode::create();
            node->setID(kNodeId);
            parent->addChild(node, 9999);
        }
        return node;
    }

    void poly(CCDrawNode* n, std::vector<CCPoint> const& v, ccColor3B c, bool fill, float alpha = 1.f) {
        auto& cfg = Cfg::get();
        ccColor4F border{c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha};
        ccColor4F fillC{border.r, border.g, border.b, fill ? cfg.hitFill * alpha : 0.f};
        n->drawPolygon(const_cast<CCPoint*>(v.data()), static_cast<unsigned>(v.size()), fillC, cfg.hitBorder, border);
    }

    std::vector<CCPoint> corners(CCRect const& r) {
        return {{r.getMinX(), r.getMinY()}, {r.getMaxX(), r.getMinY()}, {r.getMaxX(), r.getMaxY()}, {r.getMinX(), r.getMaxY()}};
    }

    void drawObject(CCDrawNode* n, GameObject* o) {
        ccColor3B col;
        switch (o->m_objectType) {
            case GameObjectType::Hazard: col = {255, 60, 60}; break;
            case GameObjectType::Solid:
            case GameObjectType::Slope: col = {70, 130, 255}; break;
            case GameObjectType::Decoration:
            case GameObjectType::Modifier: return;
            default: col = {80, 255, 120}; break;      // portals, pads, orbs, coins...
        }
        // Rotated objects carry an oriented box; everything else is an axis aligned rect.
        if (o->m_orientedBox) {
            auto b = o->m_orientedBox;
            poly(n, {b->m_p1, b->m_p2, b->m_p3, b->m_p4}, col, true);
        } else {
            poly(n, corners(o->getObjectRect()), col, true);
        }
    }

    void drawPlayer(CCDrawNode* n, PlayerObject* p) {
        if (!p) return;
        auto outer = p->getObjectRect();
        poly(n, corners(outer), {255, 215, 60}, true);
        // inner hitbox is a quarter-size box in the middle
        float w = outer.size.width * 0.25f, h = outer.size.height * 0.25f;
        CCRect inner{outer.getMidX() - w / 2, outer.getMidY() - h / 2, w, h};
        poly(n, corners(inner), {60, 220, 255}, true);
    }

    void drawTrail(CCDrawNode* n, std::deque<CCRect> const& t, ccColor3B col) {
        size_t i = 0;
        for (auto const& r : t) {
            float a = 0.10f + 0.5f * (static_cast<float>(++i) / static_cast<float>(t.size()));
            poly(n, corners(r), col, false, a);
        }
    }

    void push(std::deque<CCRect>& t, PlayerObject* p) {
        if (!p) return;
        t.push_back(p->getObjectRect());
        while (static_cast<int>(t.size()) > std::max(1, Cfg::get().trailLen)) t.pop_front();
    }
}

void reset() {
    g_trail1.clear();
    g_trail2.clear();
}

void update(PlayLayer* pl) {
    auto& c = Cfg::get();
    auto node = getNode(pl);
    node->clear();
    if (!c.hitboxes && !c.hitboxTrail) return;
    if (path::simulating()) return;

    if (c.hitboxTrail) {
        push(g_trail1, pl->m_player1);
        push(g_trail2, pl->m_gameState.m_isDualMode ? pl->m_player2 : nullptr);
        drawTrail(node, g_trail1, {255, 215, 60});
        drawTrail(node, g_trail2, {255, 140, 60});
    }
    if (!c.hitboxes) return;

    // only objects near the player are drawn (cheap culling)
    float range = CCDirector::get()->getWinSize().width * 1.2f;
    float px = pl->m_player1->getPositionX();
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!obj || std::abs(obj->getPositionX() - px) > range) continue;
        drawObject(node, obj);
    }
    if (c.playerHitbox) {
        drawPlayer(node, pl->m_player1);
        if (pl->m_gameState.m_isDualMode) drawPlayer(node, pl->m_player2);
    }
}

} // namespace kt::hitbox
