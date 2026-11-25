#include "Hooks.h"

#include "Face.h"
#include "Tears.h"
#include "Whip.h"

// Two ways to run once a frame on the main thread.
//
// The first choice is a call early in the game's main update (a call to a void
// function, the site most plugins tick from): the frame runs at a fixed place, in
// step with the game, whatever other plugins do with input or the task queue.
//
// The fallback, when that site does not hold a call on this runtime, is the driver
// adapted from OStrands' Hooks.cpp by LeonVanWinkle (Saint), GPL-3.0: the input
// device manager sends its event every frame, and the sink turns that into one
// task on the main thread, never more than two in flight. It patches nothing and
// needs no address, but a plugin that swallows input events ahead of it stops it.

namespace Lacrima::Hooks
{
	namespace
	{
		class FrameDriver : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static FrameDriver* Get()
			{
				static FrameDriver driver;
				return &driver;
			}

			// The main update hook: one frame, now.
			void Frame()
			{
				Run(1);
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const*, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				_frame.fetch_add(1, std::memory_order_acq_rel);
				auto* tasks = SKSE::GetTaskInterface();
				if (tasks && _pending.load(std::memory_order_acquire) < kMaxPending) {
					_pending.fetch_add(1, std::memory_order_acq_rel);
					tasks->AddTask([this]() {
						Update();
						_pending.fetch_sub(1, std::memory_order_acq_rel);
					});
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			static constexpr int           kMaxPending = 2;
			static constexpr std::uint64_t kMaxCatchUp = 4;

			// The input sink's task: the frames counted since the last one that ran.
			void Update()
			{
				const std::uint64_t frame = _frame.load(std::memory_order_acquire);
				if (frame == _handled) {
					return;
				}
				const std::uint64_t passed = frame - _handled;
				_handled = frame;
				Run(passed);
			}

			void Run(std::uint64_t a_passed)
			{
				auto*      ui = RE::UI::GetSingleton();
				const bool mainMenu = ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
				if (mainMenu != _mainMenu) {
					_mainMenu = mainMenu;
					if (mainMenu) {
						ResetSession();
					}
				}
				if (mainMenu) {
					return;
				}
				const bool editingBody = ui && ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
				if (editingBody != _editingBody) {
					_editingBody = editingBody;
					ResetSession();
				}
				if (editingBody) {
					return;
				}
				try {
					if (ui && ui->GameIsPaused()) {
						Tears::Get().Update(0.0f, true);
						return;
					}
					// the game's own frame time: it follows slow motion and the time scale
					const float delta = RE::GetSecondsSinceLastFrame() * static_cast<float>(std::min(a_passed, kMaxCatchUp));
					Tears::Get().Update(delta, false);
				} catch (const std::exception& error) {
					logger::error("frame update failed, all tears dropped: {}", error.what());
					ResetSession();
				}
			}

			std::atomic<std::uint64_t> _frame{ 0 };
			std::atomic<int>           _pending{ 0 };
			std::uint64_t              _handled{ 0 };
			bool                       _mainMenu{ false };
			bool                       _editingBody{ false };
		};

		// The call in the main update. The function it calls takes nothing and
		// returns nothing; it runs first, then the frame.
		struct MainUpdate
		{
			static void thunk()
			{
				func();
				FrameDriver::Get()->Frame();
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Where that call sits: the main update function (SE 35565, AE 36564) plus an
		// offset that moved with the game. On 1.7.x the 1.6 offset is a call as well,
		// to another function, so the offset is picked by version, never probed.
		std::uintptr_t MainUpdateSite()
		{
			const std::ptrdiff_t ae = REL::Module::IsAtLeast(SKSE::RUNTIME_SSE_1_7_99) ? 0xC38 : 0xC26;
			return REL::VariantID(35565, 36564, 0x5BAB10).address() + REL::VariantOffset(0x748, ae, 0x7EE).offset();
		}

		bool HookMainUpdate()
		{
			const std::uintptr_t site = MainUpdateSite();
			// a relative call (E8); anything else is a runtime this table does not know
			if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
				logger::warn("main update: no call at {:X}, the offset does not fit this runtime", site - REL::Module::get().base());
				return false;
			}
			// the 14 bytes asked for in SKSE::Init; without them the write would end the game
			auto& trampoline = SKSE::GetTrampoline();
			if (trampoline.free_size() < 14) {
				logger::warn("main update: no trampoline memory");
				return false;
			}
			MainUpdate::func = trampoline.write_call<5>(site, MainUpdate::thunk);
			return true;
		}
	}

	void ResetSession()
	{
		Tears::Get().Reset();
		Face::Reset();
		Whip::Reset();
	}

	void Install()
	{
		if (HookMainUpdate()) {
			logger::info("frame driver: main update hook");
			return;
		}
		if (!SKSE::GetTaskInterface()) {
			logger::error("no SKSE task interface; Lacrima stays off");
			return;
		}
		auto* input = RE::BSInputDeviceManager::GetSingleton();
		if (!input) {
			logger::error("no input device manager; Lacrima stays off");
			return;
		}
		input->PrependEventSink(FrameDriver::Get());
		logger::info("frame driver: input events (fallback)");
	}
}
