#include "API/LacrimaAPI.h"

#include "Tears.h"

// Native C++ exports for other SKSE plugins: thin, null-safe forwarders onto the
// same Tears calls the Papyrus natives use. Exported via exports.def; see
// include/API/LacrimaAPI.h for the consumer contract.

namespace
{
	RE::ActorHandle HandleOf(RE::Actor* a_actor)
	{
		return a_actor ? a_actor->GetHandle() : RE::ActorHandle{};
	}
}

extern "C" {

std::uint32_t Lacrima_GetVersion()
{
	const auto v = SKSE::PluginDeclaration::GetSingleton()->GetVersion();
	return static_cast<std::uint32_t>(v.major()) * 10000u + static_cast<std::uint32_t>(v.minor()) * 100u + static_cast<std::uint32_t>(v.patch());
}

std::uint32_t Lacrima_GetInterfaceVersion()
{
	return 10000;
}

void Lacrima_Cry(RE::Actor* actor, float intensity, float seconds)
{
	Lacrima::Tears::Get().Cry(HandleOf(actor), intensity, seconds);
}

void Lacrima_StopCrying(RE::Actor* actor, bool wipe)
{
	Lacrima::Tears::Get().Stop(HandleOf(actor), wipe);
}

bool Lacrima_IsCrying(RE::Actor* actor)
{
	return actor && Lacrima::Tears::Get().Query(actor->GetHandle()).crying;
}

float Lacrima_GetIntensity(RE::Actor* actor)
{
	return actor ? Lacrima::Tears::Get().Query(actor->GetHandle()).intensity : 0.0f;
}

float Lacrima_GetTimeLeft(RE::Actor* actor)
{
	return actor ? Lacrima::Tears::Get().Query(actor->GetHandle()).timeLeft : 0.0f;
}

void Lacrima_ShedTear(RE::Actor* actor, std::int32_t eye)
{
	Lacrima::Tears::Get().Shed(HandleOf(actor), eye);
}

void Lacrima_Wipe(RE::Actor* actor)
{
	Lacrima::Tears::Get().Wipe(HandleOf(actor));
}
}
