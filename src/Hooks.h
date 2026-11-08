#pragma once

namespace Lacrima::Hooks
{
	// Registers the frame driver: an input-event sink that queues one main-thread
	// task per frame, which runs the tear update. Call at kInputLoaded.
	void Install();

	// Drops every tear and every cached face (load, new game, main menu).
	void ResetSession();
}
