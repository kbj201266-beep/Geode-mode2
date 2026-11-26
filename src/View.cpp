// Window counter overlay, NaN Counter style:
//  - in the level: the counted run's path as a thick yellow line, a colored ring where every click happened,
//    and the click's window (in frames) next to the ring
//  - on screen: how many clicks fall in each window bucket (top left), share of tight clicks and the
//    average window (bottom left), and a title at the top.
#include "Common.hpp"
#include <cmath>
#include <cstdio>

using namespace geode::prelude;

namespace kt::view {

namespace {
    constexpr char const* kWorldId = "kt-counter-world";
    constexpr char const* kStatsId = "kt-counter-stats";
    int g_version = -1;
    PlayLayer* g_layer = nullptr;

    struct Bucket {
        char const* label;
        int lo, hi;
        ccColor3B col;
    };
    constexpr Bucket kBuckets[] = {
        {"9+:", 9, 999, {175, 140, 255}},
        {"7-8:", 7, 8, {120, 205, 255}},
        {"5-6:", 5, 6, {120, 235, 120}},
        {"4:", 4, 4, {255, 255, 255}},
        {"3:", 3, 3, {255, 230, 90}},
        {"2:", 2, 2, {255, 165, 80}},
        {"1:", 1, 1, {255, 80, 80}},
    };
    constexpr int kBucketCount = sizeof(kBuckets) / sizeof(kBuckets[0]);

    ccColor3B colorFor(int w) {
        for (auto& b : kBuckets) if (w >= b.lo && w <= b.hi) return b.col;
        return {255, 255, 255};
    }
    ccColor4F c4(ccColor3B c, float a = 1.f) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }

    void ring(CCDrawNode* n, CCPoint c, float r, float w, ccColor4F col) {
        constexpr int seg = 24;
        CCPoint prev{c.x + r, c.y};
        for (int i = 1; i <= seg; ++i) {
            float a = 6.2831853f * i / seg;
            CCPoint p{c.x + r * std::cos(a), c.y + r * std::sin(a)};
            n->drawSegment(prev, p, w / 2.f, col);
            prev = p;
        }
    }

    CCLabelBMFont* label(CCNode* parent, std::string const& s, CCPoint pos, float scale, ccColor3B col, CCPoint anchor) {
        auto l = CCLabelBMFont::create(s.c_str(), "bigFont.fnt");
        l->setScale(scale);
        l->setColor(col);
        l->setAnchorPoint(anchor);
        l->setPosition(pos);
        parent->addChild(l);
        return l;
    }

    void buildWorld(PlayLayer* pl) {
        auto parent = pl->m_objectLayer;
        auto root = CCNode::create();
        root->setID(kWorldId);
        parent->addChild(root, 9997);

        auto dn = CCDrawNode::create();
        root->addChild(dn);
        auto& path = counter::pathPoints();
        ccColor4F glow{1.f, 0.95f, 0.35f, 0.35f}, line{1.f, 0.93f, 0.3f, 0.95f};
        for (size_t i = 1; i < path.size(); ++i) dn->drawSegment(path[i - 1], path[i], 3.2f, glow);
        for (size_t i = 1; i < path.size(); ++i) dn->drawSegment(path[i - 1], path[i], 1.4f, line);

        for (auto& v : counter::clickViews()) {
            auto col = colorFor(v.window);
            ring(dn, v.pos, 9.f, 2.4f, c4(col));
            label(root, std::to_string(v.window), v.pos + ccp(-14.f, 0.f), 0.42f, col, {1.f, 0.5f});
        }
    }

    void buildStats(PlayLayer* pl) {
        int counts[kBucketCount] = {};
        int total = 0, tight = 0;
        float sum = 0.f;
        for (auto& r : counter::results()) {
            if (!r.counted) continue;
            int w = r.window();
            ++total;
            sum += w;
            if (w <= 3) ++tight;
            for (int b = 0; b < kBucketCount; ++b) {
                if (w >= kBuckets[b].lo && w <= kBuckets[b].hi) { ++counts[b]; break; }
            }
        }
        if (total == 0) return;

        auto root = CCNode::create();
        root->setID(kStatsId);
        pl->m_uiLayer->addChild(root, 90);
        auto win = CCDirector::get()->getWinSize();

        float y = win.height - 12.f;
        for (int b = 0; b < kBucketCount; ++b) {
            label(root, kBuckets[b].label, {8.f, y}, 0.42f, kBuckets[b].col, {0.f, 0.5f});
            label(root, std::to_string(counts[b]), {58.f, y}, 0.42f, kBuckets[b].col, {0.f, 0.5f});
            y -= 17.f;
        }

        label(root, "Window Counter", {win.width / 2.f, win.height - 24.f}, 0.32f, {150, 150, 160}, {0.5f, 0.5f});

        char buf[48];
        std::snprintf(buf, sizeof buf, "<=3f  %.2f%%", 100.f * tight / total);
        label(root, buf, {8.f, 30.f}, 0.42f, {255, 255, 255}, {0.f, 0.5f});
        std::snprintf(buf, sizeof buf, "avg  %.2ff", sum / total);
        label(root, buf, {8.f, 12.f}, 0.42f, {255, 255, 255}, {0.f, 0.5f});
    }
}

void update(PlayLayer* pl) {
    bool show = Cfg::get().counterOverlay && !counter::isCounting();
    auto world = pl->m_objectLayer->getChildByID(kWorldId);
    auto stats = pl->m_uiLayer->getChildByID(kStatsId);
    if (!show) {
        if (world) world->removeFromParentAndCleanup(true);
        if (stats) stats->removeFromParentAndCleanup(true);
        g_version = -1;
        return;
    }
    if (world && g_layer == pl && g_version == counter::version()) return;

    if (world) world->removeFromParentAndCleanup(true);
    if (stats) stats->removeFromParentAndCleanup(true);
    g_layer = pl;
    g_version = counter::version();
    buildWorld(pl);
    buildStats(pl);
}

} // namespace kt::view
