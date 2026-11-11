#include "Factions.h"
#include "Hooks.h"
#include "Menu.h"
#include "Papyrus.h"
#include "Settings.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kInputLoaded:
			Lacrima::Hooks::Install();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Lacrima::Menu::Register();
			Lacrima::Factions::Load();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Lacrima::Hooks::ResetSession();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	// CommonLib loads the Address Library at the first relocation; load it here so a
	// missing one fails at game start, not mid-session
	(void)REL::IDDB::get();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} {} on Skyrim {}", plugin->GetName(), plugin->GetVersion().string("."sv), REL::Module::get().version().string("."sv));

	Lacrima::Settings::Load();

	auto* papyrus = SKSE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(Lacrima::Papyrus::Register)) {
		logger::error("could not register the Papyrus functions");
	}
	Lacrima::Factions::RegisterSerialization();
	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		logger::error("could not register the SKSE message listener; Lacrima stays off");
		return false;
	}
	return true;
}
