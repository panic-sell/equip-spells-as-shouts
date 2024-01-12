#pragma once

#include "keys.h"
#include "settings.h"

namespace esas {
namespace internal {

/// Tries to get a field value from a serialized object. Returns nullopt if the field does not exist
/// or cannot be converted to `T`.
template <typename T, typename C>
inline std::optional<T>
GetSerObjField(const boost::json::object& jo, std::string_view name, const C& ctx) {
    const auto* jv = jo.if_contains(name);
    if (!jv) {
        return std::nullopt;
    }
    auto result = boost::json::try_value_to<T>(*jv, ctx);
    return result ? std::optional(std::move(*result)) : std::nullopt;
}

}  // namespace internal

/// Context for implementing Boost.JSON tag_invoke overloads, specifically for types in this
/// project.
///
/// Currently, this is only used to ensure that JSON conversions do NOT treat `Keyset` as a typical
/// `std::array<uint32_t, 4>`.
///
/// This class must be defined in the same namespace as classes and tag_invoke overloads.
struct SerdeContext final {};

/// Serializes object to compact JSON string.
///
/// Unlike `Deserialize()`, this function does not have a variant serializing `T` to
/// `boost::json::value` because `boost::json::value_from()` already serves that role.
template <typename T, typename C = SerdeContext>
inline std::string
Serialize(const T& t, const C& ctx = {}) {
    return boost::json::serialize(boost::json::value_from(t, ctx));
}

/// Deserializes `boost::json::value` from JSON string. Input is allowed to contain comment and
/// trailing commas.
inline std::optional<boost::json::value>
Deserialize(std::string_view s) {
    constexpr auto opts = boost::json::parse_options{
        .allow_comments = true,
        .allow_trailing_commas = true,
    };

    std::error_code ec;
    auto jv = boost::json::parse(s, ec, {}, opts);
    return !ec ? std::optional(std::move(jv)) : std::nullopt;
}

/// Deserializes object from JSON string. Input is allowed to contain comment and trailing commas.
template <typename T, typename C = SerdeContext>
inline std::optional<T>
Deserialize(std::string_view s, const C& ctx = {}) {
    return Deserialize(s).and_then([&ctx](boost::json::value&& jv) {
        auto res = boost::json::try_value_to<T>(jv, ctx);
        return res ? std::optional(std::move(*res)) : std::nullopt;
    });
}

inline void
tag_invoke(const boost::json::value_from_tag&, boost::json::value& jv, const Keyset& keyset, const SerdeContext&) {
    auto ja = boost::json::array();
    for (auto keycode : keyset) {
        if (KeycodeIsValid(keycode)) {
            auto s = boost::json::string_view(KeycodeName(keycode));
            ja.push_back(s);
        }
    }
    jv = std::move(ja);
}

inline boost::json::result<Keyset>
tag_invoke(
    const boost::json::try_value_to_tag<Keyset>&,
    const boost::json::value& jv,
    const SerdeContext& ctx
) {
    auto keyset = KeysetNormalized({});
    auto v = boost::json::try_value_to<std::vector<std::string>>(jv, ctx);
    if (!v) {
        return keyset;
    }

    auto sz = std::min(v->size(), keyset.size());
    for (size_t i = 0; i < sz; i++) {
        std::string_view name = (*v)[i];
        keyset[i] = KeycodeFromName(name);
    }
    return KeysetNormalized(keyset);
}

/// Note that there's no `value_from` tag_invoke. Settings are only every configured through JSON
/// files, so there's no need to serialize settings to JSON.
inline boost::json::result<Settings>
tag_invoke(
    const boost::json::try_value_to_tag<Settings>&,
    const boost::json::value& jv,
    const SerdeContext& ctx
) {
    auto settings = Settings();
    if (!jv.is_object()) {
        return settings;
    }
    auto jo = jv.get_object();

    if (auto field = internal::GetSerObjField<std::string>(jo, "log_level", ctx)) {
        settings.log_level = std::move(*field);
    }
    if (auto field = internal::GetSerObjField<std::vector<Keyset>>(
            jo, "convert_spell_keysets", ctx
        )) {
        settings.convert_spell_keysets = Keysets(std::move(*field));
    }
    if (auto field = internal::GetSerObjField<std::vector<Keyset>>(
            jo, "remove_shout_keysets", ctx
        )) {
        settings.remove_shout_keysets = Keysets(std::move(*field));
    }
    if (auto field = internal::GetSerObjField<bool>(jo, "allow_2h_spells", ctx)) {
        settings.allow_2h_spells = *field;
    }
    if (auto field = internal::GetSerObjField<float>(jo, "magicka_scale_faf", ctx)) {
        settings.magicka_scale_faf = *field;
    }
    if (auto field = internal::GetSerObjField<float>(jo, "magicka_scale_conc", ctx)) {
        settings.magicka_scale_conc = *field;
    }

    return settings;
}

}  // namespace esas
