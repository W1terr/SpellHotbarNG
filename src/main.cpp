#include "casting/Actions.h"
#include "core/Bindings.h"
#include "casting/CastAnim.h"
#include "casting/Replacers.h"
#include "casting/SpellCharges.h"
#include "core/Config.h"
#include "ui/Icons.h"
#include "core/Input.h"
#include "ui/ItemIcons.h"
#include "ui/UI.h"

namespace
{
	// PlayerCharacter::Update drives game-time logic (charging, channeling, cooldowns)
	struct PlayerUpdate
	{
		static void thunk(RE::PlayerCharacter* a_player, float a_delta)
		{
			func(a_player, a_delta);
			std::scoped_lock lock(Config::Lock());
			Actions::Update(a_delta);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
		logs::info("Installed PlayerCharacter::Update hook");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			CastAnim::RegisterCondition();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			{
				std::scoped_lock lock(Config::Lock());
				Icons::Load();
				Config::Load();
				CastAnim::LoadTimings();
				SpellCharges::Load();
			}
			ItemIcons::Register();
			if (!UI::Register()) {
				RE::DebugMessageBox("Spell Hotbar NG needs SKSE Menu Framework (version 3 or newer). The hotbar is disabled.");
			}
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			{
				std::scoped_lock lock(Config::Lock());
				Actions::Reset();
				Input::Reset();
			}
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	logs::info("Spell Hotbar NG loading");

	Replacers::Generate();  // before OAR reads its folders (kInputLoaded)
	Bindings::RegisterSerialization();
	InstallHooks();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
