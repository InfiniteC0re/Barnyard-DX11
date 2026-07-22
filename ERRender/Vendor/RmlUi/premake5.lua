project "RmlUi"
	kind "StaticLib"
	language "C++"
	staticruntime "off"

	files
	{
		"Source/Core/**.cpp",
		"Source/Core/**.h",
		"Source/Debugger/**.cpp",
		"Source/Debugger/**.h",
	}

	includedirs
	{
		"Include",
		"%{IncludeDir.freetype}",
	}

	defines
	{
		"RMLUI_STATIC_LIB",
		"RMLUI_FONT_ENGINE_FREETYPE",
	}
