#pragma once

#include "keys.h"

namespace esas {

struct Settings final {
    std::string log_level = "info";
    Keysets convert_spell_keysets = Keysets({
        {KeycodeFromName("LShift"), KeycodeFromName("=")},
        {KeycodeFromName("RShift"), KeycodeFromName("=")},
    });
    Keysets remove_shout_keysets = Keysets({
        {KeycodeFromName("LShift"), KeycodeFromName("-")},
        {KeycodeFromName("RShift"), KeycodeFromName("-")},
    });
    bool allow_2h_spells = false;
    float magicka_scale_faf = 1.f;
    float magicka_scale_conc = 1.f;
};

}  // namespace esas
