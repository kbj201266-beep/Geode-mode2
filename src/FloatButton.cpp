// The menu button: big, draggable, and present in every scene (it lives in Geode's OverlayManager).
// Tap = open the menu. Press and drag = move it; the position is remembered.
#include "Common.hpp"
#include <Geode/ui/OverlayManager.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace kt::ui {

CCSprite* modSprite(char const* name) {
    std::string path(Mod::get()->expandSpriteName(name));
    return CCSprite::create(path.c_str());
}

namespace {
    constexpr char const* kId = "kimyy.gd-toolkit/float-button";

    class FloatButton : public CCNode, public CCTouchDelegate {
    public:
        static FloatButton* create() {
            auto r = new FloatButton();
            if (r && r->setup()) { r->autorelease(); return r; }
            CC_SAFE_DELETE(r);
            return nullptr;
        }

        void applyScale() { m_icon->setScale(Cfg::get().btnScale); }
        void applyPos() {
            auto win = CCDirector::get()->getWinSize();
            this->setPosition({win.width * Cfg::get().btnX, win.height * Cfg::get().btnY});
        }

        void onEnter() override {
            CCNode::onEnter();
            CCTouchDispatcher::get()->addTargetedDelegate(this, -400, true);
        }
        void onExit() override {
            CCTouchDispatcher::get()->removeDelegate(this);
            CCNode::onExit();
        }

        bool ccTouchBegan(CCTouch* t, CCEvent*) override {
            if (!this->isVisible()) return false;
            auto p = this->convertTouchToNodeSpace(t);
            float radius = m_icon->getContentSize().width * m_icon->getScale() / 2.f;
            if (std::sqrt(p.x * p.x + p.y * p.y) > radius) return false;
            m_down = true;
            m_drag = false;
            m_start = t->getLocation();
            return true;
        }

        void ccTouchMoved(CCTouch* t, CCEvent*) override {
            if (!m_down) return;
            auto loc = t->getLocation();
            float dx = loc.x - m_start.x, dy = loc.y - m_start.y;
            if (!m_drag && std::sqrt(dx * dx + dy * dy) > 10.f) m_drag = true;
            if (!m_drag) return;
            auto win = CCDirector::get()->getWinSize();
            auto np = this->getParent()->convertToNodeSpace(loc);
            np.x = std::clamp(np.x, 10.f, win.width - 10.f);
            np.y = std::clamp(np.y, 10.f, win.height - 10.f);
            this->setPosition(np);
        }

        void ccTouchEnded(CCTouch*, CCEvent*) override {
            if (!m_down) return;
            m_down = false;
            if (m_drag) {
                auto win = CCDirector::get()->getWinSize();
                auto& c = Cfg::get();
                c.btnX = this->getPositionX() / win.width;
                c.btnY = this->getPositionY() / win.height;
                c.save();
            } else if (!isOpen()) {
                open();
            }
        }

        void ccTouchCancelled(CCTouch*, CCEvent*) override { m_down = false; }

        // Hidden while a level is being played (normal or practice) and while the menu is open.
        // In a level it shows up again when you pause and on the level-complete screen.
        void tick(float) {
            bool hide = isOpen();
            if (auto pl = PlayLayer::get()) {
                bool finished = pl->m_hasCompletedLevel || pl->m_levelEndAnimationStarted;
                bool playing = !pl->m_isPaused && !finished;
                hide = hide || playing || counter::isCounting();
            }
            if (hide && m_down) m_down = false;
            this->setVisible(!hide);
        }

    private:
        CCSprite* m_icon = nullptr;
        bool m_down = false, m_drag = false;
        CCPoint m_start;

        bool setup() {
            if (!CCNode::init()) return false;
            m_icon = modSprite("kt_menu.png");
            m_icon->setOpacity(225);
            this->addChild(m_icon);
            applyScale();
            applyPos();
            this->setID(kId);
            this->schedule(schedule_selector(FloatButton::tick), 0.1f);
            return true;
        }
    };

    FloatButton* find() {
        return static_cast<FloatButton*>(OverlayManager::get()->getChildByID(kId));
    }
}

void spawnButton() {
    if (find()) return;
    if (auto b = FloatButton::create()) OverlayManager::get()->addChild(b, 1000);
}

void refreshButton() {
    if (auto b = find()) b->applyScale();
}

} // namespace kt::ui
