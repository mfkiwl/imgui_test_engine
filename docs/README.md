## Contents

- `build_scripts/` helpers scripts to build with various compilers and create packages.
- `libs/` libs for sandbox and test_engine
- `sandbox/` dummy editor, experiments, showcase of third party extensions
- `shared/` shared helpers
- `test_app/` test suite (app)
- `test_engine/` test engine (library)


## build_scripts/

Note: those are merely helpers to build with variety of compilers under Windows. For day to day operation, use the provided Solution file.
For local testing with Clang, MinGW etc: copy [build_scripts/config/default_build_config.bat](https://github.com/ocornut/imgui_dev/blob/main/build_scripts/config/default_build_config.bat) to `build_config.bat` and configure your paths.