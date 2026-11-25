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
        # slideio-processor-data (extern/slideio-processor, added with
        # add_subdirectory) finds these itself. rapidjson, libtiff and zlib are
        # the versions extern/slideio-processor/conanfile.py pins. sqlite3 is not:
        # the processor pins 3.44.2, but Qt's recipe requires sqlite3/[>=3.45.0 <4]
        # for QtSql, so 3.44.2 is a version conflict, and overriding Qt down to it
        # would rebuild Qt from source against a version its recipe rejects.
        # 3.54.1 is inside Qt's range and is the version the cached Qt binary was
        # built with; the processor's store needs only its R*Tree module.
        self.requires("sqlite3/3.54.1")
        self.requires("rapidjson/cci.20250205")
        self.requires("libtiff/4.7.1")
        self.requires("zlib/1.3.2")
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
        # slideio-processor's store: an R*Tree spatial index, and rasters written
        # with None, LZW or Deflate only, so libtiff needs zlib and no other codec.
        # The same options as extern/slideio-processor/conanfile.py.
        self.options["sqlite3/*"].enable_rtree = True
        self.options["libtiff/*"].zlib = True
        self.options["libtiff/*"].jpeg = False
        for name in ["jbig", "lzma", "zstd", "webp", "libdeflate", "cxx"]:
            setattr(self.options["libtiff/*"], name, False)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()
