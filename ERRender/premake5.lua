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
		"d3d11.lib",
		"dxgi.lib",
		"d3dcompiler.lib",
		"Dwrite.lib",
		"D2d1.lib"
	}
	
	libdirs
	{
		"%{LibDir.fmod}",
		"%{LibDir.bink}",
		"%{LibDir.dx8}",
		"%{LibDir.detours}",
		"Vendor/freetype/lib",
	}

	files
	{
		"Source/**.h",
		"Source/**.cpp",
		"Source/**.c",
	}

	includedirs
	{
		"Source",
		"Vendor/freetype/include",
		"%{IncludeDir.toshi}",
		"%{IncludeDir.byardsdk}",
		"%{IncludeDir.modcore}",
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

	filter "configurations:Debug"
		links { "freetype_debug.lib" }

	filter "configurations:Release"
		links { "freetype_release.lib" }
		
		filter "configurations:Final"
		links { "freetype_release.lib" }