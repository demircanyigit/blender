# Building on Windows

This is a normal Blender build. Blender's
[Windows build instructions](https://developer.blender.org/docs/handbook/building_blender/windows/)
are the main reference if something here doesn't match your setup.

This fork has been built with MSVC 14.44 (Visual Studio 2022 build tools), CMake and Ninja on
Windows 10 x64.

## What you need

- Visual Studio 2022 or its build tools, with the **Desktop development with C++** workload
- Git and Git LFS
- CMake and Ninja, which come with that workload if you add the CMake tools
- Roughly 40 GB of free disk space, including Blender's precompiled libraries
- At least 16 GB of RAM. With 16 GB, limit Ninja to about 6 jobs

## Steps

Run these in an **x64 Native Tools Command Prompt for VS 2022**.

Get the source:

```bat
git clone --branch blender-5.1-custom https://github.com/demircanyigit/blender.git
cd blender
```

Get the precompiled libraries for Windows. They're on Blender's server and large:

```bat
python build_files\utils\make_update.py --no-blender
```

This step hasn't been tried from a fresh clone of this fork. If it fails, see Blender's instructions.

Configure and build, with the build folder next to the source folder:

```bat
cmake -S . -B ..\build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C ..\build -j 6 install
```

The result is `..\build\bin\blender.exe`. The first build can take an hour or more. Later ones only
rebuild what changed.

Open that exe directly. Shortcuts from an official install keep opening the official Blender.

## Check that it worked

```bat
..\build\bin\blender.exe -b --factory-startup --python-expr "import bpy; print('OK' if hasattr(bpy.types, 'GeometryNodeBevelMesh') else 'MISSING')"
```

This should print `OK`.

## Problems

| Symptom | Fix |
|---|---|
| `LNK1104: cannot open file 'bin\blender.exe'` | Blender is still open. Close it and run the build again |
| The compiler isn't found | Use the Native Tools Command Prompt, not a plain one |
| Git LFS or library errors | Run `git lfs install` once, then repeat the library step |
| Running out of memory | Lower the `-j` number |
| Running out of space on C: | Set `TEMP` and `TMP` to a folder on a drive with more room before building |
