// Hitboxes, hitbox trail, and Hitborders (NaN style "accurate gaps").
//
// Hitborders: instead of drawing the player's box, every object is grown by the player's size, so only the
// player's CENTER (yellow dot) matters:
//   red   = hazards grown by half the outer player box + solids grown by half the inner box  -> center in red = death
//   blue  = solids grown by half the outer player box                                         -> passable / landable
//   green = pads, orbs, portals grown by half the outer box
// Overlapping boxes of one color are merged, so only the outline of the whole area is drawn.
#include "Common.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

using namespace geode::prelude;

namespace kt::hitbox {

namespace {
    constexpr float kInner = 0.3f;     // inner (solid-death) hitbox = 30% of the outer box
    std::deque<CCRect> g_trail1, g_trail2;
    std::deque<CCPoint> g_center1, g_center2;
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

    CCRect innerOf(CCRect const& outer) {
        float w = outer.size.width * kInner, h = outer.size.height * kInner;
        return {outer.getMidX() - w / 2, outer.getMidY() - h / 2, w, h};
    }

    // axis aligned box of an object (rotated objects use the box around their corners)
    CCRect objRect(GameObject* o) {
        if (o->m_orientedBox) {
            auto& c = o->m_orientedBox->m_corners;
            float x0 = c[0].x, x1 = c[0].x, y0 = c[0].y, y1 = c[0].y;
            for (auto& p : c) {
                x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
                y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
            }
            return {x0, y0, x1 - x0, y1 - y0};
        }
        return o->getObjectRect();
    }

    CCRect grow(CCRect const& r, float hx, float hy) {
        return {r.origin.x - hx, r.origin.y - hy, r.size.width + 2 * hx, r.size.height + 2 * hy};
    }

    enum class Kind { Skip, Hazard, Solid, Special };
    Kind kindOf(GameObject* o) {
        switch (o->m_objectType) {
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard: return Kind::Hazard;
            case GameObjectType::Solid:
            case GameObjectType::Slope:
            case GameObjectType::Breakable: return Kind::Solid;
            case GameObjectType::Decoration:
            case GameObjectType::Modifier:
            case GameObjectType::CollisionObject:
            case GameObjectType::EnterEffectObject:
            case GameObjectType::Special: return Kind::Skip;
            default: return Kind::Special;
        }
    }

    // Draws the union of many rects: one merged fill and only the outer outline.
    // Uses a compressed grid of all rect edges, so overlaps are never drawn twice.
    void drawUnion(CCDrawNode* n, std::vector<CCRect> const& rects, ccColor4F fill, ccColor4F border, float bw) {
        if (rects.empty()) return;
        std::vector<float> xs, ys;
        xs.reserve(rects.size() * 2);
        ys.reserve(rects.size() * 2);
        for (auto& r : rects) {
            xs.push_back(r.getMinX()); xs.push_back(r.getMaxX());
            ys.push_back(r.getMinY()); ys.push_back(r.getMaxY());
        }
        auto same = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        xs.erase(std::unique(xs.begin(), xs.end(), same), xs.end());
        ys.erase(std::unique(ys.begin(), ys.end(), same), ys.end());
        int nx = static_cast<int>(xs.size()) - 1, ny = static_cast<int>(ys.size()) - 1;
        if (nx <= 0 || ny <= 0) return;

        auto idx = [](std::vector<float> const& v, float x) {
            return static_cast<int>(std::lower_bound(v.begin(), v.end(), x - 0.005f) - v.begin());
        };
        std::vector<uint8_t> cov(static_cast<size_t>(nx) * ny, 0);
        auto at = [&](int i, int j) -> uint8_t& { return cov[static_cast<size_t>(j) * nx + i]; };
        for (auto& r : rects) {
            int x0 = idx(xs, r.getMinX()), x1 = idx(xs, r.getMaxX());
            int y0 = idx(ys, r.getMinY()), y1 = idx(ys, r.getMaxY());
            for (int j = y0; j < y1 && j < ny; ++j)
                for (int i = x0; i < x1 && i < nx; ++i) at(i, j) = 1;
        }

        // fill: one quad per horizontal run of covered cells
        for (int j = 0; j < ny; ++j) {
            int i = 0;
            while (i < nx) {
                if (!at(i, j)) { ++i; continue; }
                int s = i;
                while (i < nx && at(i, j)) ++i;
                CCPoint q[4] = {{xs[s], ys[j]}, {xs[i], ys[j]}, {xs[i], ys[j + 1]}, {xs[s], ys[j + 1]}};
                n->drawPolygon(q, 4, fill, 0.f, fill);
            }
        }

        // outline: every edge between a covered and an uncovered cell
        if (bw <= 0.f) return;
        float rad = bw / 2.f;
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                if (!at(i, j)) continue;
                if (i == 0 || !at(i - 1, j)) n->drawSegment({xs[i], ys[j]}, {xs[i], ys[j + 1]}, rad, border);
                if (i == nx - 1 || !at(i + 1, j)) n->drawSegment({xs[i + 1], ys[j]}, {xs[i + 1], ys[j + 1]}, rad, border);
                if (j == 0 || !at(i, j - 1)) n->drawSegment({xs[i], ys[j]}, {xs[i + 1], ys[j]}, rad, border);
                if (j == ny - 1 || !at(i, j + 1)) n->drawSegment({xs[i], ys[j + 1]}, {xs[i + 1], ys[j + 1]}, rad, border);
            }
        }
    }

    void pushCenter(std::deque<CCPoint>& t, PlayerObject* p) {
        if (!p) return;
        t.push_back(p->getPosition());
        while (static_cast<int>(t.size()) > std::max(2, Cfg::get().trailLen * 4)) t.pop_front();
    }

    void drawCenter(CCDrawNode* n, std::deque<CCPoint> const& t, PlayerObject* p) {
        ccColor4F yellow{1.f, 0.95f, 0.1f, 1.f};
        for (size_t i = 1; i < t.size(); ++i) n->drawSegment(t[i - 1], t[i], 0.6f, yellow);
        if (!p) return;
        auto c = p->getPosition();
        CCPoint dot[4] = {{c.x - 2.5f, c.y - 2.5f}, {c.x + 2.5f, c.y - 2.5f}, {c.x + 2.5f, c.y + 2.5f}, {c.x - 2.5f, c.y + 2.5f}};
        n->drawPolygon(dot, 4, yellow, 0.f, yellow);
    }

    void drawHitborders(CCDrawNode* n, PlayLayer* pl) {
        auto outer = pl->m_player1->getObjectRect();
        auto inner = innerOf(outer);
        float ox = outer.size.width / 2, oy = outer.size.height / 2;
        float ix = inner.size.width / 2, iy = inner.size.height / 2;

        std::vector<CCRect> red, blue, green;
        float range = CCDirector::get()->getWinSize().width * 1.2f;
        float px = pl->m_player1->getPositionX();
        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (!obj || std::abs(obj->getPositionX() - px) > range) continue;
            auto k = kindOf(obj);
            if (k == Kind::Skip) continue;
            auto r = objRect(obj);
            if (k == Kind::Hazard) red.push_back(grow(r, ox, oy));
            else if (k == Kind::Solid) { blue.push_back(grow(r, ox, oy)); red.push_back(grow(r, ix, iy)); }
            else green.push_back(grow(r, ox, oy));
        }

        float bw = Cfg::get().hitBorder;
        float fa = std::clamp(Cfg::get().hitFill * 3.f, 0.15f, 0.85f);   // fill opacity slider also drives this view
        drawUnion(n, blue, {0.05f, 0.12f, 0.95f, 1.f}, {0.05f, 0.12f, 0.95f, 1.f}, 0.f);
        drawUnion(n, green, {0.1f, 0.75f, 0.2f, fa}, {0.2f, 1.f, 0.3f, 1.f}, bw);
        drawUnion(n, red, {0.55f, 0.05f, 0.05f, fa}, {1.f, 0.1f, 0.1f, 1.f}, bw);

        pushCenter(g_center1, pl->m_player1);
        drawCenter(n, g_center1, pl->m_player1);
        if (pl->m_gameState.m_isDualMode) {
            pushCenter(g_center2, pl->m_player2);
            drawCenter(n, g_center2, pl->m_player2);
        }
    }

    void drawObject(CCDrawNode* n, GameObject* o) {
        ccColor3B col;
        switch (kindOf(o)) {
            case Kind::Hazard: col = {255, 60, 60}; break;
            case Kind::Solid: col = {70, 130, 255}; break;
            case Kind::Skip: return;
            default: col = {80, 255, 120}; break;      // portals, pads, orbs, coins...
        }
        // Rotated objects carry an oriented box; everything else is an axis aligned rect.
        if (o->m_orientedBox) {
            auto& c = o->m_orientedBox->m_corners;   // std::array<CCPoint, 4> in 2.2091
            poly(n, std::vector<CCPoint>(c.begin(), c.end()), col, true);
        } else {
            poly(n, corners(o->getObjectRect()), col, true);
        }
    }

    void drawPlayer(CCDrawNode* n, PlayerObject* p) {
        if (!p) return;
        auto outer = p->getObjectRect();
        poly(n, corners(outer), {255, 215, 60}, true);
        poly(n, corners(innerOf(outer)), {60, 220, 255}, true);
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
    g_center1.clear();
    g_center2.clear();
}

void update(PlayLayer* pl) {
    auto& c = Cfg::get();
    auto node = getNode(pl);
    node->clear();
    if (!c.hitboxes && !c.hitboxTrail && !c.hitborders) return;
    if (path::simulating() || !pl->m_player1) return;

    if (c.hitborders) {          // NaN style view replaces the normal hitboxes
        drawHitborders(node, pl);
        return;
    }

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
