from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeDeps, CMakeToolchain


class SlideioViewerConan(ConanFile):
    name = "slideio-viewer"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("qt/6.7.3")
        self.requires("nlohmann_json/3.11.3")
        self.requires("spdlog/1.15.0")
        self.requires("catch2/3.14.0")
        self.requires("benchmark/1.9.1")

    def configure(self):
        self.options["qt/*"].shared = True
        self.options["qt/*"].qtshadertools = True
        self.options["qt/*"].opengl = "desktop"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()
