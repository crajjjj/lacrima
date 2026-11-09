#pragma once

#include <cstdint>

namespace RE
{
	class Actor;
	class TESIdleForm;
}

// ======================================================================================
// Lacrima - native C++ inter-plugin API
// ======================================================================================
//
// The same calls as the Lacrima Papyrus script, for SKSE plugins that want an actor
// to cry without a Papyrus round-trip.
//
// Consumers do NOT link against Lacrima - resolve the exports at runtime:
//
//     auto h = GetModuleHandleA("Lacrima.dll");                   // null if not installed
//     if (h) {
//         // decltype(&Fn) takes each pointer type straight from this header, so a
//         // signature change here becomes a compile error in your plugin instead of a
//         // silent mismatch. It inspects the declaration only - no link dependency.
//         auto cry = reinterpret_cast<decltype(&Lacrima_Cry)>(GetProcAddress(h, "Lacrima_Cry"));
//         if (cry) {
//             cry(actor, 0.7f, 20.0f);
//         }
//     }
//
// Resolving one export tells you Lacrima is present, NOT that every export is: an
// older build resolves Lacrima_Cry and leaves a newer name null. Null-check the
// pointer you are about to call, or gate a group on Lacrima_GetInterfaceVersion().
//
// -------------------------------------------------------------------------------------
// SEMANTICS
// -------------------------------------------------------------------------------------
// - Every call is safe from ANY thread. Requests (Cry, StopCrying, ShedTear, Wipe)
//   are queued and run on the main thread at the start of the next frame; queries
//   (IsCrying, GetIntensity, GetTimeLeft) read a snapshot taken every frame, which
//   a request updates at once, so IsCrying is true straight after Cry.
// - A null actor is ignored. An actor without loaded 3D cannot cry; the request is
//   dropped when it runs.
// - intensity is 0.0-1.0: how often and how heavily the eyes shed. 0 stops crying.
// - seconds <= 0 cries until StopCrying.
// - Cry on an actor who already cries replaces the intensity and the time left.
// - StopCrying stops new tears; the ones on the face run their course and dry up,
//   unless wipe is true, which removes them at once.
// - ShedTear sheds one tear even from an actor who is not crying. eye: 0 left,
//   1 right, 2 both, anything else one at random.
// - Wipe removes the tears on the face, and any running mascara, without changing
//   whether the actor cries.
// - WipeTearsWithHand plays idle (the vanilla brow wipe, Skyrim.esm 0977EC, when null)
//   and dries the face once a hand comes near the eyes, or after a few seconds if it
//   never does. An idle that cannot play (the actor is busy) dries the face at once.
//   Running mascara is smeared, not removed: wider, fainter, gone sooner.
// - CryEx (10100) is Cry with two more arguments, which hold until the next Cry or
//   CryEx for the actor. streams: how many points along the lower lid this crying
//   sheds from, 1 to 4; each tear picks one at random; 0 uses the player's setting,
//   as Cry does. mascara: running mascara for this crying, 0.0-1.0; negative uses
//   the actor's SetMascara, else the player's setting, as Cry does.
// - SetMascara (10100): running mascara for this actor, 0.0-1.0. Each tear leaves a
//   dark streak down its path that outlives the water and fades after a few minutes
//   (the player sets how long). 0 = none; a negative value goes back to the player's
//   setting, which is 0 unless they changed it. It belongs to the actor, not to one
//   cry, and lasts for the session: it does not survive loading a save.
// - Independently of any gesture, a crying actor's own hands take away the tears they
//   touch, in any animation, unless the player turned that off.
// - Lacrima sends the mod events Lacrima_CryStart (numArg = intensity) and
//   Lacrima_CryStop, with the actor as the sender, when crying starts and stops.
// - Crying does not survive loading a save, and no Lacrima_CryStop is sent then:
//   forget tracked actors on load, or ask Lacrima_IsCrying again.
//
// Interface version is packed MMmmpp and only ever grows: exports are appended,
// never changed or removed.

extern "C" {

// Lacrima's own version, packed major*10000 + minor*100 + patch (100 = 0.1.0).
std::uint32_t Lacrima_GetVersion();

// 10000 = 1.0.0: Cry .. WipeTearsWithHand. 10100 = 1.1.0: CryEx, SetMascara.
std::uint32_t Lacrima_GetInterfaceVersion();

void  Lacrima_Cry(RE::Actor* actor, float intensity, float seconds);
void  Lacrima_StopCrying(RE::Actor* actor, bool wipe);
bool  Lacrima_IsCrying(RE::Actor* actor);
float Lacrima_GetIntensity(RE::Actor* actor);
// Seconds left; -1 = until StopCrying; 0 = not crying.
float Lacrima_GetTimeLeft(RE::Actor* actor);
void  Lacrima_ShedTear(RE::Actor* actor, std::int32_t eye);
void  Lacrima_Wipe(RE::Actor* actor);
void  Lacrima_WipeTearsWithHand(RE::Actor* actor, RE::TESIdleForm* idle);

// 10100
void Lacrima_CryEx(RE::Actor* actor, float intensity, float seconds, std::int32_t streams, float mascara);
void Lacrima_SetMascara(RE::Actor* actor, float strength);
}
