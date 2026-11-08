#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

// CommonLib pulls real <Windows.h> into the include chain; scrub the macro
// collisions (min/max are handled by NOMINMAX in xmake.lua)
#undef GetObject

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std::literals;

namespace logger = SKSE::log;
