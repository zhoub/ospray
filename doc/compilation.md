Standard CMake Build
--------------------

### Compiling OSPRay on Linux and Mac OS\ X

Assuming the above requisites are all fulfilled, building OSPRay through
CMake is easy:

-   Create a build directory, and go into it

        mkdir ospray/build
        cd ospray/build

    (We do recommend having separate build directories for different
    configurations such as release, debug, etc.).

-   The compiler CMake will use will default to whatever the `CC` and
    `CXX` environment variables point to. Should you want to specify a
    different compiler, run cmake manually while specifying the desired
    compiler. The default compiler on most linux machines is `gcc`, but
    it can be pointed to `clang` instead by executing the following:

        cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang ..

    CMake will now use Clang instead of GCC. If you are OK with using
    the default compiler on your system, then simply skip this step.
    Note that the compiler variables cannot be changed after the first
    `cmake` or `ccmake` run.

-   Open the CMake configuration dialog

        ccmake ..

-   Make sure to properly set build mode and enable the components you
    need, etc.; then type 'c'onfigure and 'g'enerate. When back on the
    command prompt, build it using

        make

-   You should now have `libospray.[so,dylib]` as well as a set of
    [example applications].

### Entitlements on Mac OS\ X

Mac OS\ X requires notarization of applications as a security mechanism,
and [entitlements must be
declared](https://developer.apple.com/documentation/bundleresources/entitlements)
during the notarization process. OSPRay's `denoiser` uses OIDN, which
uses just-in-time compilation through
[oneDNN](https://github.com/oneapi-src/oneDNN) and thus requires the
following entitlement:

-    [`com.apple.security.cs.allow-jit`](https://developer.apple.com/documentation/bundleresources/entitlements/com_apple_security_cs_allow-jit)


### Compiling OSPRay on Windows

On Windows using the CMake GUI (`cmake-gui.exe`) is the most convenient
way to configure OSPRay and to create the Visual Studio solution files:

-   Browse to the OSPRay sources and specify a build directory (if it
    does not exist yet CMake will create it).

-   Click "Configure" and select as generator the Visual Studio version
    you have; OSPRay needs "Visual Studio 15 2017 Win64" or newer, 32\
    bit builds are not supported, e.g., "Visual Studio 17 2022".

-   If the configuration fails because some dependencies could not be
    found then follow the instructions given in the error message,
    e.g., set the variable `embree_DIR` to the folder where Embree was
    installed and `openvkl_DIR` to where Open VKL was installed.

-   Optionally change the default build options, and then click
    "Generate" to create the solution and project files in the build
    directory.

-   Open the generated `OSPRay.sln` in Visual Studio, select the build
    configuration and compile the project.


Alternatively, OSPRay can also be built without any GUI, entirely on the
console. In the Visual Studio command prompt type:

    cd path\to\ospray
    mkdir build
    cd build
    cmake -G "Visual Studio 17 2022" [-D VARIABLE=value] ..
    cmake --build . --config Release

Use `-D` to set variables for CMake, e.g., the path to Embree with "`-D
embree_DIR=\path\to\embree`".

You can also build only some projects with the `--target` switch.
Additional parameters after "`--`" will be passed to `msbuild`. For
example, to build in parallel only the OSPRay library without the
example applications use

    cmake --build . --config Release --target ospray -- /m


Finding an OSPRay Install with CMake
------------------------------------

Client applications using OSPRay can find it with CMake's
`find_package()` command. For example,

    find_package(ospray 3.0.0 REQUIRED)

finds OSPRay via OSPRay's configuration file `osprayConfig.cmake`^[This
file is usually in
`${install_location}/[lib|lib64]/cmake/ospray-${version}/`. If CMake
does not find it automatically, then specify its location in variable
`ospray_DIR` (either an environment variable or CMake variable).]. Once
found, the following is all that is required to use OSPRay:

    target_link_libraries(${client_target} ospray::ospray)

This will automatically propagate all required include paths, linked
libraries, and compiler definitions to the client CMake target
(either an executable or library).

Advanced users may want to link to additional targets which are exported
in OSPRay's CMake config, which includes all installed modules. All
targets built with OSPRay are exported in the `ospray::` namespace,
therefore all targets locally used in the OSPRay source tree can be
accessed from an install. For example, `ospray_module_cpu` can be
consumed directly via the `ospray::ospray_module_cpu` target. All
targets have their libraries, includes, and definitions attached to them
for public consumption (please [report bugs] if this is broken!).
