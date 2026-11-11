/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * The colliders of Drape and Collapse: one object, plus a list of more objects and collections.
 */

#pragma once

#include "BLI_vector_set.hh"

#include "DNA_listBase.h"

#include "BKE_lib_query.hh"

namespace blender {

struct BlendDataReader;
struct BlendWriter;
struct Mesh;
struct ModifierColliderSource;
struct ModifierEvalContext;
struct ModifierUpdateDepsgraphContext;
struct Object;
struct PointerRNA;
namespace ui {
struct Layout;
}

/** The original mesh objects: \a single and every enabled source, without \a owner. */
VectorSet<Object *> MOD_collider_objects(Object *single,
                                         const ListBaseT<ModifierColliderSource> &sources,
                                         Object *owner);

/** Depend on the shape and place of every collider object. */
void MOD_colliders_update_depsgraph(Object *single,
                                    const ListBaseT<ModifierColliderSource> &sources,
                                    const ModifierUpdateDepsgraphContext *ctx,
                                    const char *description);

/** All the colliders joined in one mesh, in world space. Null when there are none. */
Mesh *MOD_colliders_mesh(Object *single,
                         const ListBaseT<ModifierColliderSource> &sources,
                         const ModifierEvalContext *ctx);

void MOD_collider_sources_foreach_ID(ListBaseT<ModifierColliderSource> &sources,
                                     Object *owner,
                                     IDWalkFunc walk,
                                     void *user_data);
void MOD_collider_sources_copy(ListBaseT<ModifierColliderSource> &dst,
                               const ListBaseT<ModifierColliderSource> &src);
void MOD_collider_sources_free(ListBaseT<ModifierColliderSource> &sources);
void MOD_collider_sources_blend_write(BlendWriter *writer,
                                      const ListBaseT<ModifierColliderSource> &sources);
void MOD_collider_sources_blend_read(BlendDataReader *reader,
                                     ListBaseT<ModifierColliderSource> &sources);

/** The list in a modifier panel, with buttons to add objects, collections and the selection. */
void MOD_collider_sources_draw(ui::Layout &layout, PointerRNA *ptr);

}  // namespace blender
