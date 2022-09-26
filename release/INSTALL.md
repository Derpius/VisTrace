**With OpenMP (Recommended)**

1. Place `libomp140.x86_64.dll` in your root Garry's Mod directory (`/steamapps/common/GarrysMod` **not** `/steamapps/common/GarrysMod/garrysmod/lua/bin`). You only need to do this once, ignore if updating.  
2. Place `gmcl_VisTrace-vX.X_X.dll` in your Garry's Mod binary module folder (`/steamapps/common/GarrysMod/garrysmod/lua/bin`)

You may already have `libomp140.x86_64.dll` in `C:\Windows\System32`, if so you can skip the first step.

**Without OpenMP (Not Recommended)**

If for some reason the above steps don't work on your machine, you can use the binary without OpenMP enabled. This is not recommended if the above works as you'll lose all multithreading.

1. Place `gmcl_VisTrace-vX.X_X.no-omp.dll` in your Garry's Mod binary module folder (`/steamapps/common/GarrysMod/garrysmod/lua/bin`)
2. Rename `gmcl_VisTrace-vX.X_X.no-omp.dll` to `gmcl_VisTrace-vX.X_X.dll`
