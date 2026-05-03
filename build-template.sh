conan install . --output-folder=build --build=missing -s build_type=$BUILD_TYPE -s compiler.cppstd=17 -pr:b $CONAN_PROFIlE -pr:h $CONAN_PROFIlE
cmake -S . -B build/build -G "Visual Studio 17 2022" \\n  -DCMAKE_TOOLCHAIN_FILE=build/build/generators/conan_toolchain.cmake \\n  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \\n  -DSLIDEIO_ROOT="d:\Projects\slideio\slideio\build\install"
cmake --build build/build --config $BUILD_TYPE
cmake --install build/build --config $BUILD_TYPE --prefix "d:/Projects/slideio/slideio-view/build/install"
