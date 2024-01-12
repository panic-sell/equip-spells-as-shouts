// SKSE plugin entry point.
#include "event_handlers.h"
#include "fs.h"
#include "serde.h"
#include "settings.h"
#include "shouts.h"

namespace {

using namespace esas;

auto gSettings = Settings();
auto gMutex = std::mutex();
auto gFafMap = Shoutmap();
auto gConcMap = Shoutmap();

void
InitSettings() {
    auto settings = fs::ReadFile(fs::kSettingsPath).and_then([](std::string&& s) {
        return Deserialize<Settings>(s);
    });
    if (!settings) {
        SKSE::log::warn("'{}' cannot be parsed, using default settings", fs::kSettingsPath);
        return;
    }
    gSettings = std::move(*settings);
}

void
InitLogging(const SKSE::PluginDeclaration& plugin_decl) {
    auto log_dir = SKSE::log::log_directory();
    if (!log_dir) {
        SKSE::stl::report_and_fail("cannot get SKSE logs directory");
    }
    log_dir->append(plugin_decl.GetName()).replace_extension("log");

    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_dir->string(), true);
    auto logger = std::make_shared<spdlog::logger>("logger", std::move(sink));

    auto level = spdlog::level::from_str(gSettings.log_level);
    if (level == spdlog::level::off && gSettings.log_level != "off") {
        level = spdlog::level::info;
    }
    logger->flush_on(level);
    logger->set_level(level);
    // https://github.com/gabime/spdlog/wiki/3.-Custom-formatting#pattern-flags
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");

    spdlog::set_default_logger(std::move(logger));
}

void
InitSKSEMessaging(const SKSE::MessagingInterface& mi) {
    constexpr auto listener = [](SKSE::MessagingInterface::Message* msg) -> void {
        if (!msg || msg->type != SKSE::MessagingInterface::kDataLoaded) {
            return;
        }
        if (!FafHandler::Init(gMutex, gFafMap, gSettings.magicka_scale_faf)
            || !ConcHandler::Init(gMutex, gConcMap, gSettings.magicka_scale_conc)
            || !AssignmentHandler::Init(
                gMutex,
                gFafMap,
                gConcMap,
                gSettings.allow_2h_spells,
                gSettings.convert_spell_keysets,
                gSettings.remove_shout_keysets
            )) {
            SKSE::stl::report_and_fail("cannot initialize fire-and-forget handler");
        }
    };

    if (!mi.RegisterListener(listener)) {
        SKSE::stl::report_and_fail("cannot register SKSE message listener");
    }
}

void
InitSKSESerialization(const SKSE::SerializationInterface& si) {
    constexpr uint32_t kFafType = 'FAF';
    constexpr uint32_t kConcType = 'CONC';
    constexpr uint32_t kVersion = 1;

    static constexpr auto on_save = [](SKSE::SerializationInterface* si) -> void {
        if (!si) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("failed to get RE::PlayerCharacter during SKSE cosave on_save");
            return;
        }

        auto lock = std::lock_guard(gMutex);
        auto faf_ir = ShoutmapToIR(gFafMap, *player);
        auto conc_ir = ShoutmapToIR(gConcMap, *player);

        if (!faf_ir.empty()) {
            auto s = Serialize(faf_ir);
            if (si->WriteRecord(kFafType, kVersion, s.c_str(), static_cast<uint32_t>(s.size()))) {
                SKSE::log::debug("fire-and-forget spell shout assignments serialized to SKSE cosave"
                );
            } else {
                SKSE::log::error(
                    "cannot serialize fire-and-forget spell shout assignments to SKSE cosave"
                );
            }
        }
        if (!conc_ir.empty()) {
            auto s = Serialize(conc_ir);
            if (si->WriteRecord(kConcType, kVersion, s.c_str(), static_cast<uint32_t>(s.size()))) {
                SKSE::log::debug("concentration spell shout assignments serialized to SKSE cosave");
            } else {
                SKSE::log::error(
                    "cannot serialize concentration spell shout assignments to SKSE cosave"
                );
            }
        }
    };

    static constexpr auto on_load = [](SKSE::SerializationInterface* si) -> void {
        if (!si) {
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("failed to get RE::PlayerCharacter during SKSE cosave on_load");
            return;
        }

        auto lock = std::lock_guard(gMutex);
        gFafMap = Shoutmap(FafShouts());
        gConcMap = Shoutmap(ConcShouts());
        uint32_t type;
        uint32_t version;  // unused
        uint32_t length;
        while (si->GetNextRecordInfo(type, version, length)) {
            auto* map = type == kFafType ? &gFafMap : type == kConcType ? &gConcMap : nullptr;
            if (!map) {
                SKSE::log::warn("unknown record type '{}' in SKSE cosave", type);
                continue;
            }

            std::string s;
            s.reserve(length);
            for (uint32_t i = 0; i < length; i++) {
                char c;
                si->ReadRecordData(&c, 1);
                s.push_back(c);
            }

            auto ir = Deserialize<ShoutmapIR>(s);
            if (!ir) {
                SKSE::log::error("cannot deserialize spell shout assignments from SKSE cosave");
                continue;
            }
            for (auto& pair : *ir) {
                auto new_spell_id = RE::FormID();
                if (si->ResolveFormID(pair.second, new_spell_id)) {
                    pair.second = new_spell_id;
                    continue;
                }
                SKSE::log::warn("cannot resolve old form ID {:08X}", pair.second);
                pair.second = 0;
            }
            std::erase_if(*ir, [](const std::pair<RE::FormID, RE::FormID>& pair) {
                return pair.second == 0;
            });

            if (ShoutmapFillFromIR(*map, *ir, *player) > 0) {
                SKSE::log::debug(
                    "{} spell power mapping loaded from SKSE cosave",
                    type == 'FAF' ? "fire-and-forget" : "concentration"
                );
            }
        }
    };

    static constexpr auto on_revert = [](SKSE::SerializationInterface* si) -> void {
        if (!si) {
            return;
        }
        auto lock = std::lock_guard(gMutex);
        gFafMap = Shoutmap(FafShouts());
        gConcMap = Shoutmap(ConcShouts());
    };

    si.SetUniqueID('ESAS');
    si.SetSaveCallback(on_save);
    si.SetLoadCallback(on_load);
    si.SetRevertCallback(on_revert);
}

}  // namespace

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    const auto* plugin_decl = SKSE::PluginDeclaration::GetSingleton();
    if (!plugin_decl) {
        SKSE::stl::report_and_fail("cannot get SKSE plugin declaration");
    }

    InitSettings();
    InitLogging(*plugin_decl);
    SKSE::Init(skse);

    const auto* mi = SKSE::GetMessagingInterface();
    const auto* si = SKSE::GetSerializationInterface();
    if (!mi) {
        SKSE::stl::report_and_fail("cannot get SKSE messaging interface");
    }
    if (!si) {
        SKSE::stl::report_and_fail("cannot get SKSE serialization interface");
    }

    InitSKSEMessaging(*mi);
    InitSKSESerialization(*si);

    SKSE::log::info(
        "{} {}.{}.{} loaded",
        plugin_decl->GetName(),
        plugin_decl->GetVersion().major(),
        plugin_decl->GetVersion().minor(),
        plugin_decl->GetVersion().patch()
    );
    return true;
}
