/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Materialize: give faces materials by a list of rules, applied in order. Faces no rule picks
 * keep their material.
 */

#include "BLI_array.hh"
#include "BLI_hash.h"
#include "BLI_math_matrix.hh"
#include "BLI_string.h"
#include "BLI_string_utf8.h"
#include "BLI_task.hh"
#include "BLI_utildefines.h"

#include "BLT_translation.hh"

#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "DNA_texture_types.h"

#include "BKE_attribute.hh"
#include "BKE_image.hh"
#include "BKE_lib_query.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"
#include "BKE_screen.hh"
#include "BKE_texture.h"

#include "BLO_read_write.hh"

#include "UI_interface.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"

#include "RE_texture.h"

#include "GEO_mesh_materialize.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

using geometry::MaterializeType;

Span<MaterializeModifierRule> MaterializeModifierData::rules_span() const
{
  return {this->rules, this->rules_num};
}

MutableSpan<MaterializeModifierRule> MaterializeModifierData::rules_span()
{
  return {this->rules, this->rules_num};
}

static void init_data(ModifierData *md)
{
  auto *data = reinterpret_cast<MaterializeModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);

  MaterializeModifierRule *rule = MEM_new<MaterializeModifierRule>(__func__);
  STRNCPY_UTF8(rule->name, DATA_("Rule"));
  data->rules = rule;
  data->rules_num = 1;
  data->active_rule = 0;
}

static void copy_data(const ModifierData *md, ModifierData *target, const int flag)
{
  const auto *data = reinterpret_cast<const MaterializeModifierData *>(md);
  auto *tdata = reinterpret_cast<MaterializeModifierData *>(target);
  BKE_modifier_copydata_generic(md, target, flag);
  tdata->rules = static_cast<MaterializeModifierRule *>(MEM_dupalloc(data->rules));
}

static void free_data(ModifierData *md)
{
  auto *data = reinterpret_cast<MaterializeModifierData *>(md);
  MEM_SAFE_DELETE(data->rules);
}

static void blend_write(BlendWriter *writer, const ID * /*id_owner*/, const ModifierData *md)
{
  const auto *data = reinterpret_cast<const MaterializeModifierData *>(md);
  writer->write_struct(data);
  writer->write_struct_array(data->rules_num, data->rules);
}

static void blend_read(BlendDataReader *reader, ModifierData *md)
{
  auto *data = reinterpret_cast<MaterializeModifierData *>(md);
  BLO_read_struct_array(reader, MaterializeModifierRule, data->rules_num, &data->rules);
  if (data->rules == nullptr) {
    data->rules_num = 0;
  }
}

static bool rule_used(const MaterializeModifierRule &rule)
{
  return rule.flag & MOD_MATERIALIZE_RULE_ENABLED;
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const auto &data = *reinterpret_cast<MaterializeModifierData *>(md);
  for (const MaterializeModifierRule &rule : data.rules_span()) {
    if (rule_used(rule)) {
      return false;
    }
  }
  return true;
}

static bool depends_on_time(Scene * /*scene*/, ModifierData *md)
{
  const auto &data = *reinterpret_cast<MaterializeModifierData *>(md);
  for (const MaterializeModifierRule &rule : data.rules_span()) {
    if (rule_used(rule) && MaterializeType(rule.type) == MaterializeType::Texture &&
        rule.texture && BKE_texture_dependsOnTime(rule.texture))
    {
      return true;
    }
  }
  return false;
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  auto *data = reinterpret_cast<MaterializeModifierData *>(md);
  for (MaterializeModifierRule &rule : data->rules_span()) {
    walk(user_data, ob, reinterpret_cast<ID **>(&rule.material), IDWALK_CB_USER);
    walk(user_data, ob, reinterpret_cast<ID **>(&rule.only_on), IDWALK_CB_USER);
    walk(user_data, ob, reinterpret_cast<ID **>(&rule.texture), IDWALK_CB_USER);
    walk(user_data, ob, reinterpret_cast<ID **>(&rule.object), IDWALK_CB_NOP);
  }
}

static void foreach_tex_link(ModifierData *md, Object *ob, TexWalkFunc walk, void *user_data)
{
  auto *data = reinterpret_cast<MaterializeModifierData *>(md);
  for (MaterializeModifierRule &rule : data->rules_span()) {
    PointerRNA ptr = RNA_pointer_create_discrete(&ob->id, RNA_MaterializeModifierRule, &rule);
    PropertyRNA *prop = RNA_struct_find_property(&ptr, "texture");
    walk(user_data, ob, md, &ptr, prop);
  }
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  const auto &data = *reinterpret_cast<MaterializeModifierData *>(md);
  bool need_transform = false;
  for (const MaterializeModifierRule &rule : data.rules_span()) {
    if (!rule_used(rule)) {
      continue;
    }
    if (rule.flag & MOD_MATERIALIZE_RULE_GLOBAL) {
      need_transform = true;
    }
    switch (MaterializeType(rule.type)) {
      case MaterializeType::Texture:
        if (rule.texture) {
          DEG_add_generic_id_relation(ctx->node, &rule.texture->id, "Materialize Modifier");
        }
        break;
      case MaterializeType::Proximity:
        if (rule.object && rule.object != ctx->object) {
          DEG_add_object_relation(
              ctx->node, rule.object, DEG_OB_COMP_TRANSFORM, "Materialize Modifier");
          DEG_add_object_relation(
              ctx->node, rule.object, DEG_OB_COMP_GEOMETRY, "Materialize Modifier");
          need_transform = true;
        }
        break;
      case MaterializeType::Random:
        if (geometry::MaterializeRandom(rule.random) == geometry::MaterializeRandom::Object) {
          need_transform = true;
        }
        break;
      default:
        break;
    }
  }
  if (need_transform) {
    DEG_add_depends_on_transform_relation(ctx->node, "Materialize Modifier");
  }
}

/** Index of the material in the mesh, added at the end when it is not there yet. */
static int ensure_material_index(Mesh &mesh, Material *material)
{
  const int materials_num = mesh.totcol;
  if (mesh.mat && materials_num > 0) {
    const int index = Span(mesh.mat, materials_num).first_index_try(material);
    if (index != -1) {
      return index;
    }
  }
  BKE_id_material_eval_assign(&mesh.id, materials_num + 1, material);
  return materials_num;
}

static int find_material_index(const Mesh &mesh, const Material *material)
{
  if (mesh.mat == nullptr) {
    return -1;
  }
  return Span(mesh.mat, mesh.totcol).first_index_try(const_cast<Material *>(material));
}

static Array<float3> face_centers(const Mesh &mesh, const float4x4 *transform)
{
  const Span<float3> positions = mesh.vert_positions();
  const OffsetIndices faces = mesh.faces();
  const Span<int> corner_verts = mesh.corner_verts();
  Array<float3> centers(mesh.faces_num);
  threading::parallel_for(faces.index_range(), 1024, [&](const IndexRange range) {
    for (const int i : range) {
      centers[i] = bke::mesh::face_center_calc(positions, corner_verts.slice(faces[i]));
      if (transform) {
        centers[i] = math::transform_point(*transform, centers[i]);
      }
    }
  });
  return centers;
}

static Array<float> texture_values(Tex *tex,
                                   const ModifierEvalContext &ctx,
                                   const Span<float3> coords)
{
  Array<float> values(coords.size(), 0.0f);
  if (tex == nullptr) {
    return values;
  }
  if (tex->ima && BKE_image_is_animated(tex->ima)) {
    BKE_image_user_frame_calc(tex->ima, &tex->iuser, DEG_get_ctime(ctx.depsgraph));
  }
  ImagePool *pool = BKE_image_pool_new();
  BKE_texture_fetch_images_for_pool(tex, pool);
  threading::parallel_for(coords.index_range(), 512, [&](const IndexRange range) {
    for (const int i : range) {
      TexResult texres;
      BKE_texture_get_value_ex(tex, coords[i], &texres, pool, false);
      values[i] = texres.tin;
    }
  });
  BKE_image_pool_free(pool);
  return values;
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const auto &data = *reinterpret_cast<MaterializeModifierData *>(md);
  if (mesh->faces_num == 0 || is_disabled(nullptr, md, false)) {
    return mesh;
  }

  Mesh *result = BKE_mesh_copy_for_eval(*mesh);
  if (result->totcol == 0) {
    /* Faces no rule picks keep pointing at an empty slot. */
    BKE_id_material_eval_ensure_default_slot(&result->id);
  }

  const float4x4 &to_world = ctx->object->object_to_world();
  Vector<geometry::MaterializeRule> rules;
  /* Per face values for Attribute, Texture and Proximity rules. */
  Vector<Array<float>> storage;
  std::optional<Array<float3>> local_centers;
  std::optional<Array<float3>> world_centers;

  for (const MaterializeModifierRule &dna : data.rules_span()) {
    if (!rule_used(dna)) {
      continue;
    }
    geometry::MaterializeRule rule;
    rule.type = MaterializeType(dna.type);

    if (dna.flag & MOD_MATERIALIZE_RULE_USE_INDEX) {
      rule.material = std::max(dna.material_index, 0);
    }
    else if (dna.material) {
      rule.material = ensure_material_index(*result, dna.material);
    }
    else {
      /* No material picked: nothing to give. */
      continue;
    }
    if (dna.only_on) {
      rule.only_on = find_material_index(*result, dna.only_on);
      if (rule.only_on == -1) {
        continue;
      }
    }

    rule.invert = dna.flag & MOD_MATERIALIZE_RULE_INVERT;
    rule.grow = dna.grow;
    rule.min_island = dna.min_island;
    rule.breakup = dna.breakup;
    rule.breakup_scale = dna.breakup_scale;
    rule.seed = dna.seed;
    rule.global = dna.flag & MOD_MATERIALIZE_RULE_GLOBAL;
    rule.curvature = geometry::MaterializeCurvature(dna.curvature);
    rule.curvature_angle = dna.curvature_angle;
    rule.direction = geometry::MaterializeDirection(dna.direction);
    rule.custom_direction = float3(dna.custom_direction);
    rule.direction_angle = dna.direction_angle;
    rule.axis = dna.axis;
    rule.height_from = dna.height_from;
    rule.height_to = dna.height_to;
    rule.relative = dna.flag & MOD_MATERIALIZE_RULE_RELATIVE;
    rule.slope_min = dna.slope_min;
    rule.slope_max = dna.slope_max;
    rule.noise_scale = dna.noise_scale;
    rule.noise_detail = dna.noise_detail;
    rule.noise_threshold = dna.noise_threshold;
    rule.random = geometry::MaterializeRandom(dna.random);
    rule.chance = dna.chance;
    rule.threshold = dna.threshold;
    rule.distance = dna.distance;
    rule.samples = dna.samples;
    rule.occlusion_distance = dna.occlusion_distance;
    rule.occlusion = dna.occlusion;
    rule.thickness = dna.thickness;

    switch (rule.type) {
      case MaterializeType::Attribute: {
        Array<float> values(result->faces_num, 0.0f);
        if (dna.attribute[0] != '\0') {
          const VArraySpan<float> read = *result->attributes().lookup<float>(
              dna.attribute, bke::AttrDomain::Face);
          if (!read.is_empty()) {
            values.as_mutable_span().copy_from(read);
          }
        }
        storage.append(std::move(values));
        rule.values = storage.last();
        break;
      }
      case MaterializeType::Texture: {
        std::optional<Array<float3>> &centers = rule.global ? world_centers : local_centers;
        if (!centers) {
          centers = face_centers(*result, rule.global ? &to_world : nullptr);
        }
        storage.append(texture_values(dna.texture, *ctx, *centers));
        rule.values = storage.last();
        break;
      }
      case MaterializeType::Proximity: {
        Array<float> distances(result->faces_num, FLT_MAX);
        if (dna.object && dna.object != ctx->object) {
          if (const Mesh *target = BKE_modifier_get_evaluated_mesh_from_evaluated_object(
                  dna.object))
          {
            const float4x4 target_to_mesh = ctx->object->world_to_object() *
                                            dna.object->object_to_world();
            distances = geometry::materialize_face_distances(*result, *target, target_to_mesh);
          }
        }
        storage.append(std::move(distances));
        rule.distances = storage.last();
        break;
      }
      default:
        break;
    }
    rules.append(rule);
  }

  if (rules.is_empty()) {
    return result;
  }

  geometry::MeshMaterializeParams params;
  params.rules = rules;
  params.object_to_world = to_world;
  params.object_seed = int(BLI_hash_string(ctx->object->id.name + 2));
  geometry::mesh_materialize(*result, params);
  return result;
}

/* -------------------------------------------------------------------- */
/** \name UI
 * \{ */

static void draw_rule_settings(ui::Layout &layout, PointerRNA &ob_ptr, PointerRNA &rule_ptr)
{
  layout.prop(&rule_ptr, "type", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &mat_row = layout.row(true);
  if (RNA_boolean_get(&rule_ptr, "use_index")) {
    mat_row.prop(&rule_ptr, "material_index", UI_ITEM_NONE, IFACE_("Material"), ICON_NONE);
  }
  else {
    mat_row.prop(&rule_ptr, "material", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  }
  mat_row.prop(&rule_ptr, "use_index", UI_ITEM_NONE, "", ICON_LINENUMBERS_ON);
  layout.prop(&rule_ptr, "only_on", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.separator();
  const MaterializeType type = MaterializeType(RNA_enum_get(&rule_ptr, "type"));
  PointerRNA mesh_ptr = RNA_pointer_get(&ob_ptr, "data");
  ui::Layout &col = layout.column(true);
  switch (type) {
    case MaterializeType::Curvature:
      col.prop(&rule_ptr, "curvature", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "curvature_angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Direction:
      col.prop(&rule_ptr, "direction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      if (geometry::MaterializeDirection(RNA_enum_get(&rule_ptr, "direction")) ==
          geometry::MaterializeDirection::Custom)
      {
        col.prop(&rule_ptr, "custom_direction", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      }
      col.prop(&rule_ptr, "direction_angle", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(&rule_ptr, "use_global", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Height:
      col.prop(&rule_ptr, "axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "height_from", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "height_to", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(&rule_ptr, "use_relative", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(&rule_ptr, "use_global", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Slope:
      col.prop(&rule_ptr, "slope_min", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "slope_max", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(&rule_ptr, "use_global", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Noise:
      col.prop(&rule_ptr, "noise_scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "noise_detail", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "noise_threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(&rule_ptr, "use_global", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Random:
      col.prop(&rule_ptr, "random", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "chance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Attribute:
      col.prop_search(
          &rule_ptr, "attribute", &mesh_ptr, "attributes", std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Proximity:
      col.prop(&rule_ptr, "object", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Occlusion:
      col.prop(&rule_ptr, "occlusion", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "occlusion_distance", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "samples", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Thickness:
      col.prop(&rule_ptr, "thickness", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "samples", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
    case MaterializeType::Texture:
      col.prop(&rule_ptr, "texture", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      col.prop(&rule_ptr, "threshold", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      layout.prop(&rule_ptr, "use_global", UI_ITEM_NONE, std::nullopt, ICON_NONE);
      break;
  }

  layout.separator();
  ui::Layout &shape = layout.column(true);
  shape.prop(&rule_ptr, "grow", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  shape.prop(&rule_ptr, "min_island", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  ui::Layout &breakup = layout.column(true);
  breakup.prop(&rule_ptr, "breakup", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  breakup.prop(&rule_ptr, "breakup_scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(&rule_ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(&rule_ptr, "invert", UI_ITEM_NONE, std::nullopt, ICON_NONE);
}

static void panel_draw(const bContext *C, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);
  auto *data = static_cast<MaterializeModifierData *>(ptr->data);

  ui::Layout &row = layout.row(false);
  template_list(&row,
                const_cast<bContext *>(C),
                "MOD_UL_materialize_rules",
                "",
                ptr,
                "rules",
                ptr,
                "active_rule",
                nullptr,
                3,
                10,
                0,
                ui::TEMPLATE_LIST_FLAG_NONE);

  ui::Layout &col = row.column(false);
  ui::Layout *sub = &col.column(true);
  sub->op("OBJECT_OT_materialize_rule_add", "", ICON_ADD);
  sub->op("OBJECT_OT_materialize_rule_remove", "", ICON_REMOVE);
  col.separator();
  sub = &col.column(true);
  PointerRNA op_ptr = sub->op("OBJECT_OT_materialize_rule_move", "", ICON_TRIA_UP);
  RNA_enum_set(&op_ptr, "type", -1);
  op_ptr = sub->op("OBJECT_OT_materialize_rule_move", "", ICON_TRIA_DOWN);
  RNA_enum_set(&op_ptr, "type", 1);

  if (data->active_rule >= 0 && data->active_rule < data->rules_num) {
    PointerRNA rule_ptr = RNA_pointer_create_discrete(
        ptr->owner_id, RNA_MaterializeModifierRule, &data->rules[data->active_rule]);
    ui::Layout &settings = layout.column(false);
    settings.use_property_split_set(true);
    settings.active_set(RNA_boolean_get(&rule_ptr, "use"));
    draw_rule_settings(settings, ob_ptr, rule_ptr);
  }

  modifier_error_message_draw(layout, ptr);
}

static void rule_list_item_draw(uiList * /*ui_list*/,
                                const bContext * /*C*/,
                                ui::Layout &layout,
                                PointerRNA * /*idataptr*/,
                                PointerRNA *itemptr,
                                int /*icon*/,
                                PointerRNA * /*active_dataptr*/,
                                const char * /*active_propname*/,
                                int /*index*/,
                                int /*flt_flag*/)
{
  ui::Layout &row = layout.row(true);
  row.prop(itemptr, "name", ui::ITEM_R_NO_BG, "", ICON_NONE);
  row.prop(itemptr, "use", UI_ITEM_NONE, "", ICON_NONE);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Materialize, panel_draw);

  uiListType *list_type = MEM_new_zeroed<uiListType>("Materialize modifier rules");
  STRNCPY(list_type->idname, "MOD_UL_materialize_rules");
  list_type->draw_item = rule_list_item_draw;
  WM_uilisttype_add(list_type);
}

/** \} */

ModifierTypeInfo modifierType_Materialize = {
    /*idname*/ "Materialize",
    /*name*/ N_("Materialize"),
    /*struct_name*/ "MaterializeModifierData",
    /*struct_size*/ sizeof(MaterializeModifierData),
    /*srna*/ &RNA_MaterializeModifier,
    /*type*/ ModifierTypeType::Nonconstructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_MATERIALIZE,

    /*copy_data*/ copy_data,

    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,

    /*init_data*/ init_data,
    /*required_data_mask*/ nullptr,
    /*free_data*/ free_data,
    /*is_disabled*/ is_disabled,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ depends_on_time,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ foreach_tex_link,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ blend_write,
    /*blend_read*/ blend_read,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender
