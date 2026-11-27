// Window counter overlay (same look as Antic's Counter / NaN Counter):
//  - while counting: every trial's path (white = survived, red = died) and "Counting.. 10/26 / ETA ~51s"
//  - the run's path, a ring where every click happened with its window next to it,
//    bucket counts on the left (0-1, 2, 3, 4, 5-6, 7-8, 9-12, 13-16, 17-24), counted % and average bottom left.
// Performance: the level overlay is split into chunks along x; only chunks near the player are visible.
#include "Common.hpp"
#include <cmath>
#include <cstdio>
#include <map>

using namespace geode::prelude;

namespace kt::view {

namespace {
    constexpr char const* kWorldId = "kt-counter-world";
    constexpr char const* kStatsId = "kt-counter-stats";
    constexpr char const* kTrialsId = "kt-counter-trials";
    constexpr char const* kCountId = "kt-counter-status";
    constexpr float kChunk = 480.f;
    int g_version = -1;
    PlayLayer* g_layer = nullptr;

    struct Bucket {
        char const* label;
        int lo, hi;
        ccColor3B col;
    };
    constexpr Bucket kBuckets[] = {
        {"0-1:", 0, 1, {255, 80, 80}},
        {"2:", 2, 2, {255, 160, 70}},
        {"3:", 3, 3, {255, 225, 90}},
        {"4:", 4, 4, {255, 255, 255}},
        {"5-6:", 5, 6, {120, 235, 120}},
        {"7-8:", 7, 8, {110, 215, 230}},
        {"9-12:", 9, 12, {130, 150, 255}},
        {"13-16:", 13, 16, {185, 120, 255}},
        {"17-24:", 17, 999, {255, 120, 210}},
    };
    constexpr int kBucketCount = sizeof(kBuckets) / sizeof(kBuckets[0]);

    ccColor3B colorFor(int w) {
        for (auto& b : kBuckets) if (w >= b.lo && w <= b.hi) return b.col;
        return {255, 255, 255};
    }

    void ring(CCDrawNode* n, CCPoint c, float r, float w, ccColor4F col) {
        constexpr int seg = 20;
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

    // ---- the level overlay, in chunks ----
    struct Chunk {
        CCNode* node;
        CCDrawNode* draw;
    };

    void buildWorld(PlayLayer* pl) {
        auto root = CCNode::create();
        root->setID(kWorldId);
        pl->m_objectLayer->addChild(root, 9997);

        std::map<int, Chunk> chunks;
        auto chunkAt = [&](float x) -> Chunk& {
            int i = static_cast<int>(std::floor(x / kChunk));
            auto it = chunks.find(i);
            if (it != chunks.end()) return it->second;
            auto node = CCNode::create();
            node->setTag(i);
            auto draw = CCDrawNode::create();
            node->addChild(draw);
            root->addChild(node);
            return chunks[i] = Chunk{node, draw};
        };

        auto& path = counter::pathPoints();
        ccColor4F line{1.f, 1.f, 1.f, 0.9f};
        for (size_t i = 1; i < path.size(); ++i) chunkAt(path[i - 1].x).draw->drawSegment(path[i - 1], path[i], 1.1f, line);

        for (auto& v : counter::clickViews()) {
            auto& c = chunkAt(v.pos.x);
            ring(c.draw, v.pos, 9.f, 2.2f, {0.95f, 0.95f, 0.95f, 0.95f});
            label(c.node, std::to_string(v.window), v.pos + ccp(-13.f, 0.f), 0.4f, colorFor(v.window), {1.f, 0.5f});
        }
    }

    void cullWorld(PlayLayer* pl, CCNode* world) {
        float px = pl->m_player1 ? pl->m_player1->getPositionX() : 0.f;
        float range = CCDirector::get()->getWinSize().width * 1.5f;
        if (!world->getChildren()) return;
        for (auto node : CCArrayExt<CCNode*>(world->getChildren())) {
            float x0 = node->getTag() * kChunk;
            node->setVisible(x0 + kChunk > px - range && x0 < px + range);
        }
    }

    void buildStats(PlayLayer* pl) {
        int counts[kBucketCount] = {};
        int total = 0, counted = 0;
        float sum = 0.f;
        for (auto& r : counter::results()) {
            ++total;
            if (!r.counted) continue;
            int w = r.window();
            ++counted;
            sum += w;
            for (int b = 0; b < kBucketCount; ++b) {
                if (w >= kBuckets[b].lo && w <= kBuckets[b].hi) { ++counts[b]; break; }
            }
        }
        auto root = CCNode::create();
        root->setID(kStatsId);
        pl->m_uiLayer->addChild(root, 90);
        auto win = CCDirector::get()->getWinSize();

        float y = win.height - 14.f;
        for (int b = 0; b < kBucketCount; ++b) {
            label(root, kBuckets[b].label, {6.f, y}, 0.4f, kBuckets[b].col, {0.f, 0.5f});
            label(root, std::to_string(counts[b]), {96.f, y}, 0.4f, kBuckets[b].col, {1.f, 0.5f});
            y -= 16.f;
        }

        char buf[48];
        std::snprintf(buf, sizeof buf, "%.2f%%", total ? 100.f * counted / total : 0.f);
        label(root, buf, {6.f, 26.f}, 0.4f, {255, 255, 255}, {0.f, 0.5f});
        std::snprintf(buf, sizeof buf, "%.2f", counted ? sum / counted : 0.f);
        label(root, buf, {6.f, 10.f}, 0.4f, {255, 255, 255}, {0.f, 0.5f});
    }

    // ---- while counting: trial paths + status text (cheap, redrawn every frame) ----
    void updateCounting(PlayLayer* pl, bool on) {
        auto trials = static_cast<CCDrawNode*>(pl->m_objectLayer->getChildByID(kTrialsId));
        auto text = static_cast<CCLabelBMFont*>(pl->m_uiLayer->getChildByID(kCountId));
        if (!on) {
            if (trials) trials->removeFromParentAndCleanup(true);
            if (text) text->removeFromParentAndCleanup(true);
            return;
        }
        if (!trials) {
            trials = CCDrawNode::create();
            trials->setID(kTrialsId);
            pl->m_objectLayer->addChild(trials, 9998);
        }
        if (!text) {
            auto win = CCDirector::get()->getWinSize();
            text = CCLabelBMFont::create("", "bigFont.fnt");
            text->setID(kCountId);
            text->setScale(0.45f);
            text->setAlignment(kCCTextAlignmentCenter);
            text->setPosition({win.width / 2.f, 48.f});
            pl->m_uiLayer->addChild(text, 95);
        }
        trials->clear();
        auto drawLine = [&](std::vector<CCPoint> const& pts, ccColor4F col) {
            for (size_t i = 1; i < pts.size(); ++i) trials->drawSegment(pts[i - 1], pts[i], 0.8f, col);
        };
        for (auto& l : counter::trialLines()) {
            drawLine(l.pts, l.ok ? ccColor4F{1.f, 1.f, 1.f, 0.75f} : ccColor4F{1.f, 0.35f, 0.45f, 0.75f});
        }
        drawLine(counter::currentTrialPath(), {1.f, 1.f, 1.f, 0.95f});
        text->setString(counter::countingText().c_str());
    }
}

// Two separate switches:
//  - "Counter overlay" (Display tab): the path, the rings and the window numbers in the level
//  - "Show Results List" (Window counter tab): the 0-1:, 2:, 3: ... list on the left
void update(PlayLayer* pl) {
    auto& c = Cfg::get();
    bool counting = counter::isCounting();
    updateCounting(pl, counting && c.counterOverlay);

    auto world = pl->m_objectLayer->getChildByID(kWorldId);
    auto stats = pl->m_uiLayer->getChildByID(kStatsId);
    bool changed = g_layer != pl || g_version != counter::version();
    g_layer = pl;
    g_version = counter::version();

    if (!c.counterOverlay) {
        if (world) world->removeFromParentAndCleanup(true);
        world = nullptr;
    } else if (!world || changed) {
        if (world) world->removeFromParentAndCleanup(true);
        buildWorld(pl);
        world = pl->m_objectLayer->getChildByID(kWorldId);
    }

    if (!c.counterList) {
        if (stats) stats->removeFromParentAndCleanup(true);
    } else if (!stats || changed) {
        if (stats) stats->removeFromParentAndCleanup(true);
        buildStats(pl);
    }

    if (world) cullWorld(pl, world);
}

} // namespace kt::view
