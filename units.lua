require "tundra.syntax.glob"
require "tundra.syntax.files"

local glfwDefines = {
	"_GLFW_WIN32", "_GLFW_USE_OPENGL", "_GLFW_WGL"
}

local glfw = StaticLibrary {
	Name = "glfw",
	Includes = { "src/glfw/include" },
	Defines = glfwDefines,
	Sources = {
		Glob {
			Dir = "src/ext/glfw/src",
			Extensions = {".c", ".h"},
			Recursive = false,
		}
	}
}

local imgui = StaticLibrary {
	Name = "imgui",
	Includes = { "src/ext/imgui" },
	Sources = {
		Glob {
			Dir = "src/ext/imgui",
			Extensions = {".cpp", ".h"}
		}
	}
}

local glad = StaticLibrary {
	Name = "glad",
	Includes = { "src/ext/glad/include" },
	Sources = {
		"src/ext/glad/src/glad.c",
		"src/ext/glad/include/glad/glad.h",
	},
}

local tinyexr = StaticLibrary {
	Name = "tinyexr",
	Includes = { "src/ext/tinyexr" },
	Sources = {
		"src/ext/tinyexr/tinyexr.cc"
	},
}

local rendertoy = Program {
	Name = "rendertoy",
	Depends = {
		glfw, imgui, glad, tinyexr
	},
	Defines = glfwDefines,
	Includes = {
		"src/ext/glfw/include",
		"src/ext/imgui",
		"src/ext/glad/include",
		"src/ext/rapidjson/include",
		"src/ext/glm/include",
		"src/ext/tinyexr",
	},
	Sources = {
		Glob { Dir = "src/rendertoy", Extensions = {".cpp", ".h"} }
	},
	Libs = {
		{
			"user32.lib",
			"shell32.lib",
			"comdlg32.lib",
			"gdi32.lib",
			"opengl32.lib",
			Config = {"win*"}
		},
	},
}

Default(rendertoy)
