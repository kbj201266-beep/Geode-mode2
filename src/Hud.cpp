#include "Common.hpp"
#include <cstdio>

using namespace geode::prelude;

namespace kt::hud {

namespace {
    constexpr char const* kInfoId = "kt-hud-info";
    constexpr char const* kFlashId = "kt-hud-flash";
}

void attach(PlayLayer* pl) {
    auto win = CCDirector::get()->getWinSize();

    auto info = CCLabelBMFont::create("", "bigFont.fnt");
    info->setID(kInfoId);
    info->setScale(0.32f);
    info->setAnchorPoint({1.f, 0.f});          // bottom right, so it never covers the counter stats
    info->setAlignment(kCCTextAlignmentRight);
    info->setOpacity(190);
    info->setPosition({win.width - 8.f, 8.f});
    pl->m_uiLayer->addChild(info, 100);

    auto flash = CCLabelBMFont::create("", "bigFont.fnt");
    flash->setID(kFlashId);
    flash->setScale(0.9f);
    flash->setOpacity(0);
    flash->setColor({77, 214, 164});
    flash->setPosition({win.width / 2.f, win.height * 0.78f});
    pl->m_uiLayer->addChild(flash, 100);
}

void update(PlayLayer* pl) {
    auto info = static_cast<CCLabelBMFont*>(pl->m_uiLayer->getChildByID(kInfoId));
    if (!info) return;
    auto& c = Cfg::get();
    std::string text;
    char buf[64];
    if (c.showTps) {
        std::snprintf(buf, sizeof buf, "TPS %.0f\n", speed::measuredTps());
        text += buf;
    }
    if (c.noclip) {
        std::snprintf(buf, sizeof buf, "Noclip %.1f%%\n", noclip::accuracy());
        text += buf;
    }
    if (counter::isRecording()) text += counter::statusText() + "\n";   // counting has its own big text
    info->setString(text.c_str());
}

void flash(std::string const& text) {
    auto pl = PlayLayer::get();
    if (!pl) return;
    auto f = static_cast<CCLabelBMFont*>(pl->m_uiLayer->getChildByID(kFlashId));
    if (!f) return;
    f->stopAllActions();
    f->setString(text.c_str());
    f->setOpacity(255);
    f->runAction(CCSequence::create(CCDelayTime::create(0.15f), CCFadeOut::create(0.45f), nullptr));
}

} // namespace kt::hud
