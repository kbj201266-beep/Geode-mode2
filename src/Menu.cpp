// The toolkit menu: dark rounded panel, icon sidebar on the left, settings on the right.
// Built on FLAlertLayer directly (no Popup template) so it does not depend on the Popup API of a given Geode version.
#include "Common.hpp"
#include <cstdio>

using namespace geode::prelude;

namespace kt::ui {

namespace {
    constexpr ccColor3B kPanel{22, 26, 36};
    constexpr ccColor3B kSide{16, 19, 27};
    constexpr ccColor3B kAccent{77, 214, 164};
    constexpr ccColor3B kMuted{120, 128, 145};

    enum Tab { Cheats, Hitbox, Path, Display, Counter, TabCount };
    constexpr char const* kTabIcons[TabCount] = {"kt_tab_hacks.png", "kt_tab_hitbox.png", "kt_tab_path.png",
                                                  "kt_tab_display.png", "kt_tab_counter.png"};
    constexpr char const* kTabNames[TabCount] = {"Cheats", "Hitbox", "Pathfinder", "Display", "Window counter"};

    // Loads one of this mod's own sprites. Works whatever type expandSpriteName returns in this Geode version.
    CCSprite* modSprite(char const* name) {
        std::string path(Mod::get()->expandSpriteName(name));
        return CCSprite::create(path.c_str());
    }

    struct BoolRef { bool* v; bool restart; };
    struct SliderRef { float* f; int* i; float lo, hi; CCLabelBMFont* label; char const* fmt; };

    class ToolkitMenu : public FLAlertLayer {
    public:
        static ToolkitMenu* create() {
            auto r = new ToolkitMenu();
            if (r && r->setup()) { r->autorelease(); return r; }
            CC_SAFE_DELETE(r);
            return nullptr;
        }

    private:
        static constexpr float W = 460.f, H = 280.f, SIDE = 64.f;
        CCNode* m_content = nullptr;
        CCMenu* m_contentMenu = nullptr;
        std::vector<CCMenuItemSpriteExtra*> m_tabBtns;
        std::vector<BoolRef> m_bools;
        std::vector<SliderRef> m_sliders;
        CCLabelBMFont* m_status = nullptr;
        CCLabelBMFont* m_summary = nullptr;
        int m_tab = Cheats;
        float m_y = 0.f;

        bool setup() {
            if (!FLAlertLayer::init(110)) return false;
            this->setTouchEnabled(true);
            this->setKeypadEnabled(true);
            m_noElasticity = true;

            m_mainLayer = CCLayer::create();
            this->addChild(m_mainLayer);
            m_buttonMenu = CCMenu::create();
            m_buttonMenu->setPosition({0.f, 0.f});
            m_mainLayer->addChild(m_buttonMenu, 10);

            auto win = CCDirector::get()->getWinSize();
            auto origin = ccp(win.width / 2 - W / 2, win.height / 2 - H / 2);

            auto panel = CCScale9Sprite::create("square02b_001.png");
            panel->setContentSize({W, H});
            panel->setColor(kPanel);
            panel->setOpacity(245);
            panel->setPosition(win / 2);
            m_mainLayer->addChild(panel);

            auto side = CCScale9Sprite::create("square02b_001.png");
            side->setContentSize({SIDE, H - 12.f});
            side->setColor(kSide);
            side->setOpacity(255);
            side->setPosition(origin + ccp(SIDE / 2 + 6.f, H / 2));
            m_mainLayer->addChild(side);

            // sidebar tab buttons
            for (int i = 0; i < TabCount; ++i) {
                auto icon = modSprite(kTabIcons[i]);
                icon->setScale(0.34f);
                auto btn = CCMenuItemSpriteExtra::create(icon, this, menu_selector(ToolkitMenu::onTab));
                btn->setTag(i);
                btn->setPosition(origin + ccp(SIDE / 2 + 6.f, H - 40.f - i * 46.f));
                m_buttonMenu->addChild(btn);
                m_tabBtns.push_back(btn);
            }

            // close
            auto x = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
            x->setScale(0.6f);
            auto close = CCMenuItemSpriteExtra::create(x, this, menu_selector(ToolkitMenu::onClose));
            close->setPosition(origin + ccp(W - 16.f, H - 16.f));
            m_buttonMenu->addChild(close);

            m_content = CCNode::create();
            m_content->setPosition(origin + ccp(SIDE + 26.f, H - 20.f));
            m_mainLayer->addChild(m_content, 5);

            this->schedule(schedule_selector(ToolkitMenu::tick), 0.2f);
            rebuild();
            this->show();
            return true;
        }

        void keyBackClicked() override { onClose(nullptr); }
        void onClose(CCObject*) {
            Cfg::get().save();
            this->setKeypadEnabled(false);
            this->removeFromParentAndCleanup(true);
        }
        void onTab(CCObject* s) {
            m_tab = static_cast<CCNode*>(s)->getTag();
            rebuild();
        }

        // ---------- layout helpers ----------
        CCLabelBMFont* text(std::string const& s, float x, float y, float scale, ccColor3B col, CCNode* parent, float anchorX = 0.f) {
            auto l = CCLabelBMFont::create(s.c_str(), "bigFont.fnt");
            l->setScale(scale);
            l->setColor(col);
            l->setAnchorPoint({anchorX, 0.5f});
            l->setPosition({x, y});
            parent->addChild(l);
            return l;
        }

        void addToggle(char const* label, bool& ref, bool restart = false) {
            float contentW = W - SIDE - 56.f;
            text(label, 0.f, m_y, 0.38f, {225, 230, 240}, m_content);
            auto t = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ToolkitMenu::onToggle), 0.55f);
            t->toggle(ref);
            t->setTag(static_cast<int>(m_bools.size()));
            t->setPosition({contentW, m_y});
            m_contentMenu->addChild(t);
            m_bools.push_back({&ref, restart});
            m_y -= 30.f;
        }

        void addSlider(char const* label, float* f, int* i, float lo, float hi, char const* fmt) {
            float contentW = W - SIDE - 56.f;
            text(label, 0.f, m_y, 0.38f, {225, 230, 240}, m_content);
            auto sl = Slider::create(this, menu_selector(ToolkitMenu::onSlider), 0.55f);
            float cur = f ? *f : static_cast<float>(*i);
            sl->setValue((cur - lo) / (hi - lo));
            sl->setPosition({contentW - 78.f, m_y});
            sl->getThumb()->setTag(static_cast<int>(m_sliders.size()));
            m_content->addChild(sl);
            char buf[32];
            std::snprintf(buf, sizeof buf, fmt, cur);
            auto val = text(buf, contentW + 10.f, m_y, 0.32f, kAccent, m_content, 1.f);
            m_sliders.push_back({f, i, lo, hi, val, fmt});
            m_y -= 30.f;
        }

        void onToggle(CCObject* s) {
            auto t = static_cast<CCMenuItemToggler*>(s);
            auto& r = m_bools[t->getTag()];
            *r.v = !t->isToggled();
            Cfg::get().save();
            if (r.restart) Notification::create("Applies after restarting the level", NotificationIcon::Info, 1.6f)->show();
        }

        void onSlider(CCObject* s) {
            float v = static_cast<SliderThumb*>(s)->getValue();
            auto& r = m_sliders[static_cast<CCNode*>(s)->getTag()];
            float val = r.lo + v * (r.hi - r.lo);
            char buf[32];
            if (r.i) { *r.i = static_cast<int>(std::round(val)); std::snprintf(buf, sizeof buf, r.fmt, static_cast<float>(*r.i)); }
            else { *r.f = std::round(val * 100.f) / 100.f; std::snprintf(buf, sizeof buf, r.fmt, *r.f); }
            r.label->setString(buf);
        }

        CCMenuItemSpriteExtra* addButton(char const* caption, float x, float y, SEL_MenuHandler cb, char const* tex) {
            auto spr = ButtonSprite::create(caption, 70, true, "bigFont.fnt", tex, 24.f, 0.5f);
            auto b = CCMenuItemSpriteExtra::create(spr, this, cb);
            b->setPosition({x, y});
            m_contentMenu->addChild(b);
            return b;
        }

        // ---------- tabs ----------
        void rebuild() {
            m_content->removeAllChildrenWithCleanup(true);
            m_bools.clear();
            m_sliders.clear();
            m_status = m_summary = nullptr;
            m_contentMenu = CCMenu::create();
            m_contentMenu->setPosition({0.f, 0.f});
            m_content->addChild(m_contentMenu, 2);
            m_y = -14.f;

            for (int i = 0; i < TabCount; ++i) {
                auto spr = static_cast<CCSprite*>(m_tabBtns[i]->getNormalImage());
                spr->setColor(i == m_tab ? kAccent : kMuted);
            }
            text(kTabNames[m_tab], 0.f, 0.f, 0.5f, kAccent, m_content);
            m_y = -38.f;

            auto& c = Cfg::get();
            switch (m_tab) {
                case Cheats:
                    addToggle("Noclip", c.noclip);
                    addToggle("Safe mode (no submit)", c.safeMode);
                    addToggle("Speedhack", c.speedhack);
                    addSlider("Speed", &c.speed, nullptr, 0.1f, 5.f, "%.2fx");
                    addToggle("Physical TPS", c.tpsBypass);
                    addSlider("TPS", &c.tps, nullptr, 30.f, 1000.f, "%.0f");
                    addToggle("Show TPS", c.showTps);
                    break;
                case Hitbox:
                    addToggle("Show hitboxes", c.hitboxes);
                    addToggle("Player hitbox", c.playerHitbox);
                    addToggle("Hitbox trail", c.hitboxTrail);
                    addSlider("Trail length", nullptr, &c.trailLen, 5.f, 200.f, "%.0f");
                    addSlider("Fill opacity", &c.hitFill, nullptr, 0.f, 1.f, "%.2f");
                    addSlider("Border width", &c.hitBorder, nullptr, 0.5f, 4.f, "%.1f");
                    break;
                case Path:
                    addToggle("Pathfinder (experimental)", c.pathfinder);
                    addSlider("Length", nullptr, &c.pathLen, 10, 300, "%.0f");
                    text("green = hold   red = release", 0.f, m_y - 2.f, 0.3f, kMuted, m_content);
                    break;
                case Display:
                    addToggle("Layout mode", c.layoutMode, true);
                    addToggle("Practice music sync", c.practiceSync);
                    addToggle("Window cue on screen", c.counterHud);
                    addToggle("Window cue sound", c.counterSfx);
                    break;
                case Counter: buildCounter(); break;
                default: break;
            }
        }

        void buildCounter() {
            float cw = W - SIDE - 56.f;
            m_status = text("", 0.f, -42.f, 0.36f, {225, 230, 240}, m_content);
            addButton("Record", 38.f, -72.f, menu_selector(ToolkitMenu::onRecord), "GJ_button_01.png");
            addButton("Stop", 116.f, -72.f, menu_selector(ToolkitMenu::onStopRec), "GJ_button_06.png");
            addButton("Count", 194.f, -72.f, menu_selector(ToolkitMenu::onCount), "GJ_button_01.png");
            addButton("Cancel", 272.f, -72.f, menu_selector(ToolkitMenu::onCancel), "GJ_button_06.png");
            addButton("Clear", cw - 28.f, -72.f, menu_selector(ToolkitMenu::onClear), "GJ_button_06.png");
            m_summary = text("", 0.f, -132.f, 0.3f, kMuted, m_content);
            m_summary->setAlignment(kCCTextAlignmentLeft);
            m_summary->setAnchorPoint({0.f, 1.f});
            text("1) Record  2) restart the level and clear it  3) Count", 0.f, -98.f, 0.28f, kMuted, m_content);
            tick(0.f);
        }

        void tick(float) {
            if (m_status) m_status->setString(counter::statusText().c_str());
            if (m_summary) m_summary->setString(counter::summaryText().c_str());
        }

        void needLevel() { Notification::create("Open this from the pause menu inside a level", NotificationIcon::Warning, 2.f)->show(); }
        void onRecord(CCObject*) { if (!PlayLayer::get()) return needLevel(); counter::armRecording(); }
        void onStopRec(CCObject*) { counter::stopRecording(); }
        void onCount(CCObject*) {
            if (!PlayLayer::get()) return needLevel();
            counter::startCounting();
            Notification::create("Resume the game - counting runs automatically", NotificationIcon::Info, 2.f)->show();
        }
        void onCancel(CCObject*) { counter::cancelCounting(); }
        void onClear(CCObject*) { counter::clearAll(); }
    };
}

void open() {
    ToolkitMenu::create();
}

CCMenuItemSpriteExtra* makeButton(CCObject* target, SEL_MenuHandler cb, float scale) {
    auto spr = modSprite("kt_menu.png");
    spr->setScale(scale);
    auto btn = CCMenuItemSpriteExtra::create(spr, target, cb);
    btn->setID("kimyy.gd-toolkit/toolkit-button");
    return btn;
}

} // namespace kt::ui