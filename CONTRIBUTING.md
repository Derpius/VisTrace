# Contributing to VisTrace  

## Issues  
Feel free to open any issues you want, even if they don't conform to the templates.  

## Pull Requests  
When submitting a pull request, please follow the below guidance:  
* Test your code before submitting, unless it is an extremely minor change.  
* Work on the `master` branch, not on release builds  
* Maintain consistent formatting with the rest of the project
  * Indent with tabs
  * Variables should be `camelCase`, functions, classes, and enums `PascalCase`, and constants or macros `UPPER_SNAKE_CASE` (except in the SF API where you should match Starfall's formatting)
  * Do not use the Valve/Facepunch code style, write `(something)` instead of `( something )`

### C++
In addition to the above, the following applies to any changes to the binary module:  
* Keep the structure of any additions in line with existing systems
  * New features exposed to Lua should be implemented as a class/library in `objects`, with the Lua API being defined in `VisTrace.cpp`  
  * New third party libraries should be added as a submodule where possible, and always to the `libs` folder  
  * Exposing a C++ type to VisTrace extensions should be done by adding an interface (abstract class) with *no dependencies* other than system libraries to the `include/vistrace` folder, and adding the metatable ID to the registry at the key `{MetatableName}_id`
* New features should be useful in general to all use cases of VisTrace and fairly small (although can be ray tracing focused as that's the primary use case), anything highly specialised (i.e. advanced sampling strategies) or large (i.e. a bunch of render target postprocessing functions) should be implemented as a VisTrace extension
* Use `#pragma region` to group code in `VisTrace.cpp` and `#pragma once` as an include guard (all modern compilers support it now, regardless of system)
