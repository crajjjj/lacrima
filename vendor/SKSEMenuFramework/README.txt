SKSEMenuFramework.h - the client header of "SKSE Menu Framework" (Nexus 120352).

Vendored verbatim, never edited here. Lacrima.dll includes it (through
src\Menu.cpp only) and calls into the framework's SKSEMenuFramework.dll at run
time through GetProcAddress: nothing is linked, and the framework's DLL is never
redistributed with Lacrima.

Source:  https://github.com/QTR-Modding/SKSE-Menu-Framework-3/blob/master/resources/SKSEMenuFramework.h
License: GPL-3.0 (the framework's repository)
Taken:   2026-11-08, as a byte copy of the one AudioUtilTTS vendors
sha256:  48416e8220ca777e2fffc2ef2baf21f699ab2e6c409d437f44eec5e311c3524c

Rules for using it:
- include it only in src\Menu.cpp;
- call ImGuiMCP:: functions only inside a callback the framework invoked - the
  wrappers do not null-check, so a call without the framework DLL is a crash;
- every wrapper the menu calls has its export on the probe list in src\Menu.cpp
  (kFrameworkExports), so a framework that lacks one turns the menu off instead;
- do not read ImGui structs (GetStyle, GetIO) directly.
