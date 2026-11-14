from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeDeps, CMakeToolchain


class SlideioViewerConan(ConanFile):
    name = "slideio-viewer"
    version = "0.3.0"
    settings = "os", "compiler", "build_type", "arch"
    # Pairs with the SLIDEIO_VIEWER_BUILD_BENCHMARKS CMake option; pass
    # -o "&:with_benchmarks=True" with it. Off by default because nothing else
    # uses Google benchmark, and its configure step fails on some older Macs.
    options = {"with_benchmarks": [True, False]}
    default_options = {"with_benchmarks": False}

    def requirements(self):
        self.requires("qt/6.7.3")
        self.requires("nlohmann_json/3.11.3")
        self.requires("spdlog/1.15.0")
        self.requires("catch2/3.14.0")
        if self.options.with_benchmarks:
            self.requires("benchmark/1.9.1")

    def configure(self):
        self.options["qt/*"].shared = True
        self.options["qt/*"].qtshadertools = True
        self.options["qt/*"].opengl = "desktop"
        # Qt's recipe pins brotli/1.1.0 for QtNetwork, which we do not use, and
        # freetype/2.13.2's 2026-10-26 recipe revision requires brotli>=1.1.1,
        # so a resolve without a warm Conan cache fails with a version conflict.
        self.options["qt/*"].with_brotli = False

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()
