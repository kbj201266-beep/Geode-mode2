// Every hook of the mod lives here (one $modify per class) so two features never hook the same function twice.
#include "Common.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/GameObject.hpp>

using namespace geode::prelude;

class $modify(KtPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        kt::hitbox::reset();
        kt::path::reset();
        kt::noclip::reset();
        kt::hud::attach(this);
        kt::layout::apply(this);
        kt::counter::onLevelEnter(this);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        kt::speed::apply();
        kt::hitbox::update(this);
        kt::path::update(this);
        kt::counter::update(this);
        kt::hud::update(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (kt::path::simulating()) {          // dummy player died: never touch the real one
            kt::path::onSimDeath();
            return;
        }
        if (kt::counter::onDeath(this)) return;
        if (Cfg::get().noclip && obj != m_anticheatSpike) {
            kt::noclip::onHit();
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        kt::hitbox::reset();
        kt::noclip::reset();
        kt::counter::onReset(this);
        kt::practice::sync(this);
        kt::layout::apply(this);
    }

    void loadFromCheckpoint(CheckpointObject* cp) {
        PlayLayer::loadFromCheckpoint(cp);
        kt::practice::sync(this);
    }

    void levelComplete() {
        if (kt::counter::onComplete(this)) return;
        // Safe Mode: anything that changes gameplay marks the run as a test so nothing is submitted.
        if (Cfg::get().safeMode && Cfg::get().cheating()) m_isTestMode = true;
        PlayLayer::levelComplete();
    }
};

class $modify(KtBaseLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isHalfTick) {
            kt::speed::onStep();
            kt::noclip::onStep();
        }
        kt::counter::beforeStep(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!kt::counter::filterInput(down, button, isPlayer1)) return;
        kt::counter::onInput(this, down, button, isPlayer1);
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    double getModifiedDelta(float dt) {
        if (Cfg::get().tpsBypass) return kt::speed::modifiedDelta(this, dt);
        return GJBaseGameLayer::getModifiedDelta(dt);
    }
};

class $modify(KtGameObject, GameObject) {
    void setVisible(bool v) {
        if (v && Cfg::get().layoutMode && kt::layout::isDeco(this)) v = false;
        GameObject::setVisible(v);
    }
};

class $modify(KtPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto btn = kt::ui::makeButton(this, menu_selector(KtPause::onToolkit), 0.34f);
        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto fallback = CCMenu::create();
            fallback->setPosition({36.f, 36.f});
            fallback->addChild(btn);
            this->addChild(fallback);
        }
    }
    void onToolkit(CCObject*) { kt::ui::open(); }
};

class $modify(KtMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto btn = kt::ui::makeButton(this, menu_selector(KtMenuLayer::onToolkit), 0.30f);
        if (auto menu = this->getChildByID("bottom-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        }
        return true;
    }
    void onToolkit(CCObject*) { kt::ui::open(); }
};