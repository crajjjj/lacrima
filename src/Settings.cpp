#include "Settings.h"

#include <fstream>

#include <toml++/toml.hpp>

namespace Lacrima
{
	namespace
	{
		constexpr auto kBaseFile = "Data/SKSE/Plugins/Lacrima.toml";
		constexpr auto kUserFile = "Data/SKSE/Plugins/Lacrima_User.toml";

		std::recursive_mutex g_lock;
		Settings             g_settings;
		Settings             g_defaults;

		// clang-format off
		const std::array kFields{
			Field{ "general", "enabled", &Settings::enabled, 0, 1, "Test", "Tears enabled", nullptr,
				"Off removes every tear at once and ignores new requests." },
			Field{ "general", "debug_log", &Settings::debugLog, 0, 1, "Test", "Detailed log", nullptr,
				"Writes face, duct and fluid details to Lacrima.log." },

			Field{ "tears", "rate", &Settings::rate, 0.5f, 40.0f, "Tears", "Tears per minute", "%.1f",
				"How many tears each eye sheds per minute at full intensity. Lower intensity sheds fewer." },
			Field{ "tears", "drop", &Settings::drop, 2, 20, "Tears", "Drop size", "%d",
				"Fluid particles in one tear at full intensity. More makes a heavier tear that runs further." },
			Field{ "tears", "well", &Settings::well, 0.1f, 4.0f, "Tears", "Welling time", "%.2f s",
				"Seconds a tear takes to gather at the lower lid." },
			Field{ "tears", "hold", &Settings::hold, 0.0f, 4.0f, "Tears", "Cling time", "%.2f s",
				"Seconds a full tear clings to the lid before it runs down." },
			Field{ "tears", "duct_position", &Settings::ductPosition, 0.0f, 1.0f, "Tears", "Where tears start", "%.2f",
				"0 is the inner corner of the eye, beside the nose; 1 is the outer corner." },
			Field{ "tears", "duct_below", &Settings::ductBelow, 0.3f, 2.0f, "Tears", "Start below the eye", "%.2f",
				"How far below the middle of the eye tears gather, in eye sizes." },
			Field{ "tears", "both_eyes", &Settings::bothEyes, 0, 1, "Tears", "Both eyes", nullptr,
				"Off sheds tears from one eye only, picked at random each time someone starts crying." },
			Field{ "tears", "max_actors", &Settings::maxActors, 1, 16, "Tears", "Actors at once", "%d",
				"How many actors can cry at the same time. The oldest stops when a new one starts past the limit." },
			Field{ "tears", "max_particles", &Settings::maxParticles, 20, 600, "Tears", "Particles per eye", "%d",
				"Cap on the fluid on one cheek. Older trails stop growing past it." },

			Field{ "look", "size", &Settings::size, 0.5f, 3.0f, "Look", "Tear size", "%.2f",
				"Scales every drop. Real tears are about 1; bigger reads better from a distance." },
			Field{ "look", "trail", &Settings::trail, 0.2f, 2.0f, "Look", "Trail width", "%.2f",
				"Thickness of the wet trail a tear leaves on the cheek." },
			Field{ "look", "opacity", &Settings::opacity, 0.05f, 1.0f, "Look", "Opacity", "%.2f",
				"How much the tears hide the skin under them." },
			Field{ "look", "shine", &Settings::shine, 0.0f, 10.0f, "Look", "Shine", "%.2f",
				"Strength of the wet highlight." },
			Field{ "look", "gloss", &Settings::gloss, 1.0f, 1000.0f, "Look", "Gloss", "%.0f",
				"Sharpness of the highlight. High is a small bright glint, low a broad sheen." },
			Field{ "look", "lifetime", &Settings::lifetime, 3.0f, 300.0f, "Look", "Trail lifetime", "%.0f s",
				"Seconds a tear trail stays wet on the face." },
			Field{ "look", "fade", &Settings::fade, 0.2f, 30.0f, "Look", "Fade", "%.1f s",
				"Seconds at the end of its life a trail spends thinning out." },

			Field{ "physics", "stickiness", &Settings::stickiness, 0.0f, 3.0f, "Physics", "Stickiness", "%.2f",
				"How firmly a slow tear sticks to the skin and stops." },
			Field{ "physics", "friction", &Settings::friction, 1.0f, 200.0f, "Physics", "Skin friction", "%.0f",
				"Drag on a tear running over skin. Higher runs slower." },
			Field{ "physics", "viscosity", &Settings::viscosity, 0.0f, 100.0f, "Physics", "Viscosity", "%.0f",
				"How much a tear moves as one. Water is low." },
			Field{ "physics", "cohesion", &Settings::cohesion, 0.0f, 20.0f, "Physics", "Cohesion", "%.2f",
				"How strongly a tear holds together as a bead." },
			Field{ "physics", "stretch", &Settings::stretch, 0.0f, 30.0f, "Physics", "Trail stretch", "%.1f",
				"How easily a running tear stretches into a trail before it breaks." },
			Field{ "physics", "fall", &Settings::fall, 0.1f, 3.0f, "Physics", "Falling drop life", "%.2f s",
				"Seconds a drop that falls off the face stays visible." },

			Field{ "debug", "show_ducts", &Settings::showDucts, 0, 1, "Test", "Show tear starts", nullptr,
				"Marks where tears start on the console-selected actor, or on the player." },
		};
		// clang-format on

		void Clamp(Settings& a_settings)
		{
			for (const auto& field : kFields) {
				std::visit(
					[&](auto a_member) {
						using T = std::remove_reference_t<decltype(a_settings.*a_member)>;
						if constexpr (std::is_same_v<T, int>) {
							a_settings.*a_member = std::clamp(a_settings.*a_member, static_cast<int>(field.min), static_cast<int>(field.max));
						} else if constexpr (std::is_same_v<T, float>) {
							const float value = a_settings.*a_member;
							a_settings.*a_member = std::isfinite(value) ? std::clamp(value, field.min, field.max) : field.min;
						}
					},
					field.member);
			}
		}

		bool ReadFile(const char* a_path, Settings& a_settings, bool a_required)
		{
			std::error_code error;
			if (!std::filesystem::exists(a_path, error)) {
				if (a_required) {
					logger::warn("{} is missing; using built-in defaults", a_path);
				}
				return false;
			}
			toml::table table;
			try {
				table = toml::parse_file(a_path);
			} catch (const toml::parse_error& e) {
				logger::error("{}: {} (line {}); file ignored", a_path, e.description(), e.source().begin.line);
				return false;
			}
			for (const auto& field : kFields) {
				const auto node = table[field.section][field.key];
				if (!node) {
					continue;
				}
				std::visit(
					[&](auto a_member) {
						using T = std::remove_reference_t<decltype(a_settings.*a_member)>;
						if constexpr (std::is_same_v<T, bool>) {
							if (const auto value = node.template value<bool>()) {
								a_settings.*a_member = *value;
							}
						} else if constexpr (std::is_same_v<T, int>) {
							if (const auto value = node.template value<std::int64_t>()) {
								a_settings.*a_member = static_cast<int>(*value);
							}
						} else {
							if (const auto value = node.template value<double>()) {
								a_settings.*a_member = static_cast<float>(*value);
							}
						}
					},
					field.member);
			}
			Clamp(a_settings);
			return true;
		}
	}

	std::span<const Field> Fields()
	{
		return kFields;
	}

	std::unique_lock<std::recursive_mutex> Settings::Lock()
	{
		return std::unique_lock{ g_lock };
	}

	Settings& Settings::Get()
	{
		return g_settings;
	}

	Settings Settings::Snapshot()
	{
		auto guard = Lock();
		return g_settings;
	}

	const Settings& Settings::Defaults()
	{
		return g_defaults;
	}

	void Settings::Load()
	{
		auto     guard = Lock();
		Settings loaded;
		ReadFile(kBaseFile, loaded, true);
		g_defaults = loaded;
		const bool user = ReadFile(kUserFile, loaded, false);
		g_settings = loaded;
		logger::info("settings loaded from {}{}", kBaseFile, user ? std::format(" and {}", kUserFile) : std::string{});
	}

	bool Settings::Save()
	{
		auto        guard = Lock();
		toml::table table;
		for (const auto& field : kFields) {
			std::visit(
				[&](auto a_member) {
					const auto value = g_settings.*a_member;
					if (value == g_defaults.*a_member) {
						return;
					}
					if (!table.contains(field.section)) {
						table.insert(field.section, toml::table{});
					}
					auto* section = table[field.section].as_table();
					using T = std::remove_cvref_t<decltype(value)>;
					if constexpr (std::is_same_v<T, bool>) {
						section->insert_or_assign(field.key, value);
					} else if constexpr (std::is_same_v<T, int>) {
						section->insert_or_assign(field.key, static_cast<std::int64_t>(value));
					} else {
						section->insert_or_assign(field.key, std::round(static_cast<double>(value) * 1000.0) / 1000.0);
					}
				},
				field.member);
		}
		const std::filesystem::path target{ kUserFile };
		std::error_code             error;
		if (table.empty()) {
			std::filesystem::remove(target, error);
			return true;
		}
		auto temporary = target;
		temporary += ".tmp";
		{
			std::ofstream out{ temporary, std::ios::trunc };
			if (!out) {
				logger::error("cannot write {}", temporary.string());
				return false;
			}
			out << "# Written by the Lacrima settings menu: only the values that differ from Lacrima.toml.\n"
				   "# Delete this file to go back to the shipped defaults.\n\n";
			out << table << '\n';
			if (!out) {
				logger::error("cannot write {}", temporary.string());
				return false;
			}
		}
		std::filesystem::rename(temporary, target, error);
		if (error) {
			logger::error("cannot replace {}: {}", target.string(), error.message());
			std::filesystem::remove(temporary, error);
			return false;
		}
		return true;
	}

	void Settings::ResetToDefaults()
	{
		auto guard = Lock();
		g_settings = g_defaults;
	}
}
