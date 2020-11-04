local ide_dir = iif(_ACTION == nil, "vs2015", _ACTION)
if "linux-gcc" == _OPTIONS["gcc"] then
	ide_dir = "gcc"
elseif "linux-gcc-5" == _OPTIONS["gcc"] then
	ide_dir = "gcc5"
elseif "linux-clang" == _OPTIONS["gcc"] then
	ide_dir = "clang"
end

local LOCATION = "tmp/" .. ide_dir
local BINARY_DIR = LOCATION .. "/bin/"
local build_physics = true
local build_unit_tests = true
local build_app = true
local build_studio = true
local build_gui = _ACTION == "vs2015"
local build_steam = false
local build_game = false
newoption {
		trigger = "gcc",
		value = "GCC",
		description = "Choose GCC flavor",
		allowed = {
			{ "asmjs",          	"Emscripten/asm.js"       	 		},
			{ "android-x86",    	"Android - x86"            	 		},
			{ "linux-gcc", 			"Linux (GCC compiler)" 				},
			{ "linux-gcc-5", 		"Linux (GCC-5 compiler)"			},
			{ "linux-clang", 		"Linux (Clang compiler)"			}
		}
	}
	


function strip()
	configuration { "asmjs" }
		postbuildcommands {
			"$(SILENT) echo Running asmjs finalize.",
			"$(SILENT) \"$(EMSCRIPTEN)/emcc\" -O2 "
--				.. "-s EMTERPRETIFY=1 "
--				.. "-s EMTERPRETIFY_ASYNC=1 "
				.. "-s TOTAL_MEMORY=268435456 "
--				.. "-s ALLOW_MEMORY_GROWTH=1 "
--				.. "-s USE_WEBGL2=1 "
				.. "--memory-init-file 1 "
				.. "\"$(TARGET)\" -o \"$(TARGET)\".html "
--				.. "--preload-file ../../../examples/runtime@/"
		}
		
	configuration { "android-x86", "Release" }
		postbuildcommands {
			"$(SILENT) echo Stripping symbols.",
			"$(SILENT) $(ANDROID_NDK_X86)/bin/i686-linux-android-strip -s \"$(TARGET)\""
		}

	configuration {}
end

		
function defaultConfigurations()
	configuration "Debug"
		targetdir(BINARY_DIR .. "Debug")
		defines { "DEBUG", "_DEBUG" }
		flags { "Symbols", "WinMain" }

	configuration "Release"
		targetdir(BINARY_DIR .. "Release")
		defines { "NDEBUG" }
		flags { "Optimize", "WinMain" }

	configuration "RelWithDebInfo"
		targetdir(BINARY_DIR .. "RelWithDebInfo")
		defines { "NDEBUG" }
		flags { "Symbols", "Optimize", "WinMain" }

	configuration "linux"
		buildoptions { "-std=c++14" }
		links { "pthread" }

	configuration { "asmjs" }
		buildoptions { "-std=c++14" }
		
end

function linkLib(lib)
	links {lib}

	for conf,conf_dir in pairs({Debug="debug", Release="release", RelWithDebInfo="release"}) do
		for platform,target_platform in pairs({win="windows", linux="linux", }) do
			configuration { "x64", conf, target_platform }
				libdirs {"../external/" .. lib .. "/lib/" .. platform .. "64" .. "_" .. ide_dir .. "/" .. conf_dir}
				libdirs {"../external/" .. lib .. "/dll/" .. platform .. "64" .. "_" .. ide_dir .. "/" .. conf_dir}
		end
	end
	for conf,conf_dir in pairs({Debug="debug", Release="release", RelWithDebInfo="release"}) do
		configuration { "android-x86", conf }
			libdirs {"../external/" .. lib .. "/lib/android-x86_gmake/" .. conf_dir}
	end
	configuration {}
end


function forceLink(name)

	configuration { "linux-*" }
		linkoptions {"-u " .. name}
	configuration { "x64", "vs*" }
		linkoptions {"/INCLUDE:" .. name}
	configuration {}
end

solution "OpenFBX"
	if _ACTION == "gmake" then
		configuration { "android-*" }
			flags {
				"NoImportLib",
			}
			includedirs {
				"$(ANDROID_NDK_ROOT)/sources/cxx-stl/gnu-libstdc++/4.9/include",
				"$(ANDROID_NDK_ROOT)/sources/android/native_app_glue",
			}
			linkoptions {
				"-nostdlib",
				"-static-libgcc",
			}
			links {
				"c",
				"dl",
				"m",
				"android",
				"log",
				"gnustl_static",
				"gcc",
			}
			buildoptions {
				"-fPIC",
				"-no-canonical-prefixes",
				"-Wa,--noexecstack",
				"-fstack-protector",
				"-ffunction-sections",
				"-Wno-psabi",
				"-Wunused-value",
				"-Wundef",
			}
			buildoptions_cpp {
				"-std=c++14",
			}
			linkoptions {
				"-no-canonical-prefixes",
				"-Wl,--no-undefined",
				"-Wl,-z,noexecstack",
				"-Wl,-z,relro",
				"-Wl,-z,now",
			}
		
		configuration { "android-x86" }
			androidPlatform = "android-24"
			libdirs {
				path.join(_libDir, "lib/android-x86"),
				"$(ANDROID_NDK_ROOT)/sources/cxx-stl/gnu-libstdc++/4.9/libs/x86",
			}
			includedirs {
				"$(ANDROID_NDK_ROOT)/sources/cxx-stl/gnu-libstdc++/4.9/libs/x86/include",
			}
			buildoptions {
				"--sysroot=" .. path.join("$(ANDROID_NDK_ROOT)/platforms", androidPlatform, "arch-x86"),
				"-march=i686",
				"-mtune=atom",
				"-mstackrealign",
				"-msse3",
				"-mfpmath=sse",
				"-Wunused-value",
				"-Wundef",
			}
			linkoptions {
				"--sysroot=" .. path.join("$(ANDROID_NDK_ROOT)/platforms", androidPlatform, "arch-x86"),
				path.join("$(ANDROID_NDK_ROOT)/platforms", androidPlatform, "arch-x86/usr/lib/crtbegin_so.o"),
				path.join("$(ANDROID_NDK_ROOT)/platforms", androidPlatform, "arch-x86/usr/lib/crtend_so.o"),
			}
	
		configuration {}	
	
		if "asmjs" == _OPTIONS["gcc"] then
			if not os.getenv("EMSCRIPTEN") then
				print("Set EMSCRIPTEN enviroment variable.")
			end
			premake.gcc.cc   = "\"$(EMSCRIPTEN)/emcc\""
			premake.gcc.cxx  = "\"$(EMSCRIPTEN)/em++\""
			premake.gcc.ar   = "\"$(EMSCRIPTEN)/emar\""
			_G["premake"].gcc.llvm = true
			premake.gcc.llvm = true
			LOCATION = "tmp/emscripten_gmake"
		
		elseif "android-x86" == _OPTIONS["gcc"] then
			if not os.getenv("ANDROID_NDK_X86") or not os.getenv("ANDROID_NDK_ROOT") then
				print("Set ANDROID_NDK_X86 and ANDROID_NDK_ROOT envrionment variables.")
			end

			premake.gcc.cc  = "\"$(ANDROID_NDK_X86)/bin/i686-linux-android-gcc\""
			premake.gcc.cxx = "\"$(ANDROID_NDK_X86)/bin/i686-linux-android-g++\""
			premake.gcc.ar  = "\"$(ANDROID_NDK_X86)/bin/i686-linux-android-ar\""
			LOCATION = "tmp/android-x86_gmake"
		
		elseif "linux-gcc" == _OPTIONS["gcc"] then
			LOCATION = "tmp/gcc"

		elseif "linux-gcc-5" == _OPTIONS["gcc"] then
			premake.gcc.cc  = "gcc-5"
			premake.gcc.cxx = "g++-5"
			premake.gcc.ar  = "ar"
			LOCATION = "tmp/gcc5"
			
		elseif "linux-clang" == _OPTIONS["gcc"] then
			premake.gcc.cc  = "clang"
			premake.gcc.cxx = "clang++"
			premake.gcc.ar  = "ar"
			LOCATION = "tmp/clang"

		end
		BINARY_DIR = LOCATION .. "/bin/"
	end
	
	configuration { "linux-*" }
		buildoptions {
			"-fPIC",
			"-no-canonical-prefixes",
			"-Wa,--noexecstack",
			"-fstack-protector",
			"-ffunction-sections",
			"-Wno-psabi",
			"-Wunused-value",
			"-Wundef",
			"-msse2",
		}
		linkoptions {
			"-Wl,--gc-sections",
		}
	
	configuration { "linux-*", "x32" }
		buildoptions {
			"-m32",
		}

	configuration { "linux-*", "x64" }
		buildoptions {
			"-m64",
		}

	configuration {}
	
	configurations { "Debug", "Release", "RelWithDebInfo" }
	platforms { "x64" }
	flags { 
		--"FatalWarnings", 
		"StaticRuntime",
		"NoPCH", 
		"NoExceptions", 
		"NoRTTI", 
		"NoEditAndContinue"
	}
	includedirs {"../src" }
	location(LOCATION)
	language "C++"
	startproject "studio"

	configuration { "vs*" }
		defines { "_HAS_EXCEPTIONS=0" }

	configuration "not macosx"
		removefiles { "../src/**/osx/*"}
		
	configuration "not linux"
		removefiles { "../src/**/linux/*"}
		
	configuration "not windows"
		removefiles { "../src/**/win/*"}

	configuration "asmjs"
		removefiles { "../src/**/win/*"}

	configuration "android-*"
		removefiles { "../src/**/win/*"}
		
	configuration "not asmjs" 
		removefiles { "../src/**/asmjs/*"}
	

project "openfbx"
	kind "WindowedApp"

	debugdir ("../runtime")
	
	files { "../src/**.c", "../src/**.cpp", "../demo/**.cpp", "../demo/**.h", "../src/**.h", "genie.lua" }
	defaultConfigurations()

	configuration {}
		defines {"_CRT_SECURE_NO_WARNINGS", "_HAS_ITERATOR_DEBUGGING=0" }
	
	configuration "Release"
		flags { "NoExceptions", "NoFramePointer", "NoIncrementalLink", "NoRTTI", "OptimizeSize", "No64BitChecks" }
--		linkoptions { "/NODEFAULTLIB"}
		linkoptions { "/MANIFEST:NO"}
