#include "Factions.h"

namespace Lacrima::Factions
{
	namespace
	{
		constexpr auto          kPlugin = "Lacrima.esp"sv;
		constexpr RE::FormID    kCryingID = 0x800;
		constexpr RE::FormID    kMascaraID = 0x801;
		constexpr std::uint32_t kCoSaveID = 'LCRM';
		constexpr std::uint32_t kRecord = 'FACT';
		constexpr std::uint32_t kRecordVersion = 1;
		constexpr std::uint32_t kMaxSaved = 4096;  // a corrupt count must not allocate the moon

		struct State
		{
			std::int8_t crying{ -1 };
			std::int8_t mascara{ -1 };
		};

		std::mutex                            lock;  // the save callback may run off the main thread
		std::unordered_map<RE::FormID, State> members;
		RE::TESFaction*                       cryingFaction = nullptr;
		RE::TESFaction*                       mascaraFaction = nullptr;

		void Set(RE::Actor* a_actor, RE::TESFaction* a_faction, std::int8_t a_was, std::int8_t a_now)
		{
			if (!a_actor || !a_faction || a_was == a_now) {
				return;
			}
			if (a_now < 0) {
				a_actor->RemoveFromFaction(a_faction);
			} else {
				a_actor->AddToFaction(a_faction, a_now);
			}
		}

		void SendEvent(const char* a_name, RE::Actor* a_actor, float a_value)
		{
			auto* source = SKSE::GetModCallbackEventSource();
			if (!source || !a_actor) {
				return;
			}
			SKSE::ModCallbackEvent event{ RE::BSFixedString(a_name), RE::BSFixedString(""), a_value, a_actor };
			source->SendEvent(&event);
		}

		void Apply(RE::FormID a_id, State a_was, State a_now, bool a_events)
		{
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
			if (!actor) {
				return;
			}
			Set(actor, cryingFaction, a_was.crying, a_now.crying);
			Set(actor, mascaraFaction, a_was.mascara, a_now.mascara);
			if (a_events && (a_was.mascara < 0) != (a_now.mascara < 0)) {
				SendEvent(a_now.mascara < 0 ? "Lacrima_MascaraStop" : "Lacrima_MascaraStart", actor, a_now.mascara < 0 ? 0.0f : static_cast<float>(a_now.mascara) / 100.0f);
			}
		}

		void OnSave(SKSE::SerializationInterface* a_interface)
		{
			std::vector<RE::FormID> ids;
			{
				std::scoped_lock guard(lock);
				ids.reserve(members.size());
				for (const auto& [id, state] : members) {
					ids.push_back(id);
				}
			}
			if (!a_interface->OpenRecord(kRecord, kRecordVersion)) {
				logger::error("could not open the co-save record; actors in the Lacrima factions stay in them after this save loads");
				return;
			}
			const auto count = static_cast<std::uint32_t>(ids.size());
			a_interface->WriteRecordData(count);
			for (const auto id : ids) {
				a_interface->WriteRecordData(id);
			}
		}

		// The save has these actors in the factions and nobody crying: take them out.
		void OnLoad(SKSE::SerializationInterface* a_interface)
		{
			std::uint32_t type = 0;
			std::uint32_t version = 0;
			std::uint32_t length = 0;
			while (a_interface->GetNextRecordInfo(type, version, length)) {
				if (type != kRecord || version != kRecordVersion) {
					continue;
				}
				std::uint32_t count = 0;
				if (a_interface->ReadRecordData(count) != sizeof(count) || count > kMaxSaved) {
					continue;
				}
				std::uint32_t cleared = 0;
				for (std::uint32_t i = 0; i < count; ++i) {
					RE::FormID saved = 0;
					if (a_interface->ReadRecordData(saved) != sizeof(saved)) {
						break;
					}
					RE::FormID id = saved;
					// a reference made at run time (FF...) keeps its ID; the rest follow the load order
					if ((saved >> 24) != 0xFF && !a_interface->ResolveFormID(saved, id)) {
						continue;
					}
					if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
						if (cryingFaction) {
							actor->RemoveFromFaction(cryingFaction);
						}
						if (mascaraFaction) {
							actor->RemoveFromFaction(mascaraFaction);
						}
						++cleared;
					}
				}
				if (cleared > 0) {
					logger::info("{} actor(s) taken out of the crying and mascara factions the save had them in", cleared);
				}
			}
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			std::scoped_lock guard(lock);
			members.clear();
		}
	}

	void Load()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		cryingFaction = data ? data->LookupForm<RE::TESFaction>(kCryingID, kPlugin) : nullptr;
		mascaraFaction = data ? data->LookupForm<RE::TESFaction>(kMascaraID, kPlugin) : nullptr;
		if (cryingFaction && mascaraFaction) {
			logger::info("factions: crying {:08X}, mascara {:08X}", cryingFaction->GetFormID(), mascaraFaction->GetFormID());
		} else {
			logger::info("{} is not loaded: no crying or mascara faction (tears are not affected)", kPlugin);
		}
	}

	void RegisterSerialization()
	{
		auto* serialization = SKSE::GetSerializationInterface();
		if (!serialization) {
			return;
		}
		serialization->SetUniqueID(kCoSaveID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}

	void Sync(std::span<const Member> a_members, bool a_events)
	{
		std::vector<std::tuple<RE::FormID, State, State>> changes;
		{
			std::scoped_lock guard(lock);
			if (members.empty() && a_members.empty()) {
				return;
			}
			for (auto it = members.begin(); it != members.end();) {
				const auto id = it->first;
				if (std::ranges::none_of(a_members, [&](const Member& a_member) { return a_member.actor == id; })) {
					changes.emplace_back(id, it->second, State{});
					it = members.erase(it);
				} else {
					++it;
				}
			}
			for (const auto& member : a_members) {
				if (member.actor == 0 || (member.crying < 0 && member.mascara < 0)) {
					continue;
				}
				auto&       state = members[member.actor];
				const State now{ member.crying, member.mascara };
				if (state.crying != now.crying || state.mascara != now.mascara) {
					changes.emplace_back(member.actor, state, now);
					state = now;
				}
			}
		}
		for (const auto& [id, was, now] : changes) {
			Apply(id, was, now, a_events);
		}
	}
}
