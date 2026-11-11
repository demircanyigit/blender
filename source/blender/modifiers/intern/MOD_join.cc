/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_attribute.hh"
#include "BKE_collection.hh"
#include "BKE_customdata.hh"
#include "BKE_geometry_set.hh"
#include "BKE_lib_query.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_wrapper.hh"
#include "BKE_modifier.hh"
#include "BLI_index_mask.hh"
#include "BLI_listbase.h"
#include "BLI_math_matrix.hh"
#include "BLI_set.hh"
#include "BLI_utildefines.h"
#include "BLI_vector_set.hh"
#include "BLO_read_write.hh"
#include "BLT_translation.hh"
#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"
#include "DNA_collection_types.h"
#include "DNA_layer_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "GEO_join_geometries.hh"
#include "MEM_guardedalloc.h"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"
#include "RNA_access.hh"
#include "RNA_prototypes.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"
#include <cmath>
#include <exception>

namespace blender {
namespace {
VectorSet<Object *> source_objects(const JoinModifierData &data, Object *owner)
{
  VectorSet<Object *> objects;
  auto add = [&](Object *object) {
    if (!object)
      return;
    object = DEG_get_original(object);
    if (object != DEG_get_original(owner) && object->type == OB_MESH)
      objects.add(object);
  };
  for (const JoinModifierSource &source : data.sources) {
    if (!(source.flag & MOD_JOIN_SOURCE_ENABLED))
      continue;
    if (source.source_type == MOD_JOIN_SOURCE_OBJECT)
      add(source.object);
    else if (source.source_type == MOD_JOIN_SOURCE_COLLECTION && source.collection) {
      Collection *collection = DEG_get_original(source.collection);
      if (data.flag & MOD_JOIN_CHILD_COLLECTIONS) {
        FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (collection, object) {
          add(object);
        }
        FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
      }
      else {
        for (CollectionObject &entry : collection->gobject)
          add(entry.ob);
      }
    }
  }
  return objects;
}
/* Reject direct and indirect Join chains before adding relations or reading evaluation results.
 * Other kinds of dependency loops remain subject to Blender's normal cycle detection. */
bool join_reaches(Object *from, Object *target, Set<Object *> &visited, const int depth = 0)
{
  if (from == target || depth >= 64)
    return true;
  if (!visited.add(from))
    return false;
  for (ModifierData &modifier : from->modifiers) {
    if (modifier.type != eModifierType_Join ||
        !(modifier.mode & (eModifierMode_Realtime | eModifierMode_Render)))
      continue;
    for (Object *next : source_objects(*reinterpret_cast<JoinModifierData *>(&modifier), from)) {
      if (join_reaches(next, target, visited, depth + 1))
        return true;
    }
  }
  return false;
}
/** The object at the top of the parent chain. */
Object *top_parent(Object *object)
{
  object = DEG_get_original(object);
  for (int depth = 0; object->parent != nullptr && depth < 1024; depth++) {
    object = object->parent;
  }
  return object;
}

/** Give every face of the mesh the same number in a face attribute. */
void write_piece_id(Mesh &mesh, const StringRef name, const int id)
{
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  attributes.remove(name);
  attributes.add<int>(name,
                      bke::AttrDomain::Face,
                      bke::AttributeInitVArray(VArray<int>::from_single(id, mesh.faces_num)));
}
}  // namespace

static void init_data(ModifierData *md)
{
  auto *data = reinterpret_cast<JoinModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}
static void free_data(ModifierData *md)
{
  auto &data = *reinterpret_cast<JoinModifierData *>(md);
  BLI_freelistN(&data.sources);
}
static void copy_data(const ModifierData *md, ModifierData *target, const int flag)
{
  BKE_modifier_copydata_generic(md, target, flag);
  const auto &source = *reinterpret_cast<const JoinModifierData *>(md);
  auto &destination = *reinterpret_cast<JoinModifierData *>(target);
  BLI_duplicatelist(&destination.sources, &source.sources);
}
static void foreach_ID_link(ModifierData *md, Object *owner, IDWalkFunc walk, void *user_data)
{
  auto &data = *reinterpret_cast<JoinModifierData *>(md);
  for (JoinModifierSource &source : data.sources) {
    walk(user_data, owner, reinterpret_cast<ID **>(&source.object), IDWALK_CB_NOP);
    walk(user_data, owner, reinterpret_cast<ID **>(&source.collection), IDWALK_CB_USER);
  }
}
static void required_data_mask(ModifierData * /*md*/, CustomData_MeshMasks *mask)
{
  mask->vmask |= CD_MASK_MESH.vmask;
  mask->emask |= CD_MASK_MESH.emask;
  mask->fmask |= CD_MASK_MESH.fmask;
  mask->pmask |= CD_MASK_MESH.pmask;
  mask->lmask |= CD_MASK_MESH.lmask;
}
static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  auto &data = *reinterpret_cast<JoinModifierData *>(md);
  Object *owner = DEG_get_original(ctx->object);
  for (Object *object : source_objects(data, owner)) {
    Set<Object *> visited;
    if (join_reaches(object, owner, visited))
      continue;
    DEG_add_object_relation(ctx->node, object, DEG_OB_COMP_GEOMETRY, "Join source geometry");
    DEG_add_object_relation(ctx->node, object, DEG_OB_COMP_TRANSFORM, "Join source transform");
    DEG_add_customdata_mask(ctx->node, object, &CD_MASK_MESH);
  }
  /* Collection membership changes rebuild relations. Depend on each expanded source object,
   * not collection geometry as a whole: the latter would create a cycle if it contains owner. */
  DEG_add_depends_on_transform_relation(ctx->node, "Join receiver transform");
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const auto &data = *reinterpret_cast<JoinModifierData *>(md);
  const bool include_self = data.flag & MOD_JOIN_INCLUDE_SELF;
  Object *owner = DEG_get_original(ctx->object);
  const VectorSet<Object *> objects = source_objects(data, owner);
  if (objects.is_empty())
    return include_self ? mesh : BKE_mesh_new_nomain(0, 0, 0, 0);
  if (objects.size() > 4096) {
    BKE_modifier_set_warning(ctx->object, md, "Join exceeds the limit of 4096 source objects");
    return mesh;
  }
  bool invertible;
  const float4x4 world_to_receiver = math::invert(ctx->object->object_to_world(), invertible);
  if (!invertible) {
    BKE_modifier_set_warning(
        ctx->object, md, "Receiving object has a singular transform; restore non-zero scale");
    return mesh;
  }
  int64_t vertices = 0, edges = 0, faces = 0, corners = 0;
  Vector<bke::GeometrySet> pieces;
  int skipped = 0;
  /* Object 0 is the receiver itself, the sources follow in order. Parents are numbered in the
   * order they are first seen. */
  const bool piece_ids = data.flag & MOD_JOIN_PIECE_IDS;
  VectorSet<Object *> parents;
  auto append = [&](Object &object,
                    Mesh &input,
                    const float4x4 &transform,
                    const int object_id) -> bool {
    vertices += input.verts_num;
    edges += input.edges_num;
    faces += input.faces_num;
    corners += input.corners_num;
    if (vertices > 10000000 || edges > 20000000 || faces > 10000000 || corners > 40000000)
      return false;
    for (const float3 &point : input.vert_positions()) {
      if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        return false;
    }
    for (int column = 0; column < 4; column++) {
      for (int row = 0; row < 4; row++) {
        if (!std::isfinite(transform[column][row]))
          return false;
      }
    }
    Mesh *copy = BKE_mesh_copy_for_eval(input);
    bke::GeometrySet piece = bke::GeometrySet::from_mesh(copy);
    if (piece_ids) {
      write_piece_id(*copy, "object_id", object_id);
      write_piece_id(*copy, "parent_id", int(parents.index_of_or_add(top_parent(&object))));
    }
    /* Resolve object-linked material overrides before material slots are merged by identity. */
    for (int material = 0; material < copy->totcol; material++) {
      copy->mat[material] = const_cast<Material *>(
          BKE_object_material_get_eval(object, input.id, material + 1));
    }
    bke::mesh_transform(*copy, transform, false);
    for (const float3 &point : copy->vert_positions()) {
      if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        return false;
    }
    if (math::is_negative(transform))
      bke::mesh_flip_faces(*copy, IndexMask(copy->faces_num));
    pieces.append(std::move(piece));
    return true;
  };
  try {
    bool valid = true;
    if (include_self)
      valid = append(*ctx->object, *mesh, float4x4::identity(), 0);
    for (const int index : objects.index_range()) {
      Object *object = objects[index];
      if (!valid)
        break;
      Set<Object *> visited;
      if (join_reaches(object, owner, visited)) {
        skipped++;
        continue;
      }
      Object *evaluated = DEG_get_evaluated(ctx->depsgraph, object);
      Mesh *source = evaluated ? BKE_modifier_get_evaluated_mesh_from_evaluated_object(evaluated) :
                                 nullptr;
      if (!source) {
        skipped++;
        continue;
      }
      BKE_mesh_wrapper_ensure_mdata(source);
      valid = append(
          *evaluated, *source, world_to_receiver * evaluated->object_to_world(), index + 1);
    }
    if (!valid) {
      BKE_modifier_set_warning(ctx->object,
                               md,
                               "Join exceeds its geometry budget or contains invalid "
                               "coordinates/transforms; input retained");
      return mesh;
    }
    if (skipped)
      BKE_modifier_set_warning(
          ctx->object, md, "%d unavailable or cyclic Join sources were skipped", skipped);
    if (pieces.is_empty())
      return BKE_mesh_new_nomain(0, 0, 0, 0);
    bke::GeometrySet joined = geometry::join_geometries(pieces, {});
    if (!joined.has_mesh())
      return BKE_mesh_new_nomain(0, 0, 0, 0);
    Mesh *result = joined.get_component_for_write<bke::MeshComponent>().release();
    BKE_mesh_copy_parameters(result, mesh);
    return result;
  }
  catch (const std::exception &error) {
    BKE_modifier_set_warning(ctx->object, md, "Join: %s", error.what());
    return mesh;
  }
}
static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, nullptr);
  ui::Layout &layout = *panel->layout;
  layout.use_property_split_set(true);
  layout.prop(ptr, "use_original", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_child_collections", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_piece_ids", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  const std::string name = RNA_string_get(ptr, "name");
  const int count = RNA_collection_length(ptr, "sources");
  for (int i = 0; i < count; i++) {
    PointerRNA source;
    RNA_property_collection_lookup_int(ptr, RNA_struct_find_property(ptr, "sources"), i, &source);
    ui::Layout &row = layout.row(true);
    row.use_property_split_set(false);
    row.use_property_decorate_set(false);
    row.prop(&source, "enabled", UI_ITEM_NONE, "", ICON_NONE);
    row.prop(&source, "source_type", UI_ITEM_NONE, "", ICON_NONE);
    ui::Layout &input = row.row(true);
    input.active_set(RNA_boolean_get(&source, "enabled"));
    input.prop(&source,
               RNA_enum_get(&source, "source_type") == MOD_JOIN_SOURCE_OBJECT ? "object" :
                                                                                "collection",
               UI_ITEM_NONE,
               "",
               ICON_NONE);
    PointerRNA remove = row.op("OBJECT_OT_join_modifier_source_remove", "", ICON_X);
    RNA_string_set(&remove, "modifier", name.c_str());
    RNA_int_set(&remove, "index", i);
  }
  ui::Layout &add = layout.row(true);
  for (const char *type : {"OBJECT", "COLLECTION"}) {
    PointerRNA op = add.op("OBJECT_OT_join_modifier_source_add",
                           type[0] == 'O' ? IFACE_("Add Object") : IFACE_("Add Collection"),
                           ICON_ADD);
    RNA_string_set(&op, "modifier", name.c_str());
    RNA_enum_set(&op, "source_type", type[0] == 'O' ? 0 : 1);
  }
  PointerRNA selected = layout.op("OBJECT_OT_join_modifier_source_add_selected",
                                  IFACE_("Add Selected Objects"),
                                  ICON_RESTRICT_SELECT_OFF);
  RNA_string_set(&selected, "modifier", name.c_str());
  modifier_error_message_draw(layout, ptr);
}
static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Join, panel_draw);
}
static void blend_write(BlendWriter *writer, const ID * /*owner*/, const ModifierData *md)
{
  const auto *data = reinterpret_cast<const JoinModifierData *>(md);
  writer->write_struct(data);
  writer->write_struct_list(&data->sources);
}
static void blend_read(BlendDataReader *reader, ModifierData *md)
{
  auto *data = reinterpret_cast<JoinModifierData *>(md);
  BLO_read_struct_list(reader, JoinModifierSource, &data->sources);
}

ModifierTypeInfo modifierType_Join = {
    /*idname*/ "Join",
    /*name*/ N_("Join"),
    /*struct_name*/ "JoinModifierData",
    /*struct_size*/ sizeof(JoinModifierData),
    /*srna*/ &RNA_JoinModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode,
    /*icon*/ ICON_MOD_JOIN,

    /*copy_data*/ copy_data,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ required_data_mask,
    /*free_data*/ free_data,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ blend_write,
    /*blend_read*/ blend_read,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
