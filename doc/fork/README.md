# About this fork

An unofficial fork based on the Blender `v5.1.0` tag. It adds six geometry nodes and more color
attribute options for the Solid shading mode. It isn't made or supported by the Blender Foundation.

- [NODES.md](NODES.md): what each node does, its inputs and its limits
- [VIEWPORT_COLORS.md](VIEWPORT_COLORS.md): the material options
- [BUILD_WINDOWS.md](BUILD_WINDOWS.md): how to build it

There are no ready-made builds, so you need to compile it yourself.

## Using files in official Blender

Files made with this fork are meant for this fork.

- Node trees that use the new nodes won't work in official Blender, because it doesn't have them.
  Apply the modifier or bake the result first if a file has to open there.
- Isotropic Remesh and Decimate Mesh use node ids (2158 and 2159) that official Blender could
  assign to its own nodes later.
- The material color options are ignored by official Blender. They are lost if that Blender saves
  the file.
- Official files open normally in this fork, with the color options turned off.

## Testing

The nodes were tested with Python scripts that run Blender in the background, on simple meshes and
curves and on Suzanne with color attributes:

- Bevel gives the same result as the Bevel modifier across its settings.
- Sweep gives the same result as Curve to Mesh where the two overlap.
- Bevel with a random Amount ran through 1,280 combinations of settings without crashing.

This is not the same as long use on production scenes, so expect rough edges. Those scripts aren't
in this repository yet.

`source/blender/geometry/tests/GEO_isotropic_remesh_test.cc` comes from the first version of the
Isotropic Remesh node. It hasn't been built or run since the node moved to the PMP library.

Only Windows x64 has been built from this branch.

## Licenses

Blender as a whole is licensed under the GNU GPL version 3, and so is anything you build from this
fork. If you share a build, you also have to share its source code.

`extern/pmp` is the [Polygon Mesh Processing Library](https://github.com/pmp-library/pmp-library)
3.0.0 under an MIT license with an employer disclaimer, which is in `extern/pmp/LICENSE.txt`. Its
local changes are listed in `extern/pmp/README.blender`.

The GPL doesn't cover the Blender name or logo. See the
[Blender logo and trademark rules](https://www.blender.org/about/logo/). If you publish builds,
don't present them as official Blender and don't use the logo as your own.

## Credits

Blender is made by the [Blender Foundation and its contributors](https://www.blender.org/).

The first version of the Isotropic Remesh node was written by Usta
([ustacode/blender](https://github.com/ustacode/blender)). The commits keep that authorship, and
the original write-up is in `doc/isotropic_remesh/`. That version used
[huxingyi/isotropicremesher](https://github.com/huxingyi/isotropicremesher) before the node moved to
PMP.

## Updating to a new Blender version

1. Fetch the new release tag from `https://projects.blender.org/blender/blender.git`.
2. Rebase or merge this branch onto it.
3. Conflicts are most likely in the Workbench engine and shaders (`source/blender/draw`) and in
   `DNA_material_types.h`.
4. Check `versioning_510.cc`. The material defaults there depend on whether the file has the new
   members, not on the file's version, so they have to keep running on new versions too.
5. Compare the node ids in `BKE_node_legacy_types.hh` with any that upstream has added.

## Contributing to Blender

Blender takes contributions on [projects.blender.org](https://projects.blender.org), not GitHub.
Nothing from this fork has been proposed there.
