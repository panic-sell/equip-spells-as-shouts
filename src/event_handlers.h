#pragma once

#include "keys.h"
#include "settings.h"
#include "shoutmap.h"
#include "tes_util.h"

namespace esas {
namespace internal {

/// Among the given input events, find a button event that matches `user_event`. `user_event` should
/// be a member of `RE::UserEvents`.
inline const RE::ButtonEvent*
GetUserEventButtonInput(std::string_view user_event, const RE::InputEvent* events) {
    const auto* cm = RE::ControlMap::GetSingleton();
    if (!cm) {
        return nullptr;
    }

    for (; events; events = events->next) {
        const auto* button = events->AsButtonEvent();
        if (!button || !button->HasIDCode()) {
            continue;
        }
        if (cm->GetMappedKey(user_event, button->GetDevice()) != button->GetIDCode()) {
            continue;
        }
        return button;
    }

    return nullptr;
}

inline const RE::ButtonEvent*
GetShoutButtonInput(const RE::InputEvent* events) {
    const auto* user_events = RE::UserEvents::GetSingleton();
    if (!user_events) {
        return nullptr;
    }
    return GetUserEventButtonInput(user_events->shout, events);
}

}  // namespace internal

class FafHandler final : public RE::BSTEventSink<SKSE::ActionEvent> {
  public:
    [[nodiscard]] static bool
    Init(std::mutex& mutex, Shoutmap& map, const Settings& settings) {
        auto* action_ev_src = SKSE::GetActionEventSource();
        if (!action_ev_src) {
            return false;
        }

        static auto instance = FafHandler(mutex, map, settings);
        action_ev_src->AddEventSink(&instance);
        return true;
    }

    RE::BSEventNotifyControl
    ProcessEvent(const SKSE::ActionEvent* event, RE::BSTEventSource<SKSE::ActionEvent>*) override {
        Prep(event);
        Cast(event);
        return RE::BSEventNotifyControl::kContinue;
    }

  private:
    FafHandler(std::mutex& mutex, Shoutmap& map, const Settings& settings)
        : mutex_(mutex),
          map_(map),
          magicka_scale_(settings.magicka_scale_faf) {}

    FafHandler(const FafHandler&) = delete;
    FafHandler& operator=(const FafHandler&) = delete;
    FafHandler(FafHandler&&) = delete;
    FafHandler& operator=(FafHandler&&) = delete;

    void
    Prep(const SKSE::ActionEvent* event) {
        auto* player = event ? event->actor : nullptr;
        if (!player || !player->IsPlayerRef()) {
            return;
        }
        if (event->type != SKSE::ActionEvent::Type::kVoiceCast) {
            return;
        }
        shouting_ = true;
    }

    void
    Cast(const SKSE::ActionEvent* event) {
        auto* player = event ? event->actor : nullptr;
        if (!player || !player->IsPlayerRef()) {
            return;
        }
        if (event->type != SKSE::ActionEvent::Type::kVoiceFire) {
            return;
        }
        if (!shouting_) {
            return;
        }
        shouting_ = false;

        auto* high_data = tes_util::GetHighProcessData(*player);
        auto* av_owner = player->AsActorValueOwner();
        if (!high_data || !av_owner) {
            return;
        }

        auto* shout = event->sourceForm ? event->sourceForm->As<RE::TESShout>() : nullptr;
        if (!shout) {
            return;
        }
        RE::SpellItem* spell = nullptr;
        {
            auto lock = std::lock_guard(mutex_);
            spell = map_[*shout];
        }
        if (!spell) {
            SKSE::log::trace("faf: {} is not a spell shout or is unassigned", *shout);
            return;
        }
        if (spell->GetCastingType() != RE::MagicSystem::CastingType::kFireAndForget) {
            return;
        }
        if (!RE::PlayerCharacter::IsGodMode()
            && !tes_util::HasEnoughMagicka(*player, *av_owner, *spell, magicka_scale_)) {
            SKSE::log::trace("faf: {} -> {} not enough magicka", *shout, *spell);
            tes_util::ActorPlayMagicFailureSound(*player);
            tes_util::FlashMagickaBar();
            return;
        }

        // Bound weapon must be cast from hands.
        auto is_bound_spell = spell->GetAVEffect()
                              && spell->GetAVEffect()->GetArchetype()
                                     == RE::EffectArchetypes::ArchetypeID::kBoundWeapon;
        auto casting_src = RE::MagicSystem::CastingSource::kInstant;
        if (is_bound_spell) {
            if (high_data->currentShoutVariation == RE::TESShout::VariationID::kOne) {
                casting_src = RE::MagicSystem::CastingSource::kRightHand;
            } else {
                casting_src = RE::MagicSystem::CastingSource::kLeftHand;
            }
        }
        auto* magic_caster = player->GetMagicCaster(casting_src);
        if (!magic_caster) {
            SKSE::log::trace("can't get player RE::MagicCaster");
            return;
        }

        if (is_bound_spell) {
            if (auto* aem = RE::ActorEquipManager::GetSingleton()) {
                tes_util::UnequipHand(
                    *aem, *player, casting_src == RE::MagicSystem::CastingSource::kLeftHand
                );
            }
        }
        tes_util::ApplyMagickaCost(*player, *av_owner, *spell, magicka_scale_);
        tes_util::ActorPlaySound(
            *player, tes_util::GetSpellSound(spell, RE::MagicSystem::SoundID::kRelease)
        );
        tes_util::CastSpellImmediate(*player, *magic_caster, *spell);
        SKSE::log::debug("faf: casting {} -> {}", *shout, *spell);
    }

    bool shouting_ = false;
    std::mutex& mutex_;
    Shoutmap& map_;
    const float magicka_scale_;
};

class ConcHandler final : public RE::BSTEventSink<SKSE::ActionEvent>,
                          public RE::BSTEventSink<RE::InputEvent*> {
  public:
    [[nodiscard]] static bool
    Init(std::mutex& mutex, Shoutmap& map, const Settings& settings) {
        auto* action_ev_src = SKSE::GetActionEventSource();
        auto* input_ev_src = RE::BSInputDeviceManager::GetSingleton();
        if (!action_ev_src || !input_ev_src) {
            return false;
        }

        static auto instance = ConcHandler(mutex, map, settings);
        action_ev_src->AddEventSink(&instance);
        input_ev_src->AddEventSink(&instance);
        return true;
    }

    RE::BSEventNotifyControl
    ProcessEvent(const SKSE::ActionEvent* event, RE::BSTEventSource<SKSE::ActionEvent>*) override {
        Cast(event);
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl
    ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override {
        Poll(events);
        return RE::BSEventNotifyControl::kContinue;
    }

  private:
    ConcHandler(std::mutex& mutex, Shoutmap& map, const Settings& settings)
        : mutex_(mutex),
          map_(map),
          magicka_scale_(settings.magicka_scale_conc) {}

    ConcHandler(const ConcHandler&) = delete;
    ConcHandler& operator=(const ConcHandler&) = delete;
    ConcHandler(ConcHandler&&) = delete;
    ConcHandler& operator=(ConcHandler&&) = delete;

    void
    Cast(const SKSE::ActionEvent* event) {
        if (current_spell_) {
            return;
        }

        if (!event || event->type != SKSE::ActionEvent::Type::kVoiceFire) {
            return;
        }
        auto* player = event->actor;
        if (!player || !player->IsPlayerRef()) {
            return;
        }
        auto* av_owner = player->AsActorValueOwner();
        if (!av_owner) {
            return;
        }

        auto* shout = event->sourceForm ? event->sourceForm->As<RE::TESShout>() : nullptr;
        if (!shout) {
            return;
        }
        RE::SpellItem* spell = nullptr;
        {
            auto lock = std::lock_guard(mutex_);
            spell = map_[*shout];
        }
        if (!spell) {
            SKSE::log::trace("conc: {} is not a spell shout or is unassigned", *shout);
            return;
        }
        if (spell->GetCastingType() != RE::MagicSystem::CastingType::kConcentration) {
            return;
        }
        Clear(nullptr, nullptr);
        if (!RE::PlayerCharacter::IsGodMode() && spell->CalculateMagickaCost(player) > 0.f
            && av_owner->GetActorValue(RE::ActorValue::kMagicka) <= 0.f) {
            SKSE::log::trace("conc: {} -> {} not enough magicka", *shout, *spell);
            tes_util::ActorPlayMagicFailureSound(*player);
            tes_util::FlashMagickaBar();
            // Setting current_spell_ is required in order to have Poll() reset shout cooldown.
            // Resetting cooldown in this function (in the same frame?) doesn't work.
            current_spell_ = spell;
            return;
        }

        auto* magic_caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
        if (!magic_caster) {
            SKSE::log::trace("can't get player RE::MagicCaster");
            return;
        }

        loop_soundhandle_ = tes_util::ActorPlaySound(
            *player, tes_util::GetSpellSound(spell, RE::MagicSystem::SoundID::kCastLoop)
        );
        tes_util::ActorPlaySound(
            *player, tes_util::GetSpellSound(spell, RE::MagicSystem::SoundID::kRelease)
        );
        magic_caster->currentSpellCost = spell->CalculateMagickaCost(player) * magicka_scale_;
        tes_util::CastSpellImmediate(*player, *magic_caster, *spell);
        current_spell_ = spell;
        SKSE::log::debug("conc: casting {} -> {}", *shout, *spell);
    }

    void
    Poll(RE::InputEvent* const* events) {
        if (!current_spell_) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* magic_caster = player
                                 ? player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)
                                 : nullptr;
        if (!magic_caster || magic_caster->state != RE::MagicCaster::State::kCasting) {
            Clear(player, magic_caster);
            return;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused()) {
            return;
        }
        const auto* control_map = RE::ControlMap::GetSingleton();
        if (!control_map || !control_map->IsFightingControlsEnabled()) {
            return;
        }
        const auto& cmstack = control_map->GetRuntimeData().contextPriorityStack;
        if (cmstack.empty() || cmstack.back() != RE::UserEvents::INPUT_CONTEXT_ID::kGameplay) {
            return;
        }

        if (!events) {
            Clear(player, magic_caster);
            return;
        }
        const auto* button = internal::GetShoutButtonInput(*events);
        if (!button || button->IsUp()) {
            Clear(player, magic_caster);
            return;
        }
    }

    /// Sets `current_spell_` to null. Stops and resets `loop_soundhandle_` (no-op if sound handle
    /// is already cleared).
    ///
    /// If `player` is non-null, resets player's shout cooldown. If `caster` is non-null, forces
    /// caster to finish the current cast (no-op if caster isn't casting).
    void
    Clear(RE::Actor* player, RE::MagicCaster* caster) {
        if (player) {
            if (auto* high_data = tes_util::GetHighProcessData(*player)) {
                high_data->voiceRecoveryTime = 0.f;
            }
        }
        if (caster) {
            caster->FinishCast();
        }
        current_spell_ = nullptr;
        if (loop_soundhandle_) {
            loop_soundhandle_->Stop();
            loop_soundhandle_.reset();
        }
    }

    RE::SpellItem* current_spell_ = nullptr;
    std::optional<RE::BSSoundHandle> loop_soundhandle_;
    std::mutex& mutex_;
    Shoutmap& map_;
    const float magicka_scale_;
};

class AssignmentHandler final : public RE::BSTEventSink<RE::InputEvent*> {
  public:
    [[nodiscard]] static bool
    Init(std::mutex& mutex, Shoutmap& map, const Settings& settings) {
        auto* input_ev_src = RE::BSInputDeviceManager::GetSingleton();
        if (!input_ev_src) {
            return false;
        }

        static auto instance = AssignmentHandler(mutex, map, settings);
        input_ev_src->AddEventSink(&instance);
        return true;
    }

    RE::BSEventNotifyControl
    ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override {
        HandleInput(events);
        return RE::BSEventNotifyControl::kContinue;
    }

  private:
    AssignmentHandler(std::mutex& mutex, Shoutmap& map, const Settings& settings)
        : mutex_(mutex),
          map_(map),
          allow_2h_(settings.allow_2h_spells),
          assign_keysets_(settings.convert_spell_keysets),
          unassign_keysets_(settings.remove_shout_keysets) {}

    AssignmentHandler(const AssignmentHandler&) = delete;
    AssignmentHandler& operator=(const AssignmentHandler&) = delete;
    AssignmentHandler(AssignmentHandler&&) = delete;
    AssignmentHandler& operator=(AssignmentHandler&&) = delete;

    void
    HandleInput(RE::InputEvent* const* events) {
        if (!events) {
            return;
        }
        buf_.clear();
        Keystroke::InputEventsToBuffer(*events, buf_);
        if (buf_.empty()) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        if (assign_keysets_.Match(buf_) == Keypress::kPress) {
            Assign(*player);
        }
        if (unassign_keysets_.Match(buf_) == Keypress::kPress) {
            Unassign(*player);
        }
    }

    void
    Assign(RE::Actor& player) {
        auto* spell = tes_util::GetRightHandSpellItem(player);
        if (!spell) {
            return;
        }
        if (!tes_util::IsHandEquippedSpell(*spell, allow_2h_)) {
            SKSE::log::trace("{} is not eligible for spell shout assignment", *spell);
            return;
        }
        auto ct = spell->GetCastingType();
        if (ct != RE::MagicSystem::CastingType::kFireAndForget
            && ct != RE::MagicSystem::CastingType::kConcentration) {
            return;
        }

        SKSE::log::debug("assigning {} ...", *spell);
        RE::TESShout* shout = nullptr;
        switch (auto status = map_.Assign(player, *spell, shout)) {
            case Shoutmap::AssignStatus::kOk:
                tes_util::DebugNotification("{} added", shout->GetName());
                break;
            case Shoutmap::AssignStatus::kAlreadyAssigned:
                tes_util::DebugNotification("{} already assigned", spell->GetName());
                break;
            case Shoutmap::AssignStatus::kOutOfSlots:
                tes_util::DebugNotification("No remaining shout slots");
                break;
            case Shoutmap::AssignStatus::kUnknownShout:
            case Shoutmap::AssignStatus::kInternalError:
                SKSE::log::error(
                    "unexpected error assigning {} to {}: status code {}",
                    *spell,
                    *shout,
                    std::to_underlying(status)
                );
                break;
        }
    }

    void
    Unassign(RE::Actor& player) {
        auto* shout = tes_util::GetEquippedShout(player);
        if (!shout) {
            return;
        }
        auto lock = std::lock_guard(mutex_);
        if (!map_.Has(*shout)) {
            return;
        }

        SKSE::log::debug("unassigning {} ...", *shout);
        switch (auto status = map_.Unassign(player, *shout)) {
            case Shoutmap::AssignStatus::kOk:
                tes_util::DebugNotification("{} removed", shout->GetName());
                break;
            default:
                SKSE::log::error(
                    "unexpected error unassigning {}: status code {}",
                    *shout,
                    std::to_underlying(status)
                );
                break;
        }
    }

    std::vector<Keystroke> buf_;
    std::mutex& mutex_;
    Shoutmap& map_;
    const bool allow_2h_;
    const Keysets assign_keysets_;
    const Keysets unassign_keysets_;
};

}  // namespace esas
