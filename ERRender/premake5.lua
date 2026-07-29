project "ERRender"
	kind "SharedLib"
	language "C++"
	staticruntime "off"
	
	pchheader "pch.h"
	pchsource "Source/pch.cpp"

	links
	{
		"Toshi",
		"BYardSDK",
		"BYModCore",
		"RmlUi",
		"d3d11.lib",
		"dxgi.lib",
		"d3dcompiler.lib",
		"SDL2.lib"
	}
	
	libdirs
	{
		"%{LibDir.fmod}",
		"%{LibDir.bink}",
		"%{LibDir.dx8}",
		"%{LibDir.detours}",
		"%{LibDir.sdl2}",
		"Vendor/freetype/lib",
	}

	files
	{
		"Source/**.h",
		"Source/**.cpp",
		"Source/**.c",
	}

	defines
	{
		"RMLUI_STATIC_LIB",
	}

	includedirs
	{
		"Source",
		"Vendor/freetype/include",
		"Vendor/nanosvg",
		"%{IncludeDir.rmlui}",
		"%{IncludeDir.toshi}",
		"%{IncludeDir.byardsdk}",
		"%{IncludeDir.modcore}",
		"%{IncludeDir.sdl2}",
		-- Always on the search path so TracyD3D11.hpp resolves even in non-profiled
		-- builds (its own TRACY_ENABLE guard then compiles the GPU zones to no-ops).
		"%{IncludeDir.tracy}",
	}
	
	-- Modloader specific
	debugdir ("%{wks.location}/../Game")
	debugcommand ("%{wks.location}/../Game/BYardModLoader.exe")
	
	postbuildcommands
	{
		"{COPYDIR} \"%{wks.location}bin/" .. outputdir .. "/%{prj.name}/\" %{wks.location}../Game/Mods/",
	}

	filter "system:windows"
		defines
		{
			"TOSHI_SDK",
			"TOSHI_MODLOADER_CLIENT",
			"TOSHI_PROFILER_CLIENT_DLL"
		}

	filter "files:**.c"
		flags { "NoPCH" }

	filter "files:Source/DirectXTex/**"
		flags { "NoPCH" }

	filter "files:Source/ImGuizmo/**"
		flags { "NoPCH" }

	filter "files:**NanoSVGImpl.cpp"
		flags { "NoPCH" }

	filter "configurations:Debug"
		links { "freetype_debug.lib" }

	filter "configurations:Release"
		links { "freetype_release.lib" }
		
		filter "configurations:Final"
		links { "freetype_release.lib" }