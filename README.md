# exec code samples

This is the sample code repo for tutorial
https://hollykbuck.github.io/exec/

Recommended local developement setup:

```toml
[tasks.install]
run = "conan install . --build=missing -c=\"tools.cmake.cmaketoolchain:generator=Ninja\" -pr default"

[tasks.configure]
run = "cmake --preset conan-release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

[tasks.build]
run = "cmake --build --preset conan-release"
```