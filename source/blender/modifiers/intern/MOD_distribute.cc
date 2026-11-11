/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Distribute: objects from a collection packed on the faces by their measured sides.
 */

#include "BLI_math_matrix.hh"
#include "BLI_color.hh"
#include "BLI_noise.hh"
#include "BLI_string.h"
#include "BLI_vector.hh"

#include "BLT_translation.hh"

#include "DNA_collection_types.h"
#include "DNA_layer_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_attribute.h"
#include "BKE_collection.hh"
#include "BKE_geometry_set.hh"
#include "BKE_geometry_set_instances.hh"
#include "BKE_instances.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "GEO_mesh_distribute.hh"
#include "GEO_realize_instances.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static void init_data(ModifierData *md)
{
  DistributeModifierData *data = reinterpret_cast<DistributeModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  DistributeModifierData *dmd = reinterpret_cast<DistributeModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&dmd->collection), IDWALK_CB_USER);
  walk(user_data, ob, reinterpret_cast<ID **>(&dmd->fillers), IDWALK_CB_USER);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  DistributeModifierData *dmd = reinterpret_cast<DistributeModifierData *>(md);
  if (dmd->collection != nullptr) {
    DEG_add_collection_geometry_relation(ctx->node, dmd->collection, "Distribute Modifier");
  }
  if (dmd->fillers != nullptr) {
    DEG_add_collection_geometry_relation(ctx->node, dmd->fillers, "Distribute Modifier");
  }
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const DistributeModifierData *dmd = reinterpret_cast<const DistributeModifierData *>(md);
  return dmd->collection == nullptr;
}

/** Every object in a collection, as an item with its turned and scaled bounding box. */
static void gather_items(Collection *collection,
                         const Object *self,
                         const bool filler,
                         Vector<geometry::DistributeItem> &r_items,
                         Vector<Object *> &r_objects,
                         Vector<float3x3> &r_shapes)
{
  if (collection == nullptr) {
    return;
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (collection, ob) {
    if (ob == self) {
      continue;
    }
    bke::GeometrySet set = bke::object_get_evaluated_geometry_set(*ob);
    if (set.has_instances()) {
      set = geometry::realize_instances(set, geometry::RealizeInstancesOptions()).geometry;
    }
    const std::optional<Bounds<float3>> bounds = set.compute_boundbox_without_instances();
    if (!bounds) {
      continue;
    }
    /* Turned and scaled as the object is, but not moved. */
    const float3x3 shape(ob->object_to_world());
    float3 lo(1e30f), hi(-1e30f);
    for (int i = 0; i < 8; i++) {
      const float3 corner((i & 1) ? bounds->max.x : bounds->min.x,
                          (i & 2) ? bounds->max.y : bounds->min.y,
                          (i & 4) ? bounds->max.z : bounds->min.z);
      const float3 p = shape * corner;
      lo = math::min(lo, p);
      hi = math::max(hi, p);
    }
    geometry::DistributeItem item;
    item.min = lo;
    item.max = hi;
    item.filler = filler;
    r_items.append(item);
    r_objects.append(ob);
    r_shapes.append(shape);
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
}

static void store_count(ModifierData *md, const ModifierEvalContext *ctx, const int count)
{
  if (!DEG_is_active(ctx->depsgraph)) {
    return;
  }
  DistributeModifierData *orig = reinterpret_cast<DistributeModifierData *>(
      BKE_modifier_get_original(ctx->object, md));
  if (orig != nullptr) {
    orig->count_num = count;
  }
}

static void modify_geometry_set(ModifierData *md,
                                const ModifierEvalContext *ctx,
                                bke::GeometrySet *geometry_set)
{
  const DistributeModifierData &dmd = *reinterpret_cast<DistributeModifierData *>(md);
  const Mesh *surface = geometry_set->get_mesh();
  if (surface == nullptr) {
    return;
  }

  Vector<geometry::DistributeItem> items;
  Vector<Object *> objects;
  Vector<float3x3> shapes;
  gather_items(dmd.collection, ctx->object, false, items, objects, shapes);
  gather_items(dmd.fillers, ctx->object, true, items, objects, shapes);

  /* Faces whose points are all in the vertex group. */
  Array<bool> selection;
  if (dmd.vertex_group[0] != '\0') {
    const bke::AttributeAccessor attributes = surface->attributes();
    const VArraySpan<float> weights = *attributes.lookup_or_default<float>(
        dmd.vertex_group, bke::AttrDomain::Point, 0.0f);
    const bool invert = dmd.flag & MOD_DISTRIBUTE_INVERT_VGROUP;
    const OffsetIndices<int> faces = surface->faces();
    const Span<int> corner_verts = surface->corner_verts();
    selection.reinitialize(surface->faces_num);
    for (const int f : faces.index_range()) {
      bool in = true;
      for (const int v : corner_verts.slice(faces[f])) {
        if ((weights[v] > 0.0f) == invert) {
          in = false;
          break;
        }
      }
      selection[f] = in;
    }
  }

  geometry::DistributeParams params;
  params.items = items;
  params.selection = selection;
  params.gap = dmd.gap;
  params.tries = dmd.tries;
  params.align_to_normal = dmd.flag & MOD_DISTRIBUTE_ALIGN_NORMAL;
  params.direction = geometry::DistributeDirection(dmd.direction);
  params.rotation = geometry::DistributeRotation(dmd.rotation);
  params.jitter = dmd.jitter;
  params.scale_min = dmd.scale_min;
  params.scale_max = dmd.scale_max;
  params.offset = dmd.offset;
  params.stay_inside = dmd.flag & MOD_DISTRIBUTE_STAY_INSIDE;
  params.fillers = dmd.flag & MOD_DISTRIBUTE_FILLERS;
  params.filler_scale = dmd.filler_scale;
  params.max_count = dmd.max_count;
  params.seed = dmd.seed;

  Vector<geometry::DistributePlacement> placed;
  if (!items.is_empty()) {
    placed = geometry::distribute_by_size(*surface, params);
  }
  store_count(md, ctx, placed.size());

  std::unique_ptr<bke::Instances> instances = std::make_unique<bke::Instances>(placed.size());
  Array<int> handle_of(objects.size(), -1);
  MutableSpan<int> handles = instances->reference_handles_for_write();
  MutableSpan<float4x4> transforms = instances->transforms_for_write();
  for (const int i : placed.index_range()) {
    const int item = placed[i].item;
    if (handle_of[item] < 0) {
      handle_of[item] = instances->add_reference(bke::InstanceReference(*objects[item]));
    }
    handles[i] = handle_of[item];
    transforms[i] = placed[i].transform * float4x4(shapes[item]);
  }
  bke::MutableAttributeAccessor attributes = instances->attributes_for_write();
  {
    bke::SpanAttributeWriter<int> ids = attributes.lookup_or_add_for_write_only_span<int>(
        "distribute_id", bke::AttrDomain::Instance);
    bke::SpanAttributeWriter<float> rnd = attributes.lookup_or_add_for_write_only_span<float>(
        "distribute_random", bke::AttrDomain::Instance);
    for (const int i : placed.index_range()) {
      ids.span[i] = i;
      rnd.span[i] = float(noise::hash(uint32_t(i), uint32_t(dmd.seed), 77u)) / 4294967295.0f;
    }
    ids.finish();
    rnd.finish();
  }
  if (dmd.flag & MOD_DISTRIBUTE_COLOR) {
    bke::SpanAttributeWriter<ColorGeometry4f> color =
        attributes.lookup_or_add_for_write_only_span<ColorGeometry4f>("random_color",
                                                                      bke::AttrDomain::Instance);
    for (const int i : placed.index_range()) {
      const float4 c = geometry::distribute_random_color(
          i,
          dmd.seed,
          geometry::StoneworkColorMode(dmd.color_mode),
          float2(dmd.hue_min, dmd.hue_max),
          float2(dmd.saturation_min, dmd.saturation_max),
          float2(dmd.value_min, dmd.value_max));
      color.span[i] = ColorGeometry4f(c.x, c.y, c.z, c.w);
    }
    color.finish();
  }

  /* The surface: kept, turned into mortar, or removed. */
  Mesh *keep = nullptr;
  if (dmd.flag & MOD_DISTRIBUTE_MORTAR) {
    geometry::DistributeMortarParams mortar;
    mortar.offset = dmd.mortar_offset;
    mortar.remesh = geometry::StoneworkRemesh(dmd.mortar_remesh);
    mortar.resolution = dmd.mortar_resolution;
    mortar.noise_strength = dmd.noise_strength;
    mortar.noise_scale = dmd.noise_scale;
    mortar.noise_detail = dmd.noise_detail;
    mortar.seed = dmd.seed;
    mortar.material_index = dmd.mortar_material_index;
    mortar.selection = selection;
    std::string message;
    keep = geometry::distribute_mortar(*surface, mortar, message);
    if (!message.empty()) {
      BKE_modifier_set_warning(ctx->object, md, "%s", message.c_str());
    }
  }
  else if (dmd.flag & MOD_DISTRIBUTE_KEEP_SURFACE) {
    keep = BKE_mesh_copy_for_eval(*surface);
  }

  bke::GeometrySet result;
  if (keep) {
    result.replace_mesh(keep);
  }
  result.replace_instances(instances.release());
  if (dmd.flag & MOD_DISTRIBUTE_REALIZE) {
    result = geometry::realize_instances(result, geometry::RealizeInstancesOptions()).geometry;
    if (dmd.flag & MOD_DISTRIBUTE_COLOR) {
      if (Mesh *mesh = result.get_mesh_for_write()) {
        BKE_id_attributes_active_color_set(&mesh->id, "random_color");
        BKE_id_attributes_default_color_set(&mesh->id, "random_color");
      }
    }
  }
  *geometry_set = std::move(result);
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  const DistributeModifierData &dmd = *static_cast<const DistributeModifierData *>(ptr->data);

  layout.use_property_split_set(true);

  layout.prop(ptr, "collection", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "fillers", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  modifier_vgroup_ui(layout, ptr, &ob_ptr, "vertex_group", "invert_vertex_group", std::nullopt);

  ui::Layout &pack = layout.column(true);
  pack.prop(ptr, "gap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  pack.prop(ptr, "tries", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_stay_inside", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_fill_gaps", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &fill = layout.column(true);
  fill.active_set((dmd.flag & MOD_DISTRIBUTE_FILLERS) && dmd.fillers == nullptr);
  fill.prop(ptr, "filler_scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.prop(ptr, "use_align_normal", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "direction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "rotation", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "jitter", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &scale = layout.column(true);
  scale.prop(ptr, "scale_min", UI_ITEM_NONE, IFACE_("Scale Min"), ICON_NONE);
  scale.prop(ptr, "scale_max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);
  layout.prop(ptr, "offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  const bool mortar = dmd.flag & MOD_DISTRIBUTE_MORTAR;
  layout.prop(ptr, "use_mortar", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (mortar) {
    ui::Layout &m = layout.column(true);
    m.prop(ptr, "mortar_offset", UI_ITEM_NONE, IFACE_("Offset"), ICON_NONE);
    m.prop(ptr, "mortar_remesh", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    ui::Layout &res = m.column(true);
    res.active_set(dmd.mortar_remesh != 3);
    res.prop(ptr, "mortar_resolution", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    ui::Layout &n = layout.column(true);
    n.prop(ptr, "noise_strength", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    n.prop(ptr, "noise_scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    n.prop(ptr, "noise_detail", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    layout.prop(ptr, "mortar_material_index", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  else {
    layout.prop(ptr, "use_keep_surface", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }

  ui::Layout &more = layout.column(true);
  more.prop(ptr, "max_count", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  more.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "use_realize", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.prop(ptr, "use_random_color", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  if (dmd.flag & MOD_DISTRIBUTE_COLOR) {
    layout.prop(ptr, "color_mode", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
    if (dmd.color_mode == 0) {
      ui::Layout &hue = layout.column(true);
      hue.prop(ptr, "hue_min", UI_ITEM_NONE, IFACE_("Hue Min"), ICON_NONE);
      hue.prop(ptr, "hue_max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);
      ui::Layout &sat = layout.column(true);
      sat.prop(ptr, "saturation_min", UI_ITEM_NONE, IFACE_("Saturation Min"), ICON_NONE);
      sat.prop(ptr, "saturation_max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);
    }
    ui::Layout &val = layout.column(true);
    val.prop(ptr, "value_min", UI_ITEM_NONE, IFACE_("Value Min"), ICON_NONE);
    val.prop(ptr, "value_max", UI_ITEM_NONE, IFACE_("Max"), ICON_NONE);
  }

  if (dmd.count_num > 0) {
    char text[64];
    SNPRINTF(text, IFACE_("Objects: %d"), dmd.count_num);
    layout.label(text, ICON_NONE);
  }

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Distribute, panel_draw);
}

ModifierTypeInfo modifierType_Distribute = {
    /*idname*/ "Distribute",
    /*name*/ N_("Distribute"),
    /*struct_name*/ "DistributeModifierData",
    /*struct_size*/ sizeof(DistributeModifierData),
    /*srna*/ &RNA_DistributeModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh,
    /*icon*/ ICON_MOD_DISTRIBUTE,

    /*copy_data*/ BKE_modifier_copydata_generic,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ modify_geometry_set,

    /*init_data*/ init_data,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ is_disabled,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
