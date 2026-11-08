#include "Hooks.h"

#include "Face.h"
#include "Tears.h"

// The frame driver is adapted from OStrands' Hooks.cpp by LeonVanWinkle (Saint),
// GPL-3.0: the input device manager sends its event every frame, and the sink
// turns that into one task on the main thread, never more than two in flight.

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

			void Update()
			{
				const std::uint64_t frame = _frame.load(std::memory_order_acquire);
				if (frame == _handled) {
					return;
				}
				const std::uint64_t passed = frame - _handled;
				_handled = frame;
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
					const float delta = RE::GetSecondsSinceLastFrame() * static_cast<float>(std::min(passed, kMaxCatchUp));
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
	}

	void ResetSession()
	{
		Tears::Get().Reset();
		Face::Reset();
	}

	void Install()
	{
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
		logger::info("frame driver registered");
	}
}
