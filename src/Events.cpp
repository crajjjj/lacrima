#include "Events.h"

#include "Tears.h"

#include <charconv>

namespace Lacrima::Events
{
	namespace
	{
		constexpr float kIntensity = 0.6f;  // Lacrima.Cry's defaults
		constexpr float kSeconds = 20.0f;

		bool Same(const RE::BSFixedString& a_name, std::string_view a_event)
		{
			const std::string_view name{ a_name.c_str() ? a_name.c_str() : "" };
			return std::ranges::equal(name, a_event, [](char a_left, char a_right) {
				return std::tolower(static_cast<unsigned char>(a_left)) == std::tolower(static_cast<unsigned char>(a_right));
			});
		}

		// The numbers in "30, 2", in order; a piece that is not a number ends it.
		std::vector<float> Numbers(std::string_view a_text)
		{
			std::vector<float> numbers;
			const char*        at = a_text.data();
			const char* const  end = at + a_text.size();
			while (at < end) {
				if (*at == ' ' || *at == ',' || *at == ';' || *at == '\t') {
					++at;
					continue;
				}
				float      value = 0.0f;
				const auto read = std::from_chars(at, end, value);
				if (read.ec != std::errc{} || !std::isfinite(value)) {
					break;
				}
				numbers.push_back(value);
				at = read.ptr;
			}
			return numbers;
		}

		class Sink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (!a_event || !a_event->sender) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const bool cry = Same(a_event->eventName, "Lacrima_Cry");
				const bool stop = !cry && Same(a_event->eventName, "Lacrima_StopCrying");
				if (!cry && !stop && !Same(a_event->eventName, "Lacrima_SetMascara")) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* actor = a_event->sender->As<RE::Actor>();
				if (!actor) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (stop) {
					Tears::Get().Stop(actor->GetHandle(), a_event->numArg != 0.0f);
					return RE::BSEventNotifyControl::kContinue;
				}
				if (!cry) {
					Tears::Get().SetMascara(actor->GetHandle(), a_event->numArg);
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto  numbers = Numbers(a_event->strArg.c_str() ? a_event->strArg.c_str() : "");
				const float intensity = a_event->numArg > 0.0f ? a_event->numArg : kIntensity;
				const float seconds = numbers.size() > 0 ? numbers[0] : kSeconds;
				const int   streams = numbers.size() > 1 ? static_cast<int>(numbers[1]) : 0;
				Tears::Get().Cry(actor->GetHandle(), intensity, seconds, streams);
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		static Sink sink;
		if (auto* source = SKSE::GetModCallbackEventSource()) {
			source->AddEventSink(&sink);
			logger::info("events: listening for Lacrima_Cry, Lacrima_StopCrying and Lacrima_SetMascara");
		} else {
			logger::error("events: no mod event source; Lacrima_Cry, Lacrima_StopCrying and Lacrima_SetMascara are not heard");
		}
	}
}
