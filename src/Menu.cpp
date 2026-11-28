// The toolkit menu: dark rounded panel, icon sidebar on the left, settings on the right.
// Built on FLAlertLayer directly (no Popup template) so it does not depend on the Popup API of a given Geode version.
#include "Common.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

using namespace geode::prelude;

namespace kt::ui {

namespace {
    constexpr ccColor3B kPanel{22, 26, 36};
    constexpr ccColor3B kSide{16, 19, 27};
    constexpr ccColor3B kAccent{77, 214, 164};
    constexpr ccColor3B kMuted{120, 128, 145};
    constexpr ccColor3B kText{225, 230, 240};

    enum Tab { Cheats, Hitbox, Path, Display, Replay, Counter, TabCount };
    constexpr char const* kTabIcons[TabCount] = {"kt_tab_hacks.png", "kt_tab_hitbox.png", "kt_tab_path.png",
                                                  "kt_tab_display.png", "kt_tab_speed.png", "kt_tab_counter.png"};
    constexpr char const* kTabNames[TabCount] = {"Cheats", "Hitbox", "Pathfinder", "Display", "Replay", "Window counter"};

    bool g_open = false;

    void note(std::string const& msg) {
        Notification::create(msg.c_str(), NotificationIcon::Info, 1.8f)->show();
    }

    struct BoolRef { bool* v; bool restart; };
    struct SliderRef {
        float* f; int* i; float lo, hi;
        CCLabelBMFont* label;      // value text (plain sliders)
        TextInput* input;          // typed value (editable sliders)
        Slider* slider;
        char const* fmt;
    };

    // ---------------------------------------------------------------- name prompt (New replay)
    class NamePrompt : public FLAlertLayer {
    public:
        static NamePrompt* create(std::function<void(std::string const&)> cb) {
            auto r = new NamePrompt();
            r->m_cb = std::move(cb);
            if (r->setup()) { r->autorelease(); return r; }
            CC_SAFE_DELETE(r);
            return nullptr;
        }

    private:
        std::function<void(std::string const&)> m_cb;
        TextInput* m_input = nullptr;

        bool setup() {
            if (!FLAlertLayer::init(150)) return false;
            this->setTouchEnabled(true);
            this->setKeypadEnabled(true);
            m_noElasticity = true;

            m_mainLayer = CCLayer::create();
            this->addChild(m_mainLayer);
            m_buttonMenu = CCMenu::create();
            m_buttonMenu->setPosition({0.f, 0.f});
            m_mainLayer->addChild(m_buttonMenu, 10);

            auto win = CCDirector::get()->getWinSize();
            auto c = ccp(win.width / 2, win.height / 2);

            auto panel = CCScale9Sprite::create("square02b_001.png");
            panel->setContentSize({290.f, 150.f});
            panel->setColor(kPanel);
            panel->setOpacity(250);
            panel->setPosition(c);
            m_mainLayer->addChild(panel);

            auto title = CCLabelBMFont::create("New replay", "bigFont.fnt");
            title->setScale(0.5f);
            title->setColor(kAccent);
            title->setPosition(c + ccp(0.f, 50.f));
            m_mainLayer->addChild(title);

            m_input = TextInput::create(230.f, "Replay name");
            m_input->setPosition(c + ccp(0.f, 8.f));
            m_input->setMaxCharCount(24);
            m_input->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-");
            m_mainLayer->addChild(m_input, 5);

            auto cancel = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("Cancel", 90, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.5f), this,
                menu_selector(NamePrompt::onCancel));
            cancel->setPosition(c + ccp(-62.f, -44.f));
            m_buttonMenu->addChild(cancel);
            auto create = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("Create", 90, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.5f), this,
                menu_selector(NamePrompt::onCreate));
            create->setPosition(c + ccp(62.f, -44.f));
            m_buttonMenu->addChild(create);

            this->show();
            return true;
        }

        void close() {
            this->setKeypadEnabled(false);
            this->removeFromParentAndCleanup(true);
        }
        void keyBackClicked() override { close(); }
        void onCancel(CCObject*) { close(); }
        void onCreate(CCObject*) {
            auto cb = m_cb;
            std::string name = m_input->getString();
            close();
            if (cb) cb(name);
        }
    };

    // ---------------------------------------------------------------- main menu
    class ToolkitMenu : public FLAlertLayer {
    public:
        static ToolkitMenu* create() {
            auto r = new ToolkitMenu();
            if (r && r->setup()) { r->autorelease(); return r; }
            CC_SAFE_DELETE(r);
            return nullptr;
        }
        ~ToolkitMenu() {
            g_open = false;
            Cfg::get().save();      // slider changes are saved however the menu gets closed
        }

    private:
        // the sidebar holds the 6 tab buttons in 2 columns x 3 rows, so each button can be big and easy to tap
        static constexpr float H = 300.f, SIDE = 150.f;
        float W = 600.f;                   // set from the screen width in setup()
        CCNode* m_content = nullptr;
        CCMenu* m_contentMenu = nullptr;
        std::vector<CCMenuItemSpriteExtra*> m_tabBtns;
        std::vector<BoolRef> m_bools;
        std::vector<SliderRef> m_sliders;
        CCLabelBMFont* m_status = nullptr;
        CCLabelBMFont* m_summary = nullptr;
        CCLabelBMFont* m_nameLabel = nullptr;
        CCLabelBMFont* m_cardName = nullptr;
        CCLabelBMFont* m_cardSub = nullptr;
        CCMenuItemToggler* m_radios[3] = {nullptr, nullptr, nullptr};
        int m_tab = Replay;
        float m_y = 0.f;

        bool setup() {
            if (!FLAlertLayer::init(110)) return false;
            this->setTouchEnabled(true);
            this->setKeypadEnabled(true);
            m_noElasticity = true;
            g_open = true;

            m_mainLayer = CCLayer::create();
            this->addChild(m_mainLayer);
            m_buttonMenu = CCMenu::create();
            m_buttonMenu->setPosition({0.f, 0.f});
            m_mainLayer->addChild(m_buttonMenu, 10);

            auto win = CCDirector::get()->getWinSize();
            W = std::clamp(win.width - 16.f, 500.f, 640.f);
            auto origin = ccp(win.width / 2 - W / 2, win.height / 2 - H / 2);

            auto panel = CCScale9Sprite::create("square02b_001.png");
            panel->setContentSize({W, H});
            panel->setColor(kPanel);
            panel->setOpacity(245);
            panel->setPosition(ccp(win.width / 2, win.height / 2));
            m_mainLayer->addChild(panel);

            auto side = CCScale9Sprite::create("square02b_001.png");
            side->setContentSize({SIDE, H - 12.f});
            side->setColor(kSide);
            side->setOpacity(255);
            side->setPosition(origin + ccp(SIDE / 2 + 6.f, H / 2));
            m_mainLayer->addChild(side);

            // sidebar tab buttons: big icons (64 px) with their name under them, 2 columns x 3 rows
            for (int i = 0; i < TabCount; ++i) {
                float cx = 6.f + (i % 2 == 0 ? SIDE * 0.25f + 2.f : SIDE * 0.75f - 2.f);
                float cy = H - 62.f - (i / 2) * 88.f;
                auto icon = modSprite(kTabIcons[i]);
                icon->setScale(0.5f);
                auto btn = CCMenuItemSpriteExtra::create(icon, this, menu_selector(ToolkitMenu::onTab));
                btn->setTag(i);
                btn->setPosition(origin + ccp(cx, cy));
                m_buttonMenu->addChild(btn);
                m_tabBtns.push_back(btn);
                auto name = CCLabelBMFont::create(kTabNames[i], "bigFont.fnt");
                name->setScale(0.22f);
                name->setColor(kMuted);
                name->setPosition(origin + ccp(cx, cy - 40.f));
                m_mainLayer->addChild(name, 6);
            }

            // close
            auto x = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
            x->setScale(0.6f);
            auto close = CCMenuItemSpriteExtra::create(x, this, menu_selector(ToolkitMenu::onClose));
            close->setPosition(origin + ccp(W - 16.f, H - 16.f));
            m_buttonMenu->addChild(close);

            // the version shows which build is really installed (check it after every GitHub build)
            std::string titleText = "GD Toolkit " + Mod::get()->getVersion().toVString();
            auto title = CCLabelBMFont::create(titleText.c_str(), "goldFont.fnt");
            title->setScale(0.6f);
            title->setPosition(ccp(win.width / 2 + SIDE / 2, origin.y + H - 16.f));
            m_mainLayer->addChild(title, 6);

            m_content = CCNode::create();
            m_content->setPosition(origin + ccp(SIDE + 26.f, H - 40.f));
            m_mainLayer->addChild(m_content, 5);

            this->schedule(schedule_selector(ToolkitMenu::tick), 0.2f);
            rebuild();
            this->show();
            return true;
        }

        void keyBackClicked() override { onClose(nullptr); }
        void onClose(CCObject*) {
            Cfg::get().save();
            g_open = false;
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

        float contentW() const { return std::max(320.f, W - SIDE - 56.f); }

        void addToggle(char const* label, bool& ref, bool restart = false) {
            text(label, 0.f, m_y, 0.38f, kText, m_content);
            auto t = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ToolkitMenu::onToggle), 0.55f);
            t->toggle(ref);
            t->setTag(static_cast<int>(m_bools.size()));
            t->setPosition({contentW(), m_y});
            m_contentMenu->addChild(t);
            m_bools.push_back({&ref, restart});
            m_y -= 30.f;
        }

        // editable = a small text box next to the slider where the exact value can be typed
        void addSlider(char const* label, float* f, int* i, float lo, float hi, char const* fmt, bool editable = false) {
            text(label, 0.f, m_y, 0.38f, kText, m_content);
            auto sl = Slider::create(this, menu_selector(ToolkitMenu::onSlider), 0.55f);
            float cur = f ? *f : static_cast<float>(*i);
            sl->setValue((cur - lo) / (hi - lo));
            sl->setPosition({contentW() - (editable ? 100.f : 78.f), m_y});
            size_t idx = m_sliders.size();
            sl->getThumb()->setTag(static_cast<int>(idx));
            m_content->addChild(sl);
            char buf[32];
            std::snprintf(buf, sizeof buf, fmt, cur);
            CCLabelBMFont* val = nullptr;
            TextInput* input = nullptr;
            if (editable) {
                input = TextInput::create(60.f, "");
                input->setScale(0.75f);
                input->setPosition({contentW() - 12.f, m_y});
                input->setFilter("0123456789.");
                input->setMaxCharCount(7);
                input->setString(buf);
                input->setCallback([this, idx](std::string const& s) { onTyped(idx, s); });
                m_content->addChild(input, 4);
            } else {
                val = text(buf, contentW() + 10.f, m_y, 0.32f, kAccent, m_content, 1.f);
            }
            m_sliders.push_back({f, i, lo, hi, val, input, sl, fmt});
            m_y -= 30.f;
        }

        void onTyped(size_t idx, std::string const& s) {
            if (idx >= m_sliders.size()) return;
            auto& r = m_sliders[idx];
            char* end = nullptr;
            float v = std::strtof(s.c_str(), &end);
            if (end == s.c_str()) return;            // nothing parseable yet
            v = std::clamp(v, r.lo, r.hi);
            if (r.i) *r.i = static_cast<int>(std::round(v));
            else *r.f = v;
            r.slider->setValue((v - r.lo) / (r.hi - r.lo));
            Cfg::get().save();
        }

        void onToggle(CCObject* s) {
            auto t = static_cast<CCMenuItemToggler*>(s);
            auto& r = m_bools[t->getTag()];
            *r.v = !t->isToggled();
            Cfg::get().save();
            if (r.restart) note("Applies after restarting the level");
        }

        void onSlider(CCObject* s) {
            float v = static_cast<SliderThumb*>(s)->getValue();
            auto& r = m_sliders[static_cast<CCNode*>(s)->getTag()];
            float val = r.lo + v * (r.hi - r.lo);
            char buf[32];
            if (r.i) { *r.i = static_cast<int>(std::round(val)); std::snprintf(buf, sizeof buf, r.fmt, static_cast<float>(*r.i)); }
            else { *r.f = std::round(val * 100.f) / 100.f; std::snprintf(buf, sizeof buf, r.fmt, *r.f); }
            if (r.label) r.label->setString(buf);
            if (r.input) r.input->setString(buf);
            refreshButton();
        }

        CCMenuItemSpriteExtra* addButton(char const* caption, float x, float y, SEL_MenuHandler cb, char const* tex, int width = 70) {
            auto spr = ButtonSprite::create(caption, width, true, "bigFont.fnt", tex, 24.f, 0.5f);
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
            m_status = m_summary = m_nameLabel = m_cardName = m_cardSub = nullptr;
            for (auto& r : m_radios) r = nullptr;
            m_contentMenu = CCMenu::create();
            m_contentMenu->setPosition({0.f, 0.f});
            m_content->addChild(m_contentMenu, 2);

            for (int i = 0; i < TabCount; ++i) {
                auto spr = static_cast<CCSprite*>(m_tabBtns[i]->getNormalImage());
                spr->setColor(i == m_tab ? kAccent : kMuted);
            }
            auto tabTitle = CCLabelBMFont::create(kTabNames[m_tab], "goldFont.fnt");
            tabTitle->setScale(0.55f);
            tabTitle->setAnchorPoint({0.f, 0.5f});
            m_content->addChild(tabTitle);
            m_y = -30.f;

            auto& c = Cfg::get();
            switch (m_tab) {
                case Cheats:
                    addToggle("Noclip", c.noclip);
                    addToggle("Safe mode (no submit)", c.safeMode);
                    addToggle("Speedhack", c.speedhack);
                    addSlider("Speed", &c.speed, nullptr, 0.01f, 10.f, "%.2f", true);
                    addToggle("Physical TPS (240+)", c.tpsBypass);
                    addSlider("TPS", &c.tps, nullptr, 240.f, 1000.f, "%.0f", true);
                    addToggle("Show TPS", c.showTps);
                    break;
                case Hitbox:
                    addToggle("Show hitboxes", c.hitboxes);
                    addToggle("Player hitbox", c.playerHitbox);
                    addToggle("Hitbox trail", c.hitboxTrail);
                    addToggle("Hitborders (NaN style)", c.hitborders);
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
                    addToggle("Layout mode (re-open level)", c.layoutMode, true);
                    addToggle("Practice music sync", c.practiceSync);
                    addToggle("Counter overlay", c.counterOverlay);
                    addToggle("Window cue on screen", c.counterHud);
                    addToggle("Window cue sound", c.counterSfx);
                    addSlider("Menu button size", &c.btnScale, nullptr, 0.3f, 1.2f, "%.2f");
                    text("drag the round button to move it", 0.f, m_y - 2.f, 0.3f, kMuted, m_content);
                    break;
                case Replay: buildReplay(); break;
                case Counter: buildCounter(); break;
                default: break;
            }
            tick(0.f);
        }

        // Replay tab: same layout as a bot menu - mode, replay picker, ignore inputs, file buttons.
        void buildReplay() {
            float cw = contentW();
            char const* names[3] = {"Disabled", "Record", "Playback"};
            float xs[3] = {2.f, 108.f, 214.f};
            for (int i = 0; i < 3; ++i) {
                auto t = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(ToolkitMenu::onMode), 0.55f);
                t->setTag(i);
                t->setPosition({xs[i] + 12.f, m_y});
                m_contentMenu->addChild(t);
                m_radios[i] = t;
                text(names[i], xs[i] + 30.f, m_y, 0.36f, kText, m_content);
            }
            m_y -= 30.f;

            text("Replays", 0.f, m_y, 0.38f, kText, m_content);
            auto left = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            left->setScale(0.5f);
            auto lb = CCMenuItemSpriteExtra::create(left, this, menu_selector(ToolkitMenu::onPrev));
            lb->setPosition({cw - 178.f, m_y});
            m_contentMenu->addChild(lb);
            auto right = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            right->setScale(0.5f);
            right->setFlipX(true);
            auto rb = CCMenuItemSpriteExtra::create(right, this, menu_selector(ToolkitMenu::onNext));
            rb->setPosition({cw - 12.f, m_y});
            m_contentMenu->addChild(rb);
            m_nameLabel = text("", cw - 95.f, m_y, 0.32f, kAccent, m_content, 0.5f);
            m_y -= 30.f;

            addToggle("Ignore Inputs", Cfg::get().ignoreInputs);

            float bx = cw / 2.f;
            char const* tex = "GJ_button_04.png";
            addButton("New", bx, m_y, menu_selector(ToolkitMenu::onNew), tex, 230);
            m_y -= 28.f;
            addButton("Save", bx, m_y, menu_selector(ToolkitMenu::onSave), tex, 230);
            m_y -= 28.f;
            addButton("Load", bx, m_y, menu_selector(ToolkitMenu::onLoad), tex, 230);
            m_y -= 28.f;
            addButton("Delete", bx, m_y, menu_selector(ToolkitMenu::onDelete), tex, 230);
            m_y -= 28.f;
            addButton("Open Replays Folder", bx, m_y, menu_selector(ToolkitMenu::onFolder), tex, 230);
        }

        // Window counter tab, laid out like Antic's Counter: replay card, Count / Stop, Save On Finish, Copy Info.
        void buildCounter() {
            float cw = contentW();
            auto card = CCScale9Sprite::create("square02b_001.png");
            card->setContentSize({cw + 16.f, 40.f});
            card->setColor({34, 39, 52});
            card->setOpacity(255);
            card->setPosition({cw / 2.f, m_y - 10.f});
            m_content->addChild(card, 1);
            m_cardName = text("", 0.f, m_y - 2.f, 0.4f, {255, 255, 255}, m_content);
            m_cardSub = text("", 0.f, m_y - 19.f, 0.26f, kMuted, m_content);
            m_cardName->setZOrder(3);
            m_cardSub->setZOrder(3);
            addButton("Change", cw - 26.f, m_y - 10.f, menu_selector(ToolkitMenu::onChange), "GJ_button_02.png", 56);
            m_y -= 50.f;

            addButton("Count Macro", cw * 0.25f, m_y, menu_selector(ToolkitMenu::onCount), "GJ_button_01.png", 150);
            addButton("Stop", cw * 0.75f, m_y, menu_selector(ToolkitMenu::onCancel), "GJ_button_06.png", 150);
            m_y -= 32.f;
            addToggle("Save On Finish", Cfg::get().saveOnFinish);
            addToggle("Show Results List", Cfg::get().counterList);
            m_y += 2.f;
            addButton("Copy Info", cw / 2.f, m_y, menu_selector(ToolkitMenu::onCopy), "GJ_button_04.png", 316);
            m_y -= 26.f;

            m_status = text("", 0.f, m_y, 0.3f, kAccent, m_content);
            m_summary = text("", 0.f, m_y - 12.f, 0.26f, kText, m_content);
            m_summary->setAnchorPoint({0.f, 1.f});

            // other mods that hook the same game functions (bots, noclip, speedhack, practice fix): say which
            // ones are installed and what to turn off, since their features change the runs being counted
            auto loaded = [](char const* id) { return Loader::get()->isModLoaded(id); };
            std::string others;
            if (loaded("peony.silicate")) others += "Silicate ";
            if (loaded("thesillydoggo.qolmod")) others += "QOLMod ";
            if (loaded("anticroom.antics-counter")) others += "Antic's-Counter ";
            if (!others.empty()) {
                text("Also installed: " + others, 0.f, -H + 46.f, 0.22f, kAccent, m_content);
                text("While counting / recording, turn THEIR noclip, speedhack,", 0.f, -H + 37.f, 0.22f, kMuted, m_content);
                text("practice fix and bot (record/playback) off.", 0.f, -H + 28.f, 0.22f, kMuted, m_content);
            }
        }

        void tick(float) {
            if (m_status) m_status->setString(counter::statusText().c_str());
            if (m_summary) m_summary->setString(counter::summaryText().c_str());
            if (m_cardName) {
                auto n = counter::currentName();
                if (n.size() > 22) n = n.substr(0, 20) + "..";
                m_cardName->setString(n.c_str());
            }
            if (m_cardSub) m_cardSub->setString(counter::macroInfo().c_str());
            if (m_nameLabel) {
                auto n = counter::currentName();
                if (n.size() > 18) n = n.substr(0, 16) + "..";
                m_nameLabel->setString(n.c_str());
            }
            for (int i = 0; i < 3; ++i) {
                if (m_radios[i]) m_radios[i]->toggle(static_cast<int>(counter::mode()) == i);
            }
        }

        // ---------- actions ----------
        void onMode(CCObject* s) {
            counter::setMode(static_cast<counter::Mode>(static_cast<CCNode*>(s)->getTag()));
            tick(0.f);
        }
        void onPrev(CCObject*) { counter::selectReplay(-1); tick(0.f); }
        void onNext(CCObject*) { counter::selectReplay(1); tick(0.f); }
        void onNew(CCObject*) {
            NamePrompt::create([](std::string const& n) { note(counter::newReplay(n)); });
        }
        void onSave(CCObject*) { note(counter::saveReplay()); }
        void onLoad(CCObject*) { note(counter::loadReplay()); }
        void onDelete(CCObject*) {
            createQuickPopup("Delete replay", "Delete \"" + counter::currentName() + "\"?", "No", "Yes",
                             [](FLAlertLayer*, bool yes) {
                                 if (yes) note(counter::deleteReplay());
                             });
        }
        void onFolder(CCObject*) { counter::openReplaysFolder(); }
        void onCount(CCObject*) {
            if (!PlayLayer::get()) { note("Open this inside a level"); return; }
            note(counter::startCounting());     // says exactly what happens (or why it cannot count)
            tick(0.f);
        }
        void onCancel(CCObject*) {
            if (counter::isCounting()) counter::cancelCounting();
            else counter::setMode(counter::Mode::Disabled);      // also stops playback
            tick(0.f);
        }
        void onChange(CCObject*) {
            counter::selectReplay(1);
            note(counter::loadReplay());
            tick(0.f);
        }
        void onCopy(CCObject*) {
            auto info = counter::infoText();
            utils::clipboard::write(info.c_str());
            note("Copied to clipboard");
        }
    };
}

void open() {
    if (g_open) return;
    ToolkitMenu::create();
}

bool isOpen() { return g_open; }

} // namespace kt::ui
