// Utilities on top of CommonLibSSE.
#pragma once

/// This is only for fmtlib (used by logging). stdlib formatting requires separate formatter
/// specializations.
template <>
struct fmt::formatter<RE::TESForm> {
    constexpr auto
    parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto
    format(const RE::TESForm& form, format_context& ctx) const {
        auto has_name = form.GetName() && *form.GetName();
        return fmt::format_to(
            ctx.out(),
            "{:08X}{}{}{}",
            form.GetFormID(),
            has_name ? " (" : "",
            has_name ? form.GetName() : "",
            has_name ? ")" : ""
        );
    }
};

template <>
struct fmt::formatter<RE::SpellItem> : public fmt::formatter<RE::TESForm> {
    auto
    format(const RE::SpellItem& form, format_context& ctx) const {
        return fmt::formatter<RE::TESForm>::format(form, ctx);
    }
};

template <>
struct fmt::formatter<RE::TESShout> : public fmt::formatter<RE::TESForm> {
    auto
    format(const RE::TESShout& form, format_context& ctx) const {
        return fmt::formatter<RE::TESForm>::format(form, ctx);
    }
};

namespace esas {
namespace tes_util {

inline constexpr RE::FormID kEqupRightHand = 0x13f42;
inline constexpr RE::FormID kEqupLeftHand = 0x13f43;
inline constexpr RE::FormID kEqupEitherHand = 0x13f44;
inline constexpr RE::FormID kEqupBothHands = 0x13f45;
inline constexpr RE::FormID kWeapDummy = 0x20163;

/// Like `RE::TESForm::LookupByID()` but logs on failure.
inline RE::TESForm*
GetForm(RE::FormID form_id) {
    auto* form = RE::TESForm::LookupByID(form_id);
    if (!form) {
        SKSE::log::trace("unknown form {:08X}", form_id);
    }
    return form;
}

/// Like `RE::TESForm::LookupByID<T>()` but logs on failure.
template <typename T>
requires(std::is_base_of_v<RE::TESForm, T>)
T*
GetForm(RE::FormID form_id) {
    auto* form = GetForm(form_id);
    if (!form) {
        return nullptr;
    }
    auto* obj = form->As<T>();
    if (!obj) {
        SKSE::log::trace("{} cannot be cast to form type {}", *form, T::FORMTYPE);
    }
    return obj;
}

/// Like `RE::TESDataHandler::GetSingleton()->LookupForm()` but logs on failure.
///
/// Also supports looking up dynamic forms where there is no modname, in which case `local_id` is
/// treated as the full form ID.
inline RE::TESForm*
GetForm(std::string_view modname, RE::FormID local_id) {
    if (modname.empty()) {
        return GetForm(local_id);
    }

    auto* data_handler = RE::TESDataHandler::GetSingleton();
    if (!data_handler) {
        SKSE::log::error("cannot get RE::TESDataHandler instance");
        return nullptr;
    }
    auto* form = data_handler->LookupForm(local_id, modname);
    if (!form) {
        SKSE::log::trace("unknown form ({}, {:08X})", modname, local_id);
    }
    return form;
}

/// Like `RE::TESDataHandler::GetSingleton()->LookupForm<T>()` but logs on failure.
///
/// Also supports looking up dynamic forms where there is no modname, in which case, `local_id` is
/// treated as the full form ID.
template <typename T>
requires(std::is_base_of_v<RE::TESForm, T>)
T*
GetForm(std::string_view modname, RE::FormID local_id) {
    auto* form = GetForm(modname, local_id);
    if (!form) {
        return nullptr;
    }
    auto* obj = form->As<T>();
    if (!obj) {
        SKSE::log::trace("{} cannot be cast to form type {}", *form, T::FORMTYPE);
    }
    return obj;
}

/// Returns `(mod name, local ID)`.
///
/// If form is a dynamic form (e.g. a custom enchantment), returns `(empty string, full form ID)`.
inline std::pair<std::string_view, RE::FormID>
GetNamedFormID(const RE::TESForm& form) {
    const auto* file = form.GetFile(0);
    if (!file) {
        return {"", form.GetFormID()};
    }
    return {file->GetFilename(), form.GetLocalFormID()};
}

template <class... Args>
inline void
DebugNotification(std::format_string<Args...> fmt, Args&&... args) {
    auto s = std::vformat(fmt.get(), std::make_format_args(args...));
    RE::DebugNotification(s.c_str());
}

/// Returns false if unable to allocate a console command execution context. Returning true means
/// the command was executed, even if that execution failed inside the console.
template <class... Args>
[[nodiscard]] inline bool
ConsoleRun(std::format_string<Args...> fmt, Args&&... args) {
    auto cmd = std::vformat(fmt.get(), std::make_format_args(args...));

    auto* fac = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
    auto* script = fac ? fac->Create() : nullptr;
    if (!script) {
        return false;
    }

    script->SetCommand(cmd);
    script->CompileAndRun(nullptr);
    delete script;
    return true;
}

/// In particular, scrolls are not considered spells.
inline bool
IsHandEquippedSpell(const RE::SpellItem& spell, bool allow_2h = true) {
    if (spell.GetFormType() != RE::FormType::Spell) {
        return false;
    }
    const auto* equp = spell.As<RE::BGSEquipType>();
    const auto* slot = equp ? equp->GetEquipSlot() : nullptr;
    auto slot_id = slot ? slot->GetFormID() : 0;
    if ( // clang-format off
        slot_id == tes_util::kEqupRightHand
        || slot_id == tes_util::kEqupLeftHand
        || slot_id == tes_util::kEqupEitherHand
        || (slot_id == tes_util::kEqupBothHands && allow_2h)
    ) {  // clang-format on
        return true;
    }
    return false;
}

inline RE::SpellItem*
GetRightHandSpellItem(const RE::Actor& actor) {
    auto* form = actor.GetEquippedObject(/*a_leftHand=*/false);
    return form ? form->As<RE::SpellItem>() : nullptr;
}

inline RE::TESShout*
GetEquippedShout(const RE::Actor& actor) {
    auto* form = actor.GetActorRuntimeData().selectedPower;
    return form ? form->As<RE::TESShout>() : nullptr;
}

inline RE::HighProcessData*
GetHighProcessData(RE::Actor& player) {
    auto* process = player.GetActorRuntimeData().currentProcess;
    return process ? process->high : nullptr;
}

inline bool
CheckCast(
    RE::MagicCaster& caster,
    RE::SpellItem& spell,
    std::span<const RE::MagicSystem::CannotCastReason> ignored_reasons = {}
) {
    auto reason = RE::MagicSystem::CannotCastReason::kOK;
    if (caster.CheckCast(
            &spell,
            /*a_dualCast=*/false,
            /*a_alchStrength=*/nullptr,
            &reason,
            /*a_useBaseValueForCost=*/false
        )) {
        return true;
    }
    for (auto ignore : ignored_reasons) {
        if (reason == ignore) {
            return true;
        }
    }
    SKSE::log::trace("cannot cast reason: {}", std::to_underlying(reason));
    return false;
}

inline bool
HasEnoughMagicka(
    RE::Actor& actor, RE::ActorValueOwner& av_owner, const RE::SpellItem& spell, float magicka_scale
) {
    auto magicka = av_owner.GetActorValue(RE::ActorValue::kMagicka);
    return spell.CalculateMagickaCost(&actor) * magicka_scale <= magicka;
}

void
ApplyMagickaCost(
    RE::Actor& actor, RE::ActorValueOwner& av_owner, const RE::SpellItem& spell, float magicka_scale
) {
    av_owner.RestoreActorValue(
        RE::ACTOR_VALUE_MODIFIER::kDamage,
        RE::ActorValue::kMagicka,
        -spell.CalculateMagickaCost(&actor) * magicka_scale
    );
}

void
CastSpellImmediate(RE::Actor* actor, RE::MagicCaster* magic_caster, RE::SpellItem* spell) {
    if (!actor || !magic_caster || !spell) {
        return;
    }
    magic_caster->CastSpellImmediate(
        spell,
        /*a_noHitEffectArt=*/false,
        /*a_target=*/nullptr,
        /*a_effectiveness=*/1.f,
        /*a_hostileEffectivenessOnly=*/false,
        /*a_magnitudeOverride=*/0.f,
        /*a_blameActor=*/actor
    );
}

inline RE::BGSSoundDescriptorForm*
GetSpellSound(const RE::SpellItem* spell, RE::MagicSystem::SoundID id) {
    const auto* effect_setting = spell ? spell->GetAVEffect() : nullptr;
    if (!effect_setting) {
        return nullptr;
    }
    for (const auto& soundpair : effect_setting->effectSounds) {
        if (soundpair.id == id && soundpair.sound) {
            return soundpair.sound;
        }
    }
    return nullptr;
}

/// Returns nullopt if `sound` is null.
inline std::optional<RE::BSSoundHandle>
ActorPlaySound(RE::Actor& actor, const RE::BGSSoundDescriptorForm* sound) {
    if (!sound) {
        return std::nullopt;
    }
    auto sound_handle = RE::BSSoundHandle();
    actor.PlayASound(sound_handle, sound->GetFormID(), /*a_unk03=*/false, /*a_flags=*/0);
    return sound_handle;
}

inline void
ActorPlayMagicFailureSound(RE::Actor& actor) {
    ActorPlaySound(
        actor, RE::MagicSystem::GetMagicFailureSound(RE::MagicSystem::SpellType::kLesserPower)
    );
}

inline void
FlashMagickaBar() {
    // We can't simply add flash-HUD events to the UI message queue because TrueHUD won't react to
    // those.
    auto FlashHudMenuMeter = REL::Relocation<void(RE::ActorValue)>(REL::RelocationID(51907, 52845));
    FlashHudMenuMeter(RE::ActorValue::kMagicka);
}

inline void
UnequipHand(RE::ActorEquipManager& aem, RE::Actor& actor, bool left_hand) {
    auto equp_id = left_hand ? tes_util::kEqupLeftHand : tes_util::kEqupRightHand;
    const auto* bgs_slot = tes_util::GetForm<RE::BGSEquipSlot>(equp_id);
    auto* dummy = tes_util::GetForm<RE::TESObjectWEAP>(tes_util::kWeapDummy);
    if (!bgs_slot || !dummy) {
        SKSE::log::error(
            "unequip hand failed: cannot look up {:08X} or {:08X}", equp_id, tes_util::kWeapDummy
        );
        return;
    }
    //                                                 queue, force, sounds, apply_now
    aem.EquipObject(&actor, dummy, nullptr, 1, bgs_slot, false, false, false, true);
    aem.UnequipObject(&actor, dummy, nullptr, 1, bgs_slot, false, false, false, true);
}

}  // namespace tes_util
}  // namespace esas
