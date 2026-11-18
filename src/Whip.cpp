#include "Whip.h"

#include "Settings.h"
#include "Tears.h"

namespace Lacrima::Whip
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct Tally
		{
			int               hits{ 0 };
			Clock::time_point last;
		};

		constexpr std::size_t kPruneAt = 64;

		std::mutex                              g_lock;
		std::vector<std::string>                g_names;     // lower case
		std::unordered_set<RE::FormID>          g_forms;     // weapons, spells, enchantments
		std::vector<RE::BGSKeyword*>            g_keywords;
		std::unordered_map<RE::FormID, bool>    g_verdicts;  // hit source -> is a whip
		std::unordered_map<RE::FormID, Tally>   g_tallies;   // by actor

		char Lower(char a_char)
		{
			return a_char >= 'A' && a_char <= 'Z' ? static_cast<char>(a_char - 'A' + 'a') : a_char;
		}

		bool Alnum(char a_char)
		{
			return (a_char >= 'a' && a_char <= 'z') || (a_char >= '0' && a_char <= '9');
		}

		std::string Lowered(std::string_view a_text)
		{
			std::string lowered{ a_text };
			std::ranges::transform(lowered, lowered.begin(), Lower);
			return lowered;
		}

		// A term matches where a word starts: "cane" finds "Leather Cane" and "Canes",
		// not "Arcane Bolt".
		bool Names(std::string_view a_name, std::string_view a_term)
		{
			for (auto at = a_name.find(a_term); at != std::string_view::npos; at = a_name.find(a_term, at + 1)) {
				if (at == 0 || !Alnum(a_name[at - 1])) {
					return true;
				}
			}
			return false;
		}

		// "Plugin.esp|0x800"
		RE::TESForm* Resolve(const std::string& a_ref)
		{
			const auto bar = a_ref.find('|');
			auto*      data = RE::TESDataHandler::GetSingleton();
			if (bar == std::string::npos || !data) {
				return nullptr;
			}
			try {
				const auto id = static_cast<RE::FormID>(std::stoul(a_ref.substr(bar + 1), nullptr, 16));
				return data->LookupForm(id, a_ref.substr(0, bar));
			} catch (const std::exception&) {
				return nullptr;
			}
		}

		// g_lock held
		bool IsWhip(RE::FormID a_source)
		{
			if (a_source == 0) {
				return false;
			}
			if (const auto it = g_verdicts.find(a_source); it != g_verdicts.end()) {
				return it->second;
			}
			bool  whip = g_forms.contains(a_source);
			auto* form = whip ? nullptr : RE::TESForm::LookupByID(a_source);
			if (form) {
				if (const auto* keyworded = form->As<RE::BGSKeywordForm>()) {
					whip = std::ranges::any_of(g_keywords, [&](const RE::BGSKeyword* a_keyword) { return keyworded->HasKeyword(a_keyword); });
				}
				if (!whip) {
					const char* name = form->GetName();
					const auto  lowered = Lowered(name ? name : "");
					whip = !lowered.empty() && std::ranges::any_of(g_names, [&](const std::string& a_term) { return Names(lowered, a_term); });
				}
			}
			g_verdicts.emplace(a_source, whip);
			return whip;
		}

		// Counts one hit and, from the threshold on, asks for the crying. Returns the
		// count. g_lock held.
		int Count(RE::Actor* a_actor, const Settings& a_settings)
		{
			const auto now = Clock::now();
			const auto window = std::chrono::duration<float>(a_settings.whipWindow);
			if (g_tallies.size() >= kPruneAt) {
				std::erase_if(g_tallies, [&](const auto& a_entry) { return now - a_entry.second.last > window; });
			}
			auto& tally = g_tallies[a_actor->GetFormID()];
			if (now - tally.last > window) {
				tally.hits = 0;
			}
			++tally.hits;
			tally.last = now;
			if (tally.hits < a_settings.whipHits) {
				return tally.hits;
			}
			// Someone else's crying is never cut short or weakened: one without an end
			// stays as it is, a longer or heavier one keeps its length and weight.
			const auto handle = a_actor->GetHandle();
			const auto state = Tears::Get().Query(handle);
			if (state.crying && state.timeLeft < 0.0f) {
				return tally.hits;
			}
			float intensity = a_settings.whipIntensity;
			float seconds = a_settings.whipSeconds;
			if (state.crying) {
				intensity = std::max(intensity, state.intensity);
				seconds = std::max(seconds, state.timeLeft);
			}
			Tears::Get().Cry(handle, intensity, seconds);
			return tally.hits;
		}

		class HitSink : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (!a_event || !a_event->target || a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto settings = Settings::Snapshot();
				if (!settings.enabled || !settings.whipEnabled) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* actor = a_event->target->As<RE::Actor>();
				if (!actor || actor->IsDead() || !(actor->IsPlayerRef() ? settings.whipPlayer : settings.whipNpcs)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				std::scoped_lock guard(g_lock);
				if (!IsWhip(a_event->source)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const int hits = Count(actor, settings);
				if (settings.debugLog) {
					logger::info("whip: {} ({:08X}) hit with {:08X}, {} of {}", actor->GetName(), actor->GetFormID(), a_event->source, hits,
						settings.whipHits);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		static HitSink sink;
		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESHitEvent>(&sink);
			logger::info("whip: listening for hits");
		} else {
			logger::error("whip: no script event source; crying from a whipping stays off");
		}
	}

	void Load()
	{
		const auto       lists = Settings::Whips();
		std::scoped_lock guard(g_lock);
		g_names.clear();
		g_forms.clear();
		g_keywords.clear();
		g_verdicts.clear();
		for (const auto& name : lists.names) {
			if (auto lowered = Lowered(name); !lowered.empty()) {
				g_names.push_back(std::move(lowered));
			}
		}
		for (const auto& ref : lists.forms) {
			auto* form = Resolve(ref);
			if (!form) {
				logger::info("whip: {} is not in this load order", ref);
			} else if (auto* keyword = form->As<RE::BGSKeyword>()) {
				g_keywords.push_back(keyword);
			} else {
				g_forms.insert(form->GetFormID());
			}
		}
		logger::info("whip: {} name(s), {} form(s), {} keyword(s)", g_names.size(), g_forms.size(), g_keywords.size());
	}

	std::string Test(RE::Actor* a_actor, int a_hits)
	{
		if (!a_actor) {
			return "No actor.";
		}
		const auto       settings = Settings::Snapshot();
		std::scoped_lock guard(g_lock);
		int              hits = 0;
		for (int i = 0; i < std::clamp(a_hits, 1, 100); ++i) {
			hits = Count(a_actor, settings);
		}
		return std::format("{} whip hit(s) counted, crying starts at {}: {}.", hits, settings.whipHits,
			hits >= settings.whipHits ? "crying" : "not yet");
	}

	void Reset()
	{
		std::scoped_lock guard(g_lock);
		g_tallies.clear();
	}
}
