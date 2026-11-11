/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 */

#pragma once

/* so modifier types match their defines */
#include "MOD_modifiertypes.hh"

#include "BLI_function_ref.hh"

#include "DEG_depsgraph_build.hh"

namespace blender {

struct MDeformVert;
struct Mesh;
struct ModifierData;
struct ModifierEvalContext;
struct Object;

namespace geometry {
class SimCacheHasher;
}

void MOD_init_texture(MappingInfoModifierData *dmd, const ModifierEvalContext *ctx);
/**
 * \param cos: may be null, in which case we use directly mesh vertices' coordinates.
 */
void MOD_get_texture_coords(MappingInfoModifierData *dmd,
                            const ModifierEvalContext *ctx,
                            Object *ob,
                            Mesh *mesh,
                            float (*cos)[3],
                            float (*r_texco)[3]);

void MOD_previous_vcos_store(ModifierData *md, const float (*vert_coords)[3]);

void MOD_get_vgroup(const Object *ob,
                    const Mesh *mesh,
                    const char *name,
                    const MDeformVert **dvert,
                    int *defgrp_index);

void MOD_depsgraph_update_object_bone_relation(DepsNodeHandle *node,
                                               Object *object,
                                               const char *bonename,
                                               const char *description);

/**
 * The result of a slow modifier, from the cache when the mesh, the modifier's settings and what
 * \a extra adds are the same as before. Objects are evaluated again for undo and for changes to
 * other objects, and this keeps that from computing everything again. The modifier's message is
 * kept with the result and shown again.
 *
 * \param compute: The modifier's own #modify_mesh.
 * \param extra: Adds what else the result depends on, like the placement of other objects.
 */
Mesh *MOD_cached_modify_mesh(
    ModifierData *md,
    const ModifierEvalContext *ctx,
    Mesh *mesh,
    FunctionRef<Mesh *(ModifierData *, const ModifierEvalContext *, Mesh *)> compute,
    FunctionRef<void(geometry::SimCacheHasher &)> extra = nullptr);

}  // namespace blender
