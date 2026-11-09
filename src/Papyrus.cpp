#include "Papyrus.h"

#include "Settings.h"
#include "Tears.h"

namespace Lacrima::Papyrus
{
	namespace
	{
		constexpr auto kScript = "Lacrima"sv;
		constexpr auto kTestScript = "LacrimaTest"sv;

		RE::ActorHandle HandleOf(RE::Actor* a_actor)
		{
			return a_actor ? a_actor->GetHandle() : RE::ActorHandle{};
		}

		std::int32_t GetAPIVersion(RE::StaticFunctionTag*)
		{
			return kAPIVersion;
		}

		void Cry(RE::StaticFunctionTag*, RE::Actor* a_actor, float a_intensity, float a_seconds)
		{
			Tears::Get().Cry(HandleOf(a_actor), a_intensity, a_seconds);
		}

		void StopCrying(RE::StaticFunctionTag*, RE::Actor* a_actor, bool a_wipe)
		{
			Tears::Get().Stop(HandleOf(a_actor), a_wipe);
		}

		void ShedTear(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_eye)
		{
			Tears::Get().Shed(HandleOf(a_actor), a_eye);
		}

		void WipeTears(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			Tears::Get().Wipe(HandleOf(a_actor));
		}

		void WipeTearsWithHand(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESIdleForm* a_idle)
		{
			Tears::Get().WipeWithHand(HandleOf(a_actor), a_idle);
		}

		bool IsCrying(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			return a_actor && Tears::Get().Query(a_actor->GetHandle()).crying;
		}

		float GetIntensity(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			return a_actor ? Tears::Get().Query(a_actor->GetHandle()).intensity : 0.0f;
		}

		float GetTimeLeft(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			return a_actor ? Tears::Get().Query(a_actor->GetHandle()).timeLeft : 0.0f;
		}

		bool IsEnabled(RE::StaticFunctionTag*)
		{
			auto guard = Settings::Lock();
			return Settings::Get().enabled;
		}

		void SetEnabled(RE::StaticFunctionTag*, bool a_enabled)
		{
			auto guard = Settings::Lock();
			Settings::Get().enabled = a_enabled;
		}

		void ReloadConfig(RE::StaticFunctionTag*)
		{
			Settings::Load();
		}

		RE::BSFixedString GetStatus(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			return Tears::Get().Status(HandleOf(a_actor));
		}

		void SetStreams(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_streams)
		{
			Tears::Get().SetStreams(HandleOf(a_actor), a_streams);
		}

		void SetMascara(RE::StaticFunctionTag*, RE::Actor* a_actor, float a_strength)
		{
			Tears::Get().SetMascara(HandleOf(a_actor), a_strength);
		}

		// LacrimaTest debug natives

		void NearbyNative(RE::StaticFunctionTag*, float a_radius, float a_intensity, float a_seconds)
		{
			Tears::Get().CryNearby(a_radius, a_intensity, a_seconds);
		}

		void ShowDuctsNative(RE::StaticFunctionTag*, bool a_show)
		{
			auto guard = Settings::Lock();
			Settings::Get().showDucts = a_show;
		}

		void MountWorldNative(RE::StaticFunctionTag*, bool a_world)
		{
			auto guard = Settings::Lock();
			Settings::Get().mountWorld = a_world;
		}

		void WipeAllNative(RE::StaticFunctionTag*)
		{
			Tears::Get().WipeAll();
		}

		std::int32_t CryingCountNative(RE::StaticFunctionTag*)
		{
			return static_cast<std::int32_t>(Tears::Get().CryingCount());
		}
	}

	bool Register(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("GetAPIVersion"sv, kScript, GetAPIVersion, true);
		a_vm->RegisterFunction("Cry"sv, kScript, Cry);
		a_vm->RegisterFunction("StopCrying"sv, kScript, StopCrying);
		a_vm->RegisterFunction("ShedTear"sv, kScript, ShedTear);
		a_vm->RegisterFunction("WipeTears"sv, kScript, WipeTears);
		a_vm->RegisterFunction("WipeTearsWithHand"sv, kScript, WipeTearsWithHand);
		a_vm->RegisterFunction("IsCrying"sv, kScript, IsCrying, true);
		a_vm->RegisterFunction("GetIntensity"sv, kScript, GetIntensity, true);
		a_vm->RegisterFunction("GetTimeLeft"sv, kScript, GetTimeLeft, true);
		a_vm->RegisterFunction("IsEnabled"sv, kScript, IsEnabled, true);
		a_vm->RegisterFunction("SetEnabled"sv, kScript, SetEnabled);
		a_vm->RegisterFunction("ReloadConfig"sv, kScript, ReloadConfig);
		a_vm->RegisterFunction("GetStatus"sv, kScript, GetStatus);
		a_vm->RegisterFunction("SetStreams"sv, kScript, SetStreams);
		a_vm->RegisterFunction("SetMascara"sv, kScript, SetMascara);

		a_vm->RegisterFunction("NearbyNative"sv, kTestScript, NearbyNative);
		a_vm->RegisterFunction("ShowDuctsNative"sv, kTestScript, ShowDuctsNative);
		a_vm->RegisterFunction("MountWorldNative"sv, kTestScript, MountWorldNative);
		a_vm->RegisterFunction("WipeAllNative"sv, kTestScript, WipeAllNative);
		a_vm->RegisterFunction("CryingCountNative"sv, kTestScript, CryingCountNative, true);
		logger::info("Papyrus API version {} registered", kAPIVersion);
		return true;
	}
}
