local win64 = {
	Env = {
		LIBPATH = {
			"Code/embree/lib/win-x64/", "Code/tbb/lib/"
		},
		GENERATE_PDB = "1",
		CPPDEFS = { 
			"_CRT_SECURE_NO_WARNINGS",
			{ "_DEBUG"; Config = "*-*-debug-*"},
		},
		CCOPTS = { 
			"/FS",	-- fatal error C1041: cannot open program database 'filename.pdb'; if multiple CL.EXE write to the same .PDB file, please use /FS
			{ "/Od", "/MTd"; Config = "*-*-debug-*" },
			{ "/O2", "/Zo", "/MT"; Config = "*-*-release" }
		},
		CXXOPTS = { 
			"/EHsc",
			{ "/Od", "/MTd", "/openmp"; Config = "*-*-debug-*" },
			{ "/O2", "/Zo", "/MT", "/openmp"; Config = "*-*-release" }
		},
		PROGOPTS = {
			{"/NODEFAULTLIB:LIBCMT"; Config = "*-*-debug-*"}
		}
	}
}

Build {
	Units = "units.lua",
	Configs = {
		Config {
			Name = "win64-msvc",
			DefaultOnHost = "windows",
			Inherit = win64,
			Tools = {{"msvc-vs2015"; TargetArch = "x64"}}
		},
	},
	IdeGenerationHints = {
		Msvc = {
			PlatformMappings = {
				['win64-msvc'] = 'x64',
			},
		},
		MsvcSolutions = {
			['RenderToy.sln'] = {}
		},
	},
}
