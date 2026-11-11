set_xmakever("3.0.0")  -- the commonlibsse-ng submodule requires 3.0

-- Globals
PROJECT_NAME = "Lacrima"
PROJECT_VERSION = "0.2.6"
PROJECT_AUTHOR = "crajjjj"

-- Project
set_project(PROJECT_NAME)
set_version(PROJECT_VERSION)
set_languages("cxx23")
set_license("gplv3")
set_warnings("allextra")

-- Options
option("copy_to_mod")
    set_default(false)
    set_description("Copy dist/* to a mod folder (XSE_TES5_MODS_PATH)")
option_end()

option("mod_name")
    set_default("Lacrima")
    set_description("Folder under XSE_TES5_MODS_PATH that copy_to_mod writes to")
option_end()

-- Dependencies & Includes
-- Lacrima installs no trampoline hooks, so CommonLib's patch-site diagnostics
-- have nothing to check; off, their MinHook HDE64 code stays out of the DLL.
set_config("skse_patch_safety", false)
includes("lib/commonlibsse-ng")

add_requires("toml++")

-- policies
set_policy("package.requires_lock", true)

-- rules
add_rules("mode.debug", "mode.release")

if is_mode("debug") then
    add_defines("DEBUG")
    set_optimize("none")
    set_runtimes("MTd")
elseif is_mode("release") then
    add_defines("NDEBUG")
    set_optimize("fastest")
    set_symbols("debug")
    set_runtimes("MT")
end

add_defines("_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING")
add_defines("NOMINMAX")  -- CommonLib headers pull in Windows.h

-- Target
target(PROJECT_NAME)
    set_kind("shared")

    -- CommonLibSSE-NG
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = PROJECT_NAME,
        author = PROJECT_AUTHOR,
        description = "Procedural tears: fluid that wells up at the eyes and runs down the face."
    })

    -- Packages
    add_packages("toml++")

    -- Source files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src", "include", "vendor")
    set_pcxxheader("src/PCH.h")

    -- Exports (shared targets link via shflags; ldflags would be silently ignored)
    add_shflags("/DEF:exports.def", { force = true })

    -- flags
    add_cxxflags(
        "cl::/diagnostics:caret",
        "cl::/wd4200",
        "cl::/wd4201",
        "cl::/Zc:preprocessor",
        "cl::/utf-8"
    )

    if is_mode("debug") then
        add_cxxflags("cl::/bigobj")
    end

    -- Post Build
    after_build(function (target)
        local plugin_folder = path.join(os.projectdir(), "dist", "SKSE", "Plugins")
        if not os.isdir(plugin_folder) then
            os.mkdir(plugin_folder)
        end
        os.cp(target:targetfile(), plugin_folder)
        if is_mode("debug") then
            local pdb = target:symbolfile()
            if pdb then
                os.cp(pdb, plugin_folder)
            end
        end

        local mod_folder = os.getenv("XSE_TES5_MODS_PATH")
        if mod_folder and has_config("copy_to_mod") then
            os.cp("dist/*", path.join(mod_folder, get_config("mod_name") or "Lacrima"))
        end
    end)
target_end()

-- Papyrus compile: xmake build papyrus
-- Runs Pyro on Lacrima.ppj; also refreshes Release\Lacrima-<version>.zip
-- (see scripts\pyro.lua; overrides: PYRO_EXE, SKYRIM_GAME_PATH)
target("papyrus")
    set_kind("phony")
    set_default(false)
    on_build(function (target)
        import("scripts.pyro")
        pyro()
    end)
target_end()

-- Release package: xmake build release
-- Same as `papyrus` but rebuilds the DLL first so the archive always carries a
-- fresh DLL.
target("release")
    set_kind("phony")
    set_default(false)
    add_deps(PROJECT_NAME)
    on_build(function (target)
        import("scripts.pyro")
        pyro()
    end)
target_end()
