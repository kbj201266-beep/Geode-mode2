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
}
