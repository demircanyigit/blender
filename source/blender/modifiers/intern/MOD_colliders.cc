/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 */

#include <string>

#include "BLI_index_mask.hh"
#include "BLI_listbase.h"
#include "BLI_math_matrix.hh"
#include "BLI_vector.hh"

#include "BLT_translation.hh"

#include "DNA_collection_types.h"
#include "DNA_layer_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"

#include "BKE_collection.hh"
#include "BKE_geometry_set.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_wrapper.hh"
#include "BKE_modifier.hh"

#include "BLO_read_write.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "GEO_join_geometries.hh"

#include "RNA_access.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "MEM_guardedalloc.h"

#include "MOD_colliders.hh"

namespace blender {

VectorSet<Object *> MOD_collider_objects(Object *single,
                                         const ListBaseT<ModifierColliderSource> &sources,
                                         Object *owner)
{
  VectorSet<Object *> objects;
  Object *owner_orig = owner ? DEG_get_original(owner) : nullptr;
  auto add = [&](Object *object) {
    if (object == nullptr) {
      return;
    }
    object = DEG_get_original(object);
    if (object != owner_orig && object->type == OB_MESH) {
      objects.add(object);
    }
  };
  add(single);
  for (const ModifierColliderSource &source : sources) {
    if (!(source.flag & MOD_COLLIDER_SOURCE_ENABLED)) {
      continue;
    }
    if (source.source_type == MOD_COLLIDER_SOURCE_OBJECT) {
      add(source.object);
    }
    else if (source.collection != nullptr) {
      Collection *collection = DEG_get_original(source.collection);
      FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (collection, object) {
        add(object);
      }
      FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
    }
  }
  return objects;
}

void MOD_colliders_update_depsgraph(Object *single,
                                    const ListBaseT<ModifierColliderSource> &sources,
                                    const ModifierUpdateDepsgraphContext *ctx,
                                    const char *description)
{
  for (Object *object : MOD_collider_objects(single, sources, ctx->object)) {
    DEG_add_object_relation(ctx->node, object, DEG_OB_COMP_TRANSFORM, description);
    DEG_add_object_relation(ctx->node, object, DEG_OB_COMP_GEOMETRY, description);
  }
  /* Objects added to or taken out of a collection rebuild the relations. Depending on the
   * collection's geometry as a whole would make a cycle when the collection holds the owner. */
}

Mesh *MOD_colliders_mesh(Object *single,
                         const ListBaseT<ModifierColliderSource> &sources,
                         const ModifierEvalContext *ctx)
{
  Vector<bke::GeometrySet> pieces;
  for (Object *object : MOD_collider_objects(single, sources, ctx->object)) {
    Object *evaluated = DEG_get_evaluated(ctx->depsgraph, object);
    if (evaluated == nullptr) {
      continue;
    }
    Mesh *mesh = BKE_modifier_get_evaluated_mesh_from_evaluated_object(evaluated);
    if (mesh == nullptr || mesh->faces_num == 0) {
      continue;
    }
    BKE_mesh_wrapper_ensure_mdata(mesh);
    Mesh *copy = BKE_mesh_copy_for_eval(*mesh);
    bke::mesh_transform(*copy, evaluated->object_to_world(), false);
    if (math::is_negative(evaluated->object_to_world())) {
      bke::mesh_flip_faces(*copy, IndexMask(copy->faces_num));
    }
    pieces.append(bke::GeometrySet::from_mesh(copy));
  }
  if (pieces.is_empty()) {
    return nullptr;
  }
  if (pieces.size() == 1) {
    return pieces.first().get_component_for_write<bke::MeshComponent>().release();
  }
  bke::GeometrySet joined = geometry::join_geometries(pieces, {});
  if (!joined.has_mesh()) {
    return nullptr;
  }
  return joined.get_component_for_write<bke::MeshComponent>().release();
}

void MOD_collider_sources_foreach_ID(ListBaseT<ModifierColliderSource> &sources,
                                     Object *owner,
                                     IDWalkFunc walk,
                                     void *user_data)
{
  for (ModifierColliderSource &source : sources) {
    walk(user_data, owner, reinterpret_cast<ID **>(&source.object), IDWALK_CB_NOP);
    walk(user_data, owner, reinterpret_cast<ID **>(&source.collection), IDWALK_CB_USER);
  }
}

void MOD_collider_sources_copy(ListBaseT<ModifierColliderSource> &dst,
                               const ListBaseT<ModifierColliderSource> &src)
{
  BLI_duplicatelist(&dst, &src);
}

void MOD_collider_sources_free(ListBaseT<ModifierColliderSource> &sources)
{
  BLI_freelistN(&sources);
}

void MOD_collider_sources_blend_write(BlendWriter *writer,
                                      const ListBaseT<ModifierColliderSource> &sources)
{
  writer->write_struct_list(&sources);
}

void MOD_collider_sources_blend_read(BlendDataReader *reader,
                                     ListBaseT<ModifierColliderSource> &sources)
{
  BLO_read_struct_list(reader, ModifierColliderSource, &sources);
}

void MOD_collider_sources_draw(ui::Layout &layout, PointerRNA *ptr)
{
  const std::string name = RNA_string_get(ptr, "name");
  PropertyRNA *list = RNA_struct_find_property(ptr, "colliders");
  const int count = RNA_property_collection_length(ptr, list);
  for (int i = 0; i < count; i++) {
    PointerRNA source;
    RNA_property_collection_lookup_int(ptr, list, i, &source);
    ui::Layout &row = layout.row(true);
    row.use_property_split_set(false);
    row.use_property_decorate_set(false);
    row.prop(&source, "enabled", UI_ITEM_NONE, "", ICON_NONE);
    row.prop(&source, "source_type", UI_ITEM_NONE, "", ICON_NONE);
    ui::Layout &input = row.row(true);
    input.active_set(RNA_boolean_get(&source, "enabled"));
    input.prop(&source,
               RNA_enum_get(&source, "source_type") == MOD_COLLIDER_SOURCE_OBJECT ?
                   "object" :
                   "collection",
               UI_ITEM_NONE,
               "",
               ICON_NONE);
    PointerRNA remove = row.op("OBJECT_OT_collider_source_remove", "", ICON_X);
    RNA_string_set(&remove, "modifier", name.c_str());
    RNA_int_set(&remove, "index", i);
  }
  ui::Layout &add = layout.row(true);
  for (const int type : {MOD_COLLIDER_SOURCE_OBJECT, MOD_COLLIDER_SOURCE_COLLECTION}) {
    PointerRNA op = add.op("OBJECT_OT_collider_source_add",
                           type == MOD_COLLIDER_SOURCE_OBJECT ? IFACE_("Add Object") :
                                                                IFACE_("Add Collection"),
                           ICON_ADD);
    RNA_string_set(&op, "modifier", name.c_str());
    RNA_enum_set(&op, "source_type", type);
  }
  PointerRNA selected = layout.op("OBJECT_OT_collider_source_add_selected",
                                  IFACE_("Add Selected Objects"),
                                  ICON_RESTRICT_SELECT_OFF);
  RNA_string_set(&selected, "modifier", name.c_str());
}

}  // namespace blender
