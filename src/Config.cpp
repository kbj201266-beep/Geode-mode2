#include "Config.hpp"

Cfg& Cfg::get() {
    static Cfg c;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        c.load();
    }
    return c;
}

void Cfg::load() {
    auto m = geode::Mod::get();
#define X(n, d) n = m->getSavedValue<decltype(n)>(#n, d);
    KT_BOOLS(X)
    KT_FLOATS(X)
    KT_INTS(X)
#undef X
}

void Cfg::save() const {
    auto m = geode::Mod::get();
#define X(n, d) m->setSavedValue<decltype(n)>(#n, n);
    KT_BOOLS(X)
    KT_FLOATS(X)
    KT_INTS(X)
#undef X
    // Write to disk now. Otherwise settings only reach the disk when the game closes normally - on a phone the
    // game is usually swiped away, so changes (like TPS back to 240) were lost and the old value came back.
    (void)m->saveData();
}
