// Hitboxes, hitbox trail, and Hitborders (NaN style "accurate gaps").
//
// Hitborders: every object is grown by the player's size, so only the player's CENTER (yellow dot) matters:
//   red   = hazards grown by half the outer player box + solids grown by half the inner box  -> center in red = death
//   blue  = solids grown by half the outer player box                                         -> passable / landable
//   green = pads, orbs, portals grown by half the outer box
// All areas go into ONE grid: every cell gets exactly one color (red > green > blue), so areas never overlap
// and outlines are drawn only where the color changes.
//
// Performance: objects are sorted by x once per level, so each frame only the objects near the player are visited.
#include "Common.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

using namespace geode::prelude;

namespace kt::hitbox {

namespace {
    constexpr float kInner = 0.3f;     // inner (solid-death) hitbox = 30% of the outer box
    constexpr float kMargin = 400.f;   // extra x range so wide objects are not missed
    struct TrailBox {
        CCRect outer, inner;
    };
    std::deque<TrailBox> g_trail1, g_trail2;
    std::deque<CCPoint> g_center1, g_center2;
    constexpr char const* kNodeId = "kt-hitbox-node";

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

    // ---- object cache: only hitbox objects, sorted by x ----
    PlayLayer* g_owner = nullptr;
    unsigned g_count = 0;
    std::vector<GameObject*> g_sorted;
    int g_sortTick = 0;

    bool byX(GameObject* a, GameObject* b) { return a->getPositionX() < b->getPositionX(); }

    void ensureCache(PlayLayer* pl) {
        unsigned n = pl->m_objects ? pl->m_objects->count() : 0;
        if (pl == g_owner && n == g_count) {
            // moving objects (move triggers) change their x: keep the order right so none are missed.
            // The list is almost sorted already, so insertion sort is cheap; done twice a second.
            if (++g_sortTick % 30 == 0) {
                for (size_t i = 1; i < g_sorted.size(); ++i) {
                    auto o = g_sorted[i];
                    size_t j = i;
                    while (j > 0 && byX(o, g_sorted[j - 1])) { g_sorted[j] = g_sorted[j - 1]; --j; }
                    g_sorted[j] = o;
                }
            }
            return;
        }
        g_owner = pl;
        g_count = n;
        g_sorted.clear();
        if (!pl->m_objects) return;
        g_sorted.reserve(n);
        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (obj && kindOf(obj) != Kind::Skip) g_sorted.push_back(obj);
        }
        std::sort(g_sorted.begin(), g_sorted.end(), byX);
    }

    CCRect objRect(GameObject* o);

    // objects the game currently ignores for collision: "no touch", or in a group that is toggled off
    bool collides(GameObject* o) { return !o->m_isNoTouch && !o->m_isGroupDisabled; }

    // only objects whose box really reaches into [x0, x1] (the visible part of the level)
    template <class F>
    void forNear(float x0, float x1, F&& f) {
        auto it = std::lower_bound(g_sorted.begin(), g_sorted.end(), x0 - kMargin,
                                   [](GameObject* o, float x) { return o->getPositionX() < x; });
        for (; it != g_sorted.end() && (*it)->getPositionX() <= x1 + kMargin; ++it) {
            if (!collides(*it)) continue;
            auto r = objRect(*it);
            if (r.getMaxX() < x0 || r.getMinX() > x1) continue;
            f(*it);
        }
    }

    // visible x range: the player sits about a third into the screen
    void viewRange(PlayLayer* pl, float& x0, float& x1) {
        float w = CCDirector::get()->getWinSize().width;
        float px = pl->m_player1->getPositionX();
        x0 = px - w * 0.6f;
        x1 = px + w * 1.0f;
    }

    CCDrawNode* g_node = nullptr;     // kept as a pointer: no search through the level's children every frame
    CCDrawNode* getNode(PlayLayer* pl) {
        if (!g_node) {
            auto parent = pl->m_objectLayer;
            g_node = static_cast<CCDrawNode*>(parent->getChildByID(kNodeId));
            if (!g_node) {
                g_node = CCDrawNode::create();
                g_node->setID(kNodeId);
                parent->addChild(g_node, 9999);
            }
        }
        return g_node;
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

    // rotated by something that is not a multiple of 90 degrees (then its box is not axis aligned)
    bool isTilted(GameObject* o) {
        if (!o->m_orientedBox) return false;
        float r = std::fmod(std::fabs(o->getRotation()), 90.f);
        return r > 0.5f && r < 89.5f;
    }

    // saw blades and other round hazards: the game tests a circle, not a box
    bool isCircle(GameObject* o) { return o->m_objectRadius > 0.f && o->m_objectType != GameObjectType::Slope; }

    bool needsPolygon(GameObject* o) {
        return o->m_objectType == GameObjectType::Slope || isTilted(o) || isCircle(o);
    }

    // The real hitbox shape: a circle for round hazards, a triangle for slopes (45 and 22.5 degree), the rotated
    // box for tilted objects, otherwise the plain rectangle.
    std::vector<CCPoint> shapeOf(GameObject* o) {
        if (isCircle(o)) {
            auto r = objRect(o);
            CCPoint c(r.getMidX(), r.getMidY());
            float rad = o->m_objectRadius * std::max(std::fabs(o->getScaleX()), std::fabs(o->getScaleY()));
            std::vector<CCPoint> pts;
            constexpr int seg = 24;
            for (int i = 0; i < seg; ++i) {
                float a = 6.2831853f * i / seg;
                pts.push_back(CCPoint(c.x + rad * std::cos(a), c.y + rad * std::sin(a)));
            }
            return pts;
        }
        if (o->m_objectType == GameObjectType::Slope) {
            auto r = objRect(o);
            CCPoint bl(r.getMinX(), r.getMinY()), br(r.getMaxX(), r.getMinY());
            CCPoint tl(r.getMinX(), r.getMaxY()), tr(r.getMaxX(), r.getMaxY());
            // GD's names describe the FLAT sides of the triangle: slopeFloorTop = the flat (floor) side is at the
            // top (an upside-down slope), slopeWallLeft = the vertical wall is on the left.
            bool flatTop = o->slopeFloorTop();
            bool wallLeft = o->slopeWallLeft();
            if (flatTop) return wallLeft ? std::vector<CCPoint>{tl, tr, bl} : std::vector<CCPoint>{tl, tr, br};
            return wallLeft ? std::vector<CCPoint>{bl, br, tl} : std::vector<CCPoint>{bl, br, tr};
        }
        if (isTilted(o)) {
            auto& c = o->m_orientedBox->m_corners;
            return std::vector<CCPoint>(c.begin(), c.end());
        }
        return corners(o->getObjectRect());
    }

    std::vector<CCPoint> hullOf(std::vector<CCPoint> pts);

    // shape grown by a box of half size (hx, hy) = convex hull of the shape moved to every box corner
    std::vector<CCPoint> grownHull(std::vector<CCPoint> const& shape, float hx, float hy) {
        std::vector<CCPoint> pts;
        for (auto& p : shape) {
            pts.push_back(CCPoint(p.x - hx, p.y - hy));
            pts.push_back(CCPoint(p.x + hx, p.y - hy));
            pts.push_back(CCPoint(p.x + hx, p.y + hy));
            pts.push_back(CCPoint(p.x - hx, p.y + hy));
        }
        return hullOf(std::move(pts));
    }

    // convex hull (monotone chain)
    std::vector<CCPoint> hullOf(std::vector<CCPoint> pts) {
        if (pts.size() < 3) return pts;
        std::sort(pts.begin(), pts.end(), [](CCPoint const& a, CCPoint const& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        auto cross = [](CCPoint const& o, CCPoint const& a, CCPoint const& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        std::vector<CCPoint> h(pts.size() * 2);
        size_t k = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
            h[k++] = pts[i];
        }
        for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
            while (k >= t && cross(h[k - 2], h[k - 1], pts[i - 1]) <= 0) --k;
            h[k++] = pts[i - 1];
        }
        h.resize(k > 0 ? k - 1 : 0);
        return h;
    }

    void fillPoly(CCDrawNode* n, std::vector<CCPoint> const& v, ccColor4F fill, float bw, ccColor4F border) {
        if (v.size() < 3) return;
        n->drawPolygon(const_cast<CCPoint*>(v.data()), static_cast<unsigned>(v.size()), fill, bw, border);
    }

    struct Area {
        CCRect r;
        uint8_t level;      // 1 blue, 2 green, 3 red (higher wins)
    };

    // One grid for all areas: every cell gets the highest level that covers it.
    void drawAreas(CCDrawNode* n, std::vector<Area> const& areas, float bw, float fillA) {
        if (areas.empty()) return;
        std::vector<float> xs, ys;
        xs.reserve(areas.size() * 2);
        ys.reserve(areas.size() * 2);
        for (auto& a : areas) {
            xs.push_back(a.r.getMinX()); xs.push_back(a.r.getMaxX());
            ys.push_back(a.r.getMinY()); ys.push_back(a.r.getMaxY());
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
        std::vector<uint8_t> cell(static_cast<size_t>(nx) * ny, 0);
        auto at = [&](int i, int j) -> uint8_t& { return cell[static_cast<size_t>(j) * nx + i]; };
        for (auto& a : areas) {
            int x0 = idx(xs, a.r.getMinX()), x1 = idx(xs, a.r.getMaxX());
            int y0 = idx(ys, a.r.getMinY()), y1 = idx(ys, a.r.getMaxY());
            for (int j = y0; j < y1 && j < ny; ++j)
                for (int i = x0; i < x1 && i < nx; ++i) at(i, j) = std::max(at(i, j), a.level);
        }

        ccColor4F fills[4] = {{0, 0, 0, 0},
                              {0.05f, 0.12f, 0.95f, 1.f},             // blue (solid, no transparency like NaN's)
                              {0.1f, 0.75f, 0.2f, fillA},             // green
                              {0.55f, 0.05f, 0.05f, fillA}};          // red
        ccColor4F borders[4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0.2f, 1.f, 0.3f, 1.f}, {1.f, 0.1f, 0.1f, 1.f}};

        // fill: cells become as few rectangles as possible (a row run is stacked onto the run above it when
        // both have the same x range and color), so no two quads share an edge or overlap.
        struct Open { int s, e, j0; uint8_t v; };
        std::vector<Open> open;
        auto flush = [&](Open const& o, int jEnd) {
            CCPoint q[4] = {{xs[o.s], ys[o.j0]}, {xs[o.e], ys[o.j0]}, {xs[o.e], ys[jEnd]}, {xs[o.s], ys[jEnd]}};
            n->drawPolygon(q, 4, fills[o.v], 0.f, fills[o.v]);
        };
        for (int j = 0; j < ny; ++j) {
            std::vector<Open> next;
            int i = 0;
            while (i < nx) {
                uint8_t v = at(i, j);
                int s = i;
                while (i < nx && at(i, j) == v) ++i;
                if (!v) continue;
                Open cur{s, i, j, v};
                for (auto& o : open) {
                    if (o.j0 >= 0 && o.s == s && o.e == i && o.v == v) { cur.j0 = o.j0; o.j0 = -1; break; }
                }
                next.push_back(cur);
            }
            for (auto& o : open) if (o.j0 >= 0) flush(o, j);
            open = std::move(next);
        }
        for (auto& o : open) flush(o, ny);

        // outline: drawn where the color changes, in the color of the higher area (blue has none).
        // Neighbouring edge pieces of the same color are joined into one long line, so even a big level only
        // needs a few lines instead of one per cell.
        if (bw <= 0.f) return;
        float rad = bw / 2.f;
        auto val = [&](int i, int j) -> uint8_t { return (i < 0 || j < 0 || i >= nx || j >= ny) ? 0 : at(i, j); };
        auto edge = [](uint8_t a, uint8_t b) -> uint8_t {
            uint8_t hi = std::max(a, b);
            return (hi >= 2 && a != b) ? hi : 0;
        };
        // vertical lines at every x boundary
        for (int i = 0; i <= nx; ++i) {
            int j = 0;
            while (j < ny) {
                uint8_t e = edge(val(i - 1, j), val(i, j));
                if (!e) { ++j; continue; }
                int s = j;
                while (j < ny && edge(val(i - 1, j), val(i, j)) == e) ++j;
                n->drawSegment({xs[i], ys[s]}, {xs[i], ys[j]}, rad, borders[e]);
            }
        }
        // horizontal lines at every y boundary
        for (int j = 0; j <= ny; ++j) {
            int i = 0;
            while (i < nx) {
                uint8_t e = edge(val(i, j - 1), val(i, j));
                if (!e) { ++i; continue; }
                int s = i;
                while (i < nx && edge(val(i, j - 1), val(i, j)) == e) ++i;
                n->drawSegment({xs[s], ys[j]}, {xs[i], ys[j]}, rad, borders[e]);
            }
        }
    }

    void pushCenter(std::deque<CCPoint>& t, PlayerObject* p) {
        if (!p) return;
        t.push_back(p->getPosition());
        while (static_cast<int>(t.size()) > std::clamp(Cfg::get().trailLen * 4, 2, 1200)) t.pop_front();
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

        // Straight boxes go into the merged grid. Slopes and tilted objects are not rectangles, so they get
        // their exact grown shape as a polygon instead (blue ones under the grid, red/green ones on top).
        std::vector<Area> areas;
        std::vector<std::vector<CCPoint>> blueP, greenP, redP;
        float x0, x1;
        viewRange(pl, x0, x1);
        forNear(x0, x1, [&](GameObject* obj) {
            auto kind = kindOf(obj);
            if (kind == Kind::Solid && obj->m_objectType == GameObjectType::Slope && obj->m_slopeIsHazard) kind = Kind::Hazard;
            if (needsPolygon(obj)) {
                auto shape = shapeOf(obj);
                switch (kind) {
                    case Kind::Hazard: redP.push_back(grownHull(shape, ox, oy)); break;
                    case Kind::Solid:
                        blueP.push_back(grownHull(shape, ox, oy));
                        if (!obj->m_isPassable) redP.push_back(grownHull(shape, ix, iy));
                        break;
                    case Kind::Special: greenP.push_back(grownHull(shape, ox, oy)); break;
                    default: break;
                }
                return;
            }
            auto r = objRect(obj);
            switch (kind) {
                case Kind::Hazard: areas.push_back({grow(r, ox, oy), 3}); break;
                case Kind::Solid:
                    areas.push_back({grow(r, ox, oy), 1});
                    // passable blocks (jump through from below) can never kill: no red core
                    if (!obj->m_isPassable) areas.push_back({grow(r, ix, iy), 3});
                    break;
                case Kind::Special: areas.push_back({grow(r, ox, oy), 2}); break;
                default: break;
            }
        });

        float bw = Cfg::get().hitBorder;
        float fa = std::clamp(Cfg::get().hitFill * 3.f, 0.15f, 0.85f);   // fill opacity slider also drives this view
        ccColor4F blue{0.05f, 0.12f, 0.95f, 1.f};
        for (auto& p : blueP) fillPoly(n, p, blue, 0.f, blue);
        drawAreas(n, areas, bw, fa);
        for (auto& p : greenP) fillPoly(n, p, {0.1f, 0.75f, 0.2f, fa}, bw / 2.f, {0.2f, 1.f, 0.3f, 1.f});
        for (auto& p : redP) fillPoly(n, p, {0.55f, 0.05f, 0.05f, fa}, bw / 2.f, {1.f, 0.1f, 0.1f, 1.f});

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
        // Circles for round hazards, triangles for slopes, oriented boxes for rotated objects, else a rect.
        if (isCircle(o)) {
            poly(n, shapeOf(o), col, true);
        } else if (o->m_objectType == GameObjectType::Slope) {
            if (o->m_slopeIsHazard) col = {255, 60, 60};
            poly(n, shapeOf(o), col, true);
        } else if (o->m_orientedBox) {
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

    // Trail like a swept band: every two following boxes are joined by their convex hull, so the trail is one
    // solid ribbon. Red = outer hitbox (with a dark red edge), blue = inner hitbox on top.
    void drawTrail(CCDrawNode* n, std::deque<TrailBox> const& t) {
        if (t.empty()) return;
        ccColor4F edge{0.45f, 0.f, 0.f, 1.f}, red{1.f, 0.f, 0.f, 1.f}, blue{0.f, 0.f, 0.85f, 1.f};
        auto sweep = [&](bool outer, float g, ccColor4F col) {
            for (size_t i = 0; i < t.size(); ++i) {
                CCRect a = outer ? t[i].outer : t[i].inner;
                CCRect b = outer ? t[i ? i - 1 : 0].outer : t[i ? i - 1 : 0].inner;
                auto pts = corners(grow(a, g, g));
                auto pb = corners(grow(b, g, g));
                pts.insert(pts.end(), pb.begin(), pb.end());
                fillPoly(n, hullOf(std::move(pts)), col, 0.f, col);
            }
        };
        sweep(true, 1.2f, edge);
        sweep(true, 0.f, red);
        sweep(false, 0.f, blue);
    }

    void push(std::deque<TrailBox>& t, PlayerObject* p) {
        if (!p) return;
        auto outer = p->getObjectRect();
        t.push_back({outer, innerOf(outer)});
        while (static_cast<int>(t.size()) > std::max(1, Cfg::get().trailLen)) t.pop_front();
    }
}

// New level: the object cache and the draw node belong to the old one.
void enter() {
    g_owner = nullptr;
    g_count = 0;
    g_sorted.clear();
    g_node = nullptr;
    reset();
}

// Every death / restart: only the trails. The object cache stays (sorting the whole level again on every
// death caused a hitch each time).
void reset() {
    g_trail1.clear();
    g_trail2.clear();
    g_center1.clear();
    g_center2.clear();
}

void update(PlayLayer* pl) {
    auto& c = Cfg::get();
    static bool s_drawn = false;
    if (!c.hitboxes && !c.hitboxTrail && !c.hitborders) {
        if (s_drawn && g_node) g_node->clear();     // nothing on: no work at all after the first clear
        s_drawn = false;
        return;
    }
    if (path::simulating() || !pl->m_player1) return;
    auto node = getNode(pl);
    node->clear();
    s_drawn = true;
    ensureCache(pl);

    // Hitborders and hitboxes are independent: turn both on to get the NaN look (merged danger area underneath,
    // the real object hitboxes drawn on top). Anything drawn later ends up above what was drawn before.
    if (c.hitborders) drawHitborders(node, pl);

    if (c.hitboxTrail) {
        push(g_trail1, pl->m_player1);
        push(g_trail2, pl->m_gameState.m_isDualMode ? pl->m_player2 : nullptr);
        drawTrail(node, g_trail1);
        drawTrail(node, g_trail2);
    }
    if (!c.hitboxes) return;

    float x0, x1;
    viewRange(pl, x0, x1);
    forNear(x0, x1, [&](GameObject* obj) { drawObject(node, obj); });
    if (c.playerHitbox) {
        drawPlayer(node, pl->m_player1);
        if (pl->m_gameState.m_isDualMode) drawPlayer(node, pl->m_player2);
    }
}

} // namespace kt::hitbox
