// Small info text (bottom right) and the big "3f" flash in the middle.
// Performance: the labels are kept as pointers and the text is only set again when it really changed
// (setting a bitmap font label rebuilds all its letters, which is expensive when done every frame).
#include "Common.hpp"
#include <cstdio>

using namespace geode::prelude;

namespace kt::hud {

namespace {
    CCLabelBMFont* g_info = nullptr;
    CCLabelBMFont* g_flash = nullptr;
    std::string g_infoText;
    PlayLayer* g_owner = nullptr;
}

void attach(PlayLayer* pl) {
    auto win = CCDirector::get()->getWinSize();
    g_owner = pl;

    g_info = CCLabelBMFont::create("", "bigFont.fnt");
    g_info->setScale(0.32f);
    g_info->setAnchorPoint({1.f, 0.f});          // bottom right, so it never covers the counter stats
    g_info->setAlignment(kCCTextAlignmentRight);
    g_info->setOpacity(190);
    g_info->setPosition({win.width - 8.f, 8.f});
    pl->m_uiLayer->addChild(g_info, 100);
    g_infoText.clear();

    g_flash = CCLabelBMFont::create("", "bigFont.fnt");
    g_flash->setScale(0.9f);
    g_flash->setOpacity(0);
    g_flash->setColor({77, 214, 164});
    g_flash->setPosition({win.width / 2.f, win.height * 0.78f});
    pl->m_uiLayer->addChild(g_flash, 100);
}

void update(PlayLayer* pl) {
    if (!g_info || pl != g_owner) return;
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
    if (text == g_infoText) return;
    g_infoText = text;
    g_info->setString(text.c_str());
}

void flash(std::string const& text) {
    if (!g_flash || !g_owner || PlayLayer::get() != g_owner) return;
    g_flash->stopAllActions();
    g_flash->setString(text.c_str());
    g_flash->setOpacity(255);
    g_flash->runAction(CCSequence::create(CCDelayTime::create(0.15f), CCFadeOut::create(0.45f), nullptr));
}

} // namespace kt::hud
