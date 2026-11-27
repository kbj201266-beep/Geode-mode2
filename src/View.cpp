// Window counter overlay (Antic's Counter / NaN Counter look):
//  - while counting: the paths of the trials (white = survived, red = died), "Counting.. 10/26 / ETA ~51s",
//    and a donut ring for every press / release the moment its window is finished - one by one.
//  - while playing (yourself or the bot): the run's path, and the donut rings appear one by one as you reach
//    each press / release. Top right: the window of the last one, in frames and milliseconds (2 decimals).
//  - "Show Results List": how many presses / releases fall in each window bucket (left side).
// Performance: everything drawn in the level is split into chunks along x; only chunks near the player are visible.
#include "Common.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

using namespace geode::prelude;

namespace kt::view {

namespace {
    constexpr char const* kPathId = "kt-counter-path";
    constexpr char const* kRingsId = "kt-counter-rings";
    constexpr char const* kStatsId = "kt-counter-stats";
    constexpr char const* kTrialsId = "kt-counter-trials";
    constexpr char const* kCountId = "kt-counter-status";
    constexpr char const* kLastId = "kt-counter-last";
    constexpr float kChunk = 480.f;

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
    ccColor4F c4(ccColor3B c, float a = 1.f) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }

    // thick ring ("donut")
    void donut(CCDrawNode* n, CCPoint c, float r, float thick, ccColor4F col) {
        constexpr int seg = 24;
        CCPoint prev(c.x + r, c.y);
        for (int i = 1; i <= seg; ++i) {
            float a = 6.2831853f * i / seg;
            CCPoint p(c.x + r * std::cos(a), c.y + r * std::sin(a));
            n->drawSegment(prev, p, thick / 2.f, col);
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

    float tickMs() {
        auto& c = Cfg::get();
        return 1000.f / (c.tpsBypass ? c.tps : 240.f);
    }

    // ---- chunked node: children are tagged with their x chunk, only nearby chunks are visible ----
    struct Chunk {
        CCNode* node;
        CCDrawNode* draw;
    };
    struct Chunked {
        std::map<int, Chunk> chunks;
        Chunk& at(CCNode* root, float x) {
            int i = static_cast<int>(std::floor(x / kChunk));
            auto it = chunks.find(i);
            if (it != chunks.end()) return it->second;
            auto node = CCNode::create();
            node->setTag(i);
            auto draw = CCDrawNode::create();
            node->addChild(draw);
            root->addChild(node);
            return chunks[i] = Chunk{node, draw};
        }
    };

    void cull(PlayLayer* pl, CCNode* root) {
        if (!root || !root->getChildren()) return;
        float px = pl->m_player1 ? pl->m_player1->getPositionX() : 0.f;
        float range = CCDirector::get()->getWinSize().width * 1.5f;
        for (auto node : CCArrayExt<CCNode*>(root->getChildren())) {
            float x0 = node->getTag() * kChunk;
            node->setVisible(x0 + kChunk > px - range && x0 < px + range);
        }
    }

    CCNode* freshRoot(CCNode* parent, char const* id, int z) {
        if (auto old = parent->getChildByID(id)) old->removeFromParentAndCleanup(true);
        auto root = CCNode::create();
        root->setID(id);
        parent->addChild(root, z);
        return root;
    }

    // ---- the run's path ----
    PlayLayer* g_pathLayer = nullptr;
    size_t g_pathSize = 0;
    int g_pathVersion = -1;

    void updatePath(PlayLayer* pl, bool show) {
        auto root = pl->m_objectLayer->getChildByID(kPathId);
        if (!show || counter::baselineRunning()) {
            if (root) root->removeFromParentAndCleanup(true);
            g_pathLayer = nullptr;
            return;
        }
        auto& path = counter::pathPoints();
        if (!root || g_pathLayer != pl || g_pathSize != path.size() || g_pathVersion != counter::version()) {
            // the path only changes when a replay is loaded or counted, so this rarely rebuilds
            if (root && g_pathLayer == pl && g_pathSize == path.size()) {
                g_pathVersion = counter::version();
            } else {
                root = freshRoot(pl->m_objectLayer, kPathId, 9996);
                Chunked ch;
                ccColor4F line{1.f, 1.f, 1.f, 0.85f};
                for (size_t i = 1; i < path.size(); ++i) ch.at(root, path[i - 1].x).draw->drawSegment(path[i - 1], path[i], 1.1f, line);
                g_pathLayer = pl;
                g_pathSize = path.size();
                g_pathVersion = counter::version();
            }
        }
        cull(pl, root);
    }

    // ---- donut rings, added one by one ----
    struct Rings {
        PlayLayer* layer = nullptr;
        bool counting = false;
        int version = -1;
        size_t shown = 0;
        Chunked ch;
    } g_rings;

    void addRing(CCNode* root, counter::Cue const& c) {
        if (c.pos.x < -1e8f) return;     // no position known (old replay): number only shows top right
        auto col = colorFor(c.window);
        auto& chunk = g_rings.ch.at(root, c.pos.x);
        float r = c.release ? 6.f : 9.f;
        donut(chunk.draw, c.pos, r, c.release ? 2.6f : 3.6f, c4(col, 0.95f));
        label(chunk.node, std::to_string(c.window), c.pos + ccp(-(r + 4.f), 0.f), c.release ? 0.3f : 0.4f, col, {1.f, 0.5f});
    }

    void updateRings(PlayLayer* pl, bool show) {
        auto root = pl->m_objectLayer->getChildByID(kRingsId);
        if (!show) {
            if (root) root->removeFromParentAndCleanup(true);
            g_rings.layer = nullptr;
            return;
        }
        bool counting = counter::isCounting();
        std::vector<counter::Cue> list;
        if (counting) {
            list = counter::countedCues();
        } else {
            auto& all = counter::cues();
            size_t n = std::min(counter::cueCursor(), all.size());
            list.assign(all.begin(), all.begin() + n);
        }
        bool reset = !root || g_rings.layer != pl || g_rings.counting != counting || list.size() < g_rings.shown ||
                     (!counting && g_rings.version != counter::version());
        if (reset) {
            root = freshRoot(pl->m_objectLayer, kRingsId, 9998);
            g_rings = Rings{};
            g_rings.layer = pl;
            g_rings.counting = counting;
            g_rings.version = counter::version();
        }
        for (size_t i = g_rings.shown; i < list.size(); ++i) addRing(root, list[i]);
        g_rings.shown = list.size();
        cull(pl, root);
    }

    // ---- top right: the last reached window, 2 decimals ----
    int g_lastShown = -2;
    int g_lastVersion = -1;

    void updateLast(PlayLayer* pl, bool show) {
        auto l = static_cast<CCLabelBMFont*>(pl->m_uiLayer->getChildByID(kLastId));
        int idx = counter::lastCue();
        auto& all = counter::cues();
        if (!show || idx < 0 || idx >= static_cast<int>(all.size())) {
            if (l) l->setVisible(false);
            g_lastShown = -2;
            return;
        }
        if (!l) {
            auto win = CCDirector::get()->getWinSize();
            l = CCLabelBMFont::create("", "bigFont.fnt");
            l->setID(kLastId);
            l->setScale(0.45f);
            l->setAnchorPoint({1.f, 0.5f});
            l->setPosition({win.width - 10.f, win.height - 46.f});
            pl->m_uiLayer->addChild(l, 95);
            g_lastShown = -2;
        }
        l->setVisible(true);
        if (idx == g_lastShown && g_lastVersion == counter::version()) return;
        g_lastShown = idx;
        g_lastVersion = counter::version();
        auto& c = all[idx];
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s%df  %.2fms", c.release ? "rel " : "", c.window, c.window * tickMs());
        l->setString(buf);
        l->setColor(colorFor(c.window));
    }

    // ---- left side bucket list ----
    int g_statsVersion = -1;
    PlayLayer* g_statsLayer = nullptr;

    void updateStats(PlayLayer* pl, bool show) {
        auto stats = pl->m_uiLayer->getChildByID(kStatsId);
        if (!show) {
            if (stats) stats->removeFromParentAndCleanup(true);
            g_statsLayer = nullptr;
            return;
        }
        if (stats && g_statsLayer == pl && g_statsVersion == counter::version()) return;
        g_statsLayer = pl;
        g_statsVersion = counter::version();

        int counts[kBucketCount] = {};
        int total = 0, counted = 0;
        float sum = 0.f;
        auto add = [&](int w) {
            ++counted;
            sum += w;
            for (int b = 0; b < kBucketCount; ++b) {
                if (w >= kBuckets[b].lo && w <= kBuckets[b].hi) { ++counts[b]; break; }
            }
        };
        for (auto& r : counter::results()) {
            ++total;
            if (r.counted) add(r.window());
            if (r.relFrame >= 0) {
                ++total;
                if (r.relCounted) add(r.relWindow());
            }
        }

        auto root = freshRoot(pl->m_uiLayer, kStatsId, 90);
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
            pl->m_objectLayer->addChild(trials, 9997);
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

// Two switches:
//  - "Counter overlay" (Display tab): path, donut rings, numbers, the top right value, the counting text
//  - "Show Results List" (Window counter tab): the 0-1:, 2:, 3: ... list on the left
void update(PlayLayer* pl) {
    auto& c = Cfg::get();
    bool counting = counter::isCounting();
    updateCounting(pl, counting && c.counterOverlay);
    updatePath(pl, c.counterOverlay);
    updateRings(pl, c.counterOverlay);
    updateLast(pl, c.counterOverlay && !counting);
    updateStats(pl, c.counterList);
}

} // namespace kt::view
