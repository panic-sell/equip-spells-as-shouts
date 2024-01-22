#pragma once

#include "serde.h"
#include "tes_util.h"

namespace esas {

inline constexpr std::string_view kModname = ESAS_NAME ".esp";

namespace internal {

inline RE::TESWordOfPower*
Word() {
    return tes_util::GetForm<RE::TESWordOfPower>(kModname, 0x801);
}

inline RE::TESShout*
DefaultShout() {
    return tes_util::GetForm<RE::TESShout>(kModname, 0x8ff);
}

template <RE::FormID First, RE::FormID Last>
requires(First <= Last)
inline constexpr std::array<RE::FormID, Last - First + 1>
MakeLocalIDs() {
    auto arr = std::array<RE::FormID, Last - First + 1>();
    for (RE::FormID i = 0; i < arr.size(); i++) {
        arr[i] = First + i;
    }
    return arr;
}

inline std::vector<RE::TESShout*>
GetShouts(std::span<const RE::FormID> local_ids) {
    auto v = std::vector<RE::TESShout*>();
    for (auto id : local_ids) {
        auto* shout = tes_util::GetForm<RE::TESShout>(kModname, id);
        if (shout) {
            v.push_back(shout);
        }
    }
    return v;
}

}  // namespace internal

inline std::vector<RE::TESShout*>
FafShouts() {
    return internal::GetShouts(internal::MakeLocalIDs<0x900, 0x90f>());
}

inline std::vector<RE::TESShout*>
ConcShouts() {
    return internal::GetShouts(internal::MakeLocalIDs<0x980, 0x98f>());
}

/// Shouts and their spell assignments.
///
/// Invariants:
/// - `size() == shouts_.size() == spells_.size()`
/// - Every element of `shouts_` is non-null.
class Shoutmap final {
  public:
    Shoutmap() = default;

    explicit Shoutmap(std::vector<RE::TESShout*> shouts)
        : shouts_(std::move(shouts)),
          spells_(shouts_.size(), nullptr) {}

    size_t
    size() const {
        return shouts_.size();
    }

    const std::vector<RE::TESShout*>&
    shouts() const {
        return shouts_;
    }

    const std::vector<RE::SpellItem*>&
    spells() const {
        return spells_;
    }

    bool
    Has(const RE::TESShout& shout) const {
        return IndexOf(shout) < size();
    }

    bool
    Has(const RE::SpellItem& spell) const {
        return IndexOf(spell) < size();
    }

    RE::SpellItem*
    operator[](const RE::TESShout& shout) const {
        auto i = IndexOf(shout);
        return i < size() ? spells_[i] : nullptr;
    }

    RE::TESShout*
    operator[](const RE::SpellItem& spell) const {
        auto i = IndexOf(spell);
        return i < size() ? shouts_[i] : nullptr;
    }

    enum class AssignStatus {
        kOk,
        kAlreadyAssigned,
        kOutOfSlots,
        kUnknownShout,
        kInternalError,
    };

    /// Will never return `kUnknownShout`. `assigned_shout` will only be written if returned status
    /// is `kOk`.
    AssignStatus
    Assign(RE::Actor& player, RE::SpellItem& spell, RE::TESShout*& assigned_shout) {
        auto* shout = (*this)[spell];
        if (shout && player.HasShout(shout)) {
            return AssignStatus::kAlreadyAssigned;
        }
        if (!shout) {
            shout = NextUnassigned(player);
            if (!shout) {
                return AssignStatus::kOutOfSlots;
            }
        }

        auto* word = internal::Word();
        auto* default_shout = internal::DefaultShout();
        if (!word || !default_shout) {
            return AssignStatus::kInternalError;
        }

        if (
            // There's no harm adding the word of power during every assignment. It'll just be a
            // no-op most of the time.
            !tes_util::ConsoleRun("player.teachword {:08x}", word->GetFormID())
            // Teaching a word auto-adds the corresponding shout. If multiple shouts share the same
            // word, the shout with the lowest form ID is chosen. Since all spell shouts share the
            // same word, the lowest one will get auto-added. To prevent auto-adding real spell
            // shouts, the ESP defines a default shout with a lower form ID than all real shouts.
            // Hence, it is necessary to clean up the default shout as well after it gets
            // auto-added.
            || !tes_util::ConsoleRun("player.removeshout {:08x}", default_shout->GetFormID())
        ) {
            return AssignStatus::kInternalError;
        }
        // No way to check if a player knows a particular word, so we have to blindly assume the
        // above console commands worked.

        player.UnlockWord(word);
        player.AddShout(shout);
        auto res = Assign(*shout, spell);
        if (res == AssignStatus::kOk) {
            assigned_shout = shout;
        }
        return res;
    }

    AssignStatus
    Assign(RE::TESShout& shout, RE::SpellItem& spell) {
        auto i = IndexOf(shout);
        if (i >= size()) {
            return AssignStatus::kUnknownShout;
        }

        spells_[i] = &spell;
        shout.SetFullName(std::format("{} (Spell Shout)", spell.GetName()).c_str());
        auto shout_disp = shout.As<RE::BGSMenuDisplayObject>();
        auto spell_disp = spell.As<RE::BGSMenuDisplayObject>();
        if (shout_disp && spell_disp) {
            shout_disp->CopyComponent(spell_disp);
        }

        auto recovery = 0.f;
        if (const auto* av_eff = spell.GetAVEffect();
            av_eff && av_eff->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kBoundWeapon) {
            // When casting a level 3 shout, prevent accidentally holding the shout button for too
            // long and casting a subsequent level 1 shout.
            recovery = 2.f;
        } else if (spell.GetCastingType() == RE::MagicSystem::CastingType::kConcentration) {
            // Prevent the shout animation from looping.
            recovery = 5.f;
        }
        for (auto& var : shout.variations) {
            var.recoveryTime = recovery;
        }

        return AssignStatus::kOk;
    }

    /// Will never return `kAlreadyAssigned` or `kOutOfSlots`. Will not reset `shout`'s form data.
    AssignStatus
    Unassign(RE::Actor& player, RE::TESShout& shout) {
        auto i = IndexOf(shout);
        if (i >= size()) {
            return AssignStatus::kUnknownShout;
        }
        (void)player;
        if (!tes_util::ConsoleRun("player.removeshout {:08x}", shout.GetFormID())) {
            return AssignStatus::kInternalError;
        }
        spells_[i] = nullptr;
        return AssignStatus::kOk;
    }

  private:
    size_t
    IndexOf(const RE::TESShout& shout) const {
        return std::find(shouts_.cbegin(), shouts_.cend(), &shout) - shouts_.cbegin();
    }

    size_t
    IndexOf(const RE::SpellItem& spell) const {
        return std::find(spells_.cbegin(), spells_.cend(), &spell) - spells_.cbegin();
    }

    /// Shouts the player doesn't have are considered to be unassigned.
    RE::TESShout*
    NextUnassigned(RE::Actor& player) const {
        // Prioritize shouts that the player doesn't have but are somehow mapped to a spell.
        for (size_t i = 0; i < size(); i++) {
            auto* shout = shouts_[i];
            auto* spell = spells_[i];
            if (spell && !player.HasShout(shout)) {
                SKSE::log::trace("{} can be assigned to", *shout);
                return shout;
            }
        }
        for (size_t i = 0; i < size(); i++) {
            auto* shout = shouts_[i];
            auto* spell = spells_[i];
            if (!spell) {
                SKSE::log::trace("{} can be assigned to", *shout);
                return shout;
            }
        }
        SKSE::log::trace("no remaining unassigned shouts");
        return nullptr;
    }

    std::vector<RE::TESShout*> shouts_;
    std::vector<RE::SpellItem*> spells_;
};

/// Maps spell shout local IDs to spell absolute IDs.
using ShoutmapIR = std::vector<std::pair<RE::FormID, RE::FormID>>;

/// Returns all assignments for which the shout is in `player`'s inventory.
inline ShoutmapIR
ShoutmapToIR(const Shoutmap& map, const RE::Actor& player) {
    auto ir = ShoutmapIR();

    for (size_t i = 0; i < map.size(); i++) {
        auto* shout = map.shouts()[i];
        auto* spell = map.spells()[i];
        if (!spell) {
            continue;
        }
        if (!player.HasShout(shout)) {
            SKSE::log::trace(
                "discarding {}: assigned to {} but not in player inventory", *shout, *spell
            );
            continue;
        }
        ir.emplace_back(shout->GetLocalFormID(), spell->GetFormID());
    }

    return ir;
}

/// Writes all valid assignment from `ir` into `map`, filtering only for assignments where the shout
/// is in `player`'s inventory.
inline size_t
ShoutmapFillFromIR(Shoutmap& map, const ShoutmapIR& ir, const RE::Actor& player) {
    size_t assignments = 0;

    for (const auto& [shout_local_id, spell_id] : ir) {
        auto* shout = tes_util::GetForm<RE::TESShout>(kModname, shout_local_id);
        if (!shout) {
            continue;
        }
        if (!map.Has(*shout)) {
            SKSE::log::trace("{} was stored in shoutmap but is not a spell shout", *shout);
            continue;
        }
        auto* spell = tes_util::GetForm<RE::SpellItem>(spell_id);
        if (!spell) {
            continue;
        }
        if (!player.HasShout(shout)) {
            SKSE::log::trace(
                "discarding {}: assigned to {} but not in player inventory", *shout, *spell
            );
            continue;
        }

        switch (auto status = map.Assign(*shout, *spell)) {
            case Shoutmap::AssignStatus::kOk:
                assignments++;
                break;
            default:
                SKSE::log::error(
                    "unexpected error assigning {} to {}: status code {}",
                    *spell,
                    *shout,
                    std::to_underlying(status)
                );
                break;
        }
    }

    return assignments;
}

}  // namespace esas
