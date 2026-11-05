/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 *
 * Nodes that run the code of a mesh modifier, so that the node and the modifier always give the
 * same result. The sockets of each node are made from the RNA properties of its modifier, with
 * the same names, ranges and defaults. A "Weight" field takes the place of the vertex group.
 */

#include <functional>
#include <vector>

#include "MEM_guardedalloc.h"
#include <mutex>

#include "BLI_atomic_disjoint_set.hh"
#include "BLI_listbase.h"
#include "BLI_set.hh"
#include "BLI_string.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"

#include "BKE_customdata.hh"
#include "BKE_deform.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_randomize.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_modifier_nodes_cc {

/** Name of the vertex group that passes the "Weight" field to the modifier. */
static constexpr const char *weight_group_name = ".node_weight";

using ObjectSetter = void (*)(ModifierData &md, Object *ob);
using NameSetter = void (*)(ModifierData &md, const char *name);

struct ObjectInput {
  const char *name;
  const char *description;
  ObjectSetter set;
};

struct ModifierNodeInfo {
  const char *idname;
  const char *ui_name;
  const char *description;
  ModifierType type;
  StructRNA **srna;
  /** RNA properties that become inputs, in order. */
  std::vector<const char *> props;
  std::vector<ObjectInput> objects;
  /** Sets the vertex group name of the modifier. Null when it has no vertex group. */
  NameSetter set_vgroup = nullptr;
  /** The modifier reads the scene time. */
  bool needs_depsgraph = false;
};

#define OBJ_SETTER(Type, field) \
  [](ModifierData &md, Object *ob) { reinterpret_cast<Type *>(&md)->field = ob; }
#define VGROUP_SETTER(Type, field) \
  [](ModifierData &md, const char *name) { STRNCPY(reinterpret_cast<Type *>(&md)->field, name); }

static const Vector<ModifierNodeInfo> &node_infos()
{
  static const Vector<ModifierNodeInfo> infos = [] {
    Vector<ModifierNodeInfo> v;
    v.append({"GeometryNodeSolidify",
              "Solidify",
              "Give the surface of a mesh thickness, the same as the Solidify modifier",
              eModifierType_Solidify,
              &RNA_SolidifyModifier,
              {"solidify_mode",
               "nonmanifold_thickness_mode",
               "nonmanifold_boundary_mode",
               "thickness",
               "offset",
               "use_even_offset",
               "use_quality_normals",
               "use_rim",
               "use_rim_only",
               "use_flip_normals",
               "use_flat_faces",
               "nonmanifold_merge_threshold",
               "thickness_clamp",
               "use_thickness_angle_clamp",
               "thickness_vertex_group",
               "edge_crease_inner",
               "edge_crease_outer",
               "edge_crease_rim",
               "bevel_convex",
               "material_offset",
               "material_offset_rim"},
              {},
              VGROUP_SETTER(SolidifyModifierData, defgrp_name)});
    v.append({"GeometryNodeWireframe",
              "Wireframe",
              "Turn the edges of a mesh into thin bars, the same as the Wireframe modifier",
              eModifierType_Wireframe,
              &RNA_WireframeModifier,
              {"thickness",
               "offset",
               "use_boundary",
               "use_replace",
               "use_even_offset",
               "use_relative_offset",
               "use_crease",
               "crease_weight",
               "material_offset",
               "thickness_vertex_group"},
              {},
              VGROUP_SETTER(WireframeModifierData, defgrp_name)});
    v.append({"GeometryNodeScrew",
              "Screw",
              "Sweep a profile around an axis, the same as the Screw modifier",
              eModifierType_Screw,
              &RNA_ScrewModifier,
              {"axis",
               "angle",
               "screw_offset",
               "iterations",
               "steps",
               "use_merge_vertices",
               "merge_threshold",
               "use_normal_flip",
               "use_normal_calculate",
               "use_smooth_shade",
               "use_stretch_u",
               "use_stretch_v",
               "use_object_screw_offset"},
              {{"Axis Object",
                "Object whose transform sets the axis",
                OBJ_SETTER(ScrewModifierData, ob_axis)}}});
    v.append({"GeometryNodeMirror",
              "Mirror",
              "Mirror a mesh along its axes, with optional bisect and merge, the same as the "
              "Mirror modifier",
              eModifierType_Mirror,
              &RNA_MirrorModifier,
              {"use_axis",
               "use_bisect_axis",
               "use_bisect_flip_axis",
               "use_mirror_merge",
               "merge_threshold",
               "bisect_threshold",
               "use_clip",
               "use_mirror_vertex_groups",
               "use_mirror_u",
               "use_mirror_v",
               "use_mirror_udim",
               "mirror_offset_u",
               "mirror_offset_v",
               "offset_u",
               "offset_v"},
              {{"Mirror Object",
                "Object to use as the mirror center",
                OBJ_SETTER(MirrorModifierData, mirror_ob)}}});
    v.append({"GeometryNodeSkin",
              "Skin",
              "Build a skin around the edges of a mesh, the same as the Skin modifier",
              eModifierType_Skin,
              &RNA_SkinModifier,
              {"branch_smoothing", "use_x_symmetry", "use_y_symmetry", "use_z_symmetry",
               "use_smooth_shade"}});
    v.append({"GeometryNodeCast",
              "Cast",
              "Push a mesh toward a sphere, cylinder or box, the same as the Cast modifier",
              eModifierType_Cast,
              &RNA_CastModifier,
              {"cast_type",
               "use_x",
               "use_y",
               "use_z",
               "factor",
               "radius",
               "size",
               "use_radius_as_size",
               "use_transform"},
              {{"Control Object",
                "Object whose transform controls the shape",
                OBJ_SETTER(CastModifierData, object)}},
              VGROUP_SETTER(CastModifierData, defgrp_name)});
    v.append({"GeometryNodeWave",
              "Wave",
              "Move the points of a mesh in ripples, the same as the Wave modifier",
              eModifierType_Wave,
              &RNA_WaveModifier,
              {"use_x",
               "use_y",
               "use_cyclic",
               "use_normal",
               "use_normal_x",
               "use_normal_y",
               "use_normal_z",
               "falloff_radius",
               "height",
               "width",
               "narrowness",
               "start_position_x",
               "start_position_y",
               "time_offset",
               "lifetime",
               "damping_time",
               "speed"},
              {{"Start Object",
                "Object whose position is the start of the waves",
                OBJ_SETTER(WaveModifierData, objectcenter)}},
              VGROUP_SETTER(WaveModifierData, defgrp_name),
              true});
    v.append({"GeometryNodeWarp",
              "Warp",
              "Move a part of a mesh from one object to another, the same as the Warp modifier",
              eModifierType_Warp,
              &RNA_WarpModifier,
              {"strength", "falloff_type", "falloff_radius", "use_volume_preserve"},
              {{"Object From",
                "Object to move the points from",
                OBJ_SETTER(WarpModifierData, object_from)},
               {"Object To", "Object to move the points to", OBJ_SETTER(WarpModifierData, object_to)}},
              VGROUP_SETTER(WarpModifierData, defgrp_name)});
    v.append({"GeometryNodeWeightedNormal",
              "Weighted Normal",
              "Set custom normals from the area or angles of the faces, the same as the Weighted "
              "Normal modifier",
              eModifierType_WeightedNormal,
              &RNA_WeightedNormalModifier,
              {"mode", "weight", "thresh", "keep_sharp", "use_face_influence"},
              {},
              VGROUP_SETTER(WeightedNormalModifierData, defgrp_name)});
    v.append({"GeometryNodeLaplacianSmooth",
              "Laplacian Smooth",
              "Smooth a mesh while keeping its volume, the same as the Laplacian Smooth modifier",
              eModifierType_LaplacianSmooth,
              &RNA_LaplacianSmoothModifier,
              {"iterations",
               "lambda_factor",
               "lambda_border",
               "use_x",
               "use_y",
               "use_z",
               "use_volume_preserve",
               "use_normalized"},
              {},
              VGROUP_SETTER(LaplacianSmoothModifierData, defgrp_name)});
    v.append({"GeometryNodeShrinkwrap",
              "Shrinkwrap",
              "Move the points of a mesh onto the surface of another mesh, the same as the "
              "Shrinkwrap modifier",
              eModifierType_Shrinkwrap,
              &RNA_ShrinkwrapModifier,
              {"wrap_method",
               "wrap_mode",
               "offset",
               "project_limit",
               "subsurf_levels",
               "use_project_x",
               "use_project_y",
               "use_project_z",
               "use_negative_direction",
               "use_positive_direction",
               "cull_face",
               "use_invert_cull"},
              {{"Target", "Mesh object to wrap onto", OBJ_SETTER(ShrinkwrapModifierData, target)},
               {"Auxiliary Target",
                "Extra mesh object to project onto",
                OBJ_SETTER(ShrinkwrapModifierData, auxTarget)}},
              VGROUP_SETTER(ShrinkwrapModifierData, vgroup_name)});
    v.append({"GeometryNodeSmoothModifier",
              "Smooth Points",
              "Smooth the points of a mesh, the same as the Smooth modifier",
              eModifierType_Smooth,
              &RNA_SmoothModifier,
              {"factor", "iterations", "use_x", "use_y", "use_z"},
              {},
              VGROUP_SETTER(SmoothModifierData, defgrp_name)});
    v.append({"GeometryNodeSimpleDeform",
              "Simple Deform",
              "Twist, bend, taper or stretch a mesh, the same as the Simple Deform modifier",
              eModifierType_SimpleDeform,
              &RNA_SimpleDeformModifier,
              {"deform_method", "deform_axis", "angle", "factor", "limits", "lock_x", "lock_y",
               "lock_z"},
              {{"Origin",
                "Object whose transform is the center of the deformation",
                OBJ_SETTER(SimpleDeformModifierData, origin)}},
              VGROUP_SETTER(SimpleDeformModifierData, vgroup_name)});
    v.append({"GeometryNodeArrayModifier",
              "Array",
              "Repeat a mesh with offsets and merge the copies, the same as the Array modifier",
              eModifierType_Array,
              &RNA_ArrayModifier,
              {"fit_type",
               "count",
               "fit_length",
               "use_relative_offset",
               "relative_offset_displace",
               "use_constant_offset",
               "constant_offset_displace",
               "use_object_offset",
               "use_merge_vertices",
               "use_merge_vertices_cap",
               "merge_threshold",
               "offset_u",
               "offset_v"},
              {{"Offset Object",
                "Object whose transform is added to each copy",
                OBJ_SETTER(ArrayModifierData, offset_ob)},
               {"Start Cap", "Mesh object to add at the start", OBJ_SETTER(ArrayModifierData, start_cap)},
               {"End Cap", "Mesh object to add at the end", OBJ_SETTER(ArrayModifierData, end_cap)},
               {"Curve",
                "Curve object whose length the copies fill",
                OBJ_SETTER(ArrayModifierData, curve_ob)}}});
    v.append({"GeometryNodeEdgeSplitModifier",
              "Edge Split",
              "Split the edges that are sharp or meet at a large angle, the same as the Edge "
              "Split modifier",
              eModifierType_EdgeSplit,
              &RNA_EdgeSplitModifier,
              {"use_edge_angle", "split_angle", "use_edge_sharp"}});
    v.append({"GeometryNodeUVWarp",
              "UV Warp",
              "Move, rotate and scale the UV map, the same as the UV Warp modifier",
              eModifierType_UVWarp,
              &RNA_UVWarpModifier,
              {"center", "axis_u", "axis_v", "offset", "scale", "rotation"},
              {{"Object From",
                "Object whose transform is the start of the warp",
                OBJ_SETTER(UVWarpModifierData, object_src)},
               {"Object To",
                "Object whose transform is the end of the warp",
                OBJ_SETTER(UVWarpModifierData, object_dst)}},
              VGROUP_SETTER(UVWarpModifierData, vgroup_name)});
    v.append({"GeometryNodeNormalEdit",
              "Normal Edit",
              "Set custom normals pointing away from a point or along a direction, the same as "
              "the Normal Edit modifier",
              eModifierType_NormalEdit,
              &RNA_NormalEditModifier,
              {"mode", "mix_mode", "mix_factor", "mix_limit", "no_polynors_fix",
               "use_direction_parallel"},
              {{"Target",
                "Object the normals point to or away from",
                OBJ_SETTER(NormalEditModifierData, target)}},
              VGROUP_SETTER(NormalEditModifierData, defgrp_name)});
    v.append({"GeometryNodeLatticeDeform",
              "Lattice Deform",
              "Deform a mesh with a lattice object, the same as the Lattice modifier",
              eModifierType_Lattice,
              &RNA_LatticeModifier,
              {"strength"},
              {{"Lattice", "Lattice object to deform with", OBJ_SETTER(LatticeModifierData, object)}},
              VGROUP_SETTER(LatticeModifierData, name)});
    v.append({"GeometryNodeCurveDeform",
              "Curve Deform",
              "Bend a mesh along a curve object, the same as the Curve modifier",
              eModifierType_Curve,
              &RNA_CurveModifier,
              {"deform_axis"},
              {{"Curve", "Curve object to deform along", OBJ_SETTER(CurveModifierData, object)}},
              VGROUP_SETTER(CurveModifierData, name)});
    v.append({"GeometryNodeBuild",
              "Build",
              "Show the faces of a mesh one by one over time, the same as the Build modifier",
              eModifierType_Build,
              &RNA_BuildModifier,
              {"frame_start", "frame_duration", "use_reverse", "use_random_order", "seed"},
              {},
              nullptr,
              true});
    v.append({"GeometryNodeRemeshModifier",
              "Remesh Blocks",
              "Rebuild a mesh out of blocks or voxels, the same as the Remesh modifier",
              eModifierType_Remesh,
              &RNA_RemeshModifier,
              {"mode",
               "voxel_size",
               "adaptivity",
               "octree_depth",
               "scale",
               "sharpness",
               "use_remove_disconnected",
               "threshold",
               "use_smooth_shade"}});
    return v;
  }();
  return infos;
}

#undef OBJ_SETTER
#undef VGROUP_SETTER

/** Number of entries in #node_infos, needed at compile time to make a node type for each. */
static constexpr int NODES_NUM = 21;

static const char *axis_suffix(const char *prop_id, const int len, const int i)
{
  if (len == 2) {
    if (STREQ(prop_id, "limits")) {
      return i == 0 ? "Min" : "Max";
    }
    return i == 0 ? "U" : "V";
  }
  static const char *xyz[4] = {"X", "Y", "Z", "W"};
  return xyz[std::min(i, 3)];
}

static std::string array_item_identifier(const char *prop_id, const int i)
{
  return std::string(prop_id) + "_" + std::to_string(i);
}

static PropertySubType float_socket_subtype(const PropertySubType subtype)
{
  switch (subtype) {
    case PROP_DISTANCE:
    case PROP_ANGLE:
    case PROP_FACTOR:
    case PROP_PERCENTAGE:
    case PROP_TIME:
      return subtype;
    default:
      return PROP_NONE;
  }
}

static void declare_property(NodeDeclarationBuilder &b, PointerRNA &ptr, PropertyRNA *prop)
{
  const char *id = RNA_property_identifier(prop);
  const char *name = RNA_property_ui_name_raw(prop);
  const char *description = RNA_property_ui_description_raw(prop);
  const int len = RNA_property_array_length(&ptr, prop);

  switch (RNA_property_type(prop)) {
    case PROP_BOOLEAN: {
      if (len == 0) {
        b.add_input<decl::Bool>(name, id)
            .default_value(RNA_property_boolean_get(&ptr, prop))
            .description(description);
        break;
      }
      Array<bool> values(len);
      RNA_property_boolean_get_array(&ptr, prop, values.data());
      for (const int i : IndexRange(len)) {
        b.add_input<decl::Bool>(std::string(name) + " " + axis_suffix(id, len, i),
                                array_item_identifier(id, i))
            .default_value(values[i])
            .description(description);
      }
      break;
    }
    case PROP_INT: {
      int min, max;
      RNA_property_int_range(&ptr, prop, &min, &max);
      b.add_input<decl::Int>(name, id)
          .default_value(RNA_property_int_get(&ptr, prop))
          .min(min)
          .max(max)
          .description(description);
      break;
    }
    case PROP_FLOAT: {
      float min, max;
      RNA_property_float_range(&ptr, prop, &min, &max);
      const PropertySubType subtype = RNA_property_subtype(prop);
      if (len == 3) {
        float3 value;
        RNA_property_float_get_array(&ptr, prop, &value.x);
        b.add_input<decl::Vector>(name, id)
            .default_value(value)
            .subtype(ELEM(subtype, PROP_TRANSLATION, PROP_EULER, PROP_XYZ) ? subtype : PROP_XYZ)
            .description(description);
        break;
      }
      if (len > 0) {
        Array<float> values(len);
        RNA_property_float_get_array(&ptr, prop, values.data());
        for (const int i : IndexRange(len)) {
          b.add_input<decl::Float>(std::string(name) + " " + axis_suffix(id, len, i),
                                   array_item_identifier(id, i))
              .default_value(values[i])
              .min(min)
              .max(max)
              .subtype(float_socket_subtype(subtype))
              .description(description);
        }
        break;
      }
      b.add_input<decl::Float>(name, id)
          .default_value(RNA_property_float_get(&ptr, prop))
          .min(min)
          .max(max)
          .subtype(float_socket_subtype(subtype))
          .description(description);
      break;
    }
    case PROP_ENUM: {
      if (RNA_property_flag(prop) & PROP_ENUM_FLAG) {
        break;
      }
      const EnumPropertyItem *items = nullptr;
      int items_num = 0;
      bool free = false;
      RNA_property_enum_items_ex(nullptr, &ptr, prop, true, &items, &items_num, &free);
      if (items == nullptr || free) {
        /* Only static items stay valid for the lifetime of the declaration. */
        if (free) {
          MEM_delete(items);
        }
        break;
      }
      b.add_input<decl::Menu>(name, id)
          .static_items(items)
          .default_value(MenuValue(RNA_property_enum_get(&ptr, prop)))
          .optional_label()
          .description(description);
      break;
    }
    default:
      break;
  }
}

static void declare_from_info(const ModifierNodeInfo &info, NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to change");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  if (info.set_vgroup) {
    b.add_input<decl::Float>("Weight")
        .default_value(1.0f)
        .min(0.0f)
        .max(1.0f)
        .subtype(PROP_FACTOR)
        .field_on_all()
        .description("How much each point is affected, like the vertex group of the modifier");
  }
  if (info.type == eModifierType_Skin) {
    b.add_input<decl::Float>("Radius")
        .default_value(0.25f)
        .min(0.0f)
        .subtype(PROP_DISTANCE)
        .field_on_all()
        .description("Size of the skin around each point");
  }
  for (const ObjectInput &object : info.objects) {
    b.add_input<decl::Object>(object.name).description(object.description);
  }

  ModifierData *md = BKE_modifier_new(info.type);
  PointerRNA ptr = RNA_pointer_create_discrete(nullptr, *info.srna, md);
  for (const char *prop_id : info.props) {
    PropertyRNA *prop = RNA_struct_find_property(&ptr, prop_id);
    BLI_assert_msg(prop != nullptr, "Modifier node property not found");
    if (prop != nullptr) {
      declare_property(b, ptr, prop);
    }
  }
  BKE_modifier_free(md);
}

using PropertySetter = std::function<void(PointerRNA &ptr)>;

/** Read the inputs made from RNA properties, as functions that set them on a new modifier. */
static Vector<PropertySetter> extract_property_setters(const ModifierNodeInfo &info,
                                                       GeoNodeExecParams &params)
{
  Vector<PropertySetter> setters;
  ModifierData *md = BKE_modifier_new(info.type);
  PointerRNA ptr = RNA_pointer_create_discrete(nullptr, *info.srna, md);
  for (const char *prop_id : info.props) {
    PropertyRNA *prop = RNA_struct_find_property(&ptr, prop_id);
    if (prop == nullptr) {
      continue;
    }
    const int len = RNA_property_array_length(&ptr, prop);
    switch (RNA_property_type(prop)) {
      case PROP_BOOLEAN: {
        if (len == 0) {
          const bool value = params.extract_input<bool>(prop_id);
          setters.append([prop, value](PointerRNA &p) { RNA_property_boolean_set(&p, prop, value); });
          break;
        }
        Array<bool> values(len);
        for (const int i : IndexRange(len)) {
          values[i] = params.extract_input<bool>(array_item_identifier(prop_id, i));
        }
        setters.append([prop, values](PointerRNA &p) {
          RNA_property_boolean_set_array(&p, prop, values.data());
        });
        break;
      }
      case PROP_INT: {
        const int value = params.extract_input<int>(prop_id);
        setters.append([prop, value](PointerRNA &p) { RNA_property_int_set(&p, prop, value); });
        break;
      }
      case PROP_FLOAT: {
        if (len == 3) {
          const float3 value = params.extract_input<float3>(prop_id);
          setters.append(
              [prop, value](PointerRNA &p) { RNA_property_float_set_array(&p, prop, &value.x); });
          break;
        }
        if (len > 0) {
          Array<float> values(len);
          for (const int i : IndexRange(len)) {
            values[i] = params.extract_input<float>(array_item_identifier(prop_id, i));
          }
          setters.append([prop, values](PointerRNA &p) {
            RNA_property_float_set_array(&p, prop, values.data());
          });
          break;
        }
        const float value = params.extract_input<float>(prop_id);
        setters.append([prop, value](PointerRNA &p) { RNA_property_float_set(&p, prop, value); });
        break;
      }
      case PROP_ENUM: {
        if (RNA_property_flag(prop) & PROP_ENUM_FLAG) {
          break;
        }
        const int value = params.extract_input<MenuValue>(prop_id).value;
        setters.append([prop, value](PointerRNA &p) { RNA_property_enum_set(&p, prop, value); });
        break;
      }
      default:
        break;
    }
  }
  BKE_modifier_free(md);
  return setters;
}

static void add_weight_group(Mesh &mesh, const Field<float> &weight_field)
{
  bDeformGroup *group = MEM_new<bDeformGroup>(__func__);
  STRNCPY(group->name, weight_group_name);
  BLI_addtail(&mesh.vertex_group_names, group);
  const int group_index = BLI_listbase_count(&mesh.vertex_group_names) - 1;

  const bke::MeshFieldContext context{mesh, bke::AttrDomain::Point};
  fn::FieldEvaluator evaluator{context, mesh.verts_num};
  evaluator.add(weight_field);
  evaluator.evaluate();
  const VArray<float> weights = evaluator.get_evaluated<float>(0);

  MutableSpan<MDeformVert> dverts = mesh.deform_verts_for_write();
  for (const int i : IndexRange(mesh.verts_num)) {
    const float weight = weights[i];
    if (weight > 0.0f) {
      MDeformWeight *dw = BKE_defvert_ensure_index(&dverts[i], group_index);
      dw->weight = std::min(weight, 1.0f);
    }
  }
}

static void remove_weight_group(Mesh &mesh)
{
  const int group_index = BKE_id_defgroup_name_index(&mesh.id, weight_group_name);
  if (group_index == -1) {
    return;
  }
  if (!mesh.deform_verts().is_empty()) {
    for (MDeformVert &dvert : mesh.deform_verts_for_write()) {
      if (MDeformWeight *dw = BKE_defvert_find_index(&dvert, group_index)) {
        BKE_defvert_remove_group(&dvert, dw);
      }
      for (MDeformWeight &dw : MutableSpan(dvert.dw, dvert.totweight)) {
        if (dw.def_nr > group_index) {
          dw.def_nr--;
        }
      }
    }
  }
  BLI_freelinkN(&mesh.vertex_group_names, BLI_findlink(&mesh.vertex_group_names, group_index));
}

/** The Skin modifier needs a radius for each point and a root point in each piece. */
static void add_skin_data(Mesh &mesh, const Field<float> &radius_field)
{
  const bke::MeshFieldContext context{mesh, bke::AttrDomain::Point};
  fn::FieldEvaluator evaluator{context, mesh.verts_num};
  evaluator.add(radius_field);
  evaluator.evaluate();
  const VArray<float> radii = evaluator.get_evaluated<float>(0);

  MVertSkin *skin = static_cast<MVertSkin *>(
      CustomData_get_layer_for_write(&mesh.vert_data, CD_MVERT_SKIN, mesh.verts_num));
  if (skin == nullptr) {
    skin = static_cast<MVertSkin *>(
        CustomData_add_layer(&mesh.vert_data, CD_MVERT_SKIN, CD_SET_DEFAULT, mesh.verts_num));
  }

  AtomicDisjointSet islands(mesh.verts_num);
  for (const int2 &edge : mesh.edges()) {
    islands.join(edge[0], edge[1]);
  }
  Set<int> roots;
  for (const int i : IndexRange(mesh.verts_num)) {
    const float radius = radii[i];
    skin[i].radius[0] = radius;
    skin[i].radius[1] = radius;
    skin[i].flag &= ~MVERT_SKIN_ROOT;
    if (roots.add(islands.find_root(i))) {
      skin[i].flag |= MVERT_SKIN_ROOT;
    }
  }
}

static Mesh *run_modifier(const ModifierTypeInfo &mti,
                          ModifierData &md,
                          const ModifierEvalContext &ctx,
                          Mesh *mesh)
{
  if (mti.type == ModifierTypeType::OnlyDeform) {
    MutableSpan<float3> positions = mesh->vert_positions_for_write();
    mti.deform_verts(&md, &ctx, mesh, positions);
    mesh->tag_positions_changed();
    return mesh;
  }
  Mesh *result = mti.modify_mesh(&md, &ctx, mesh);
  if (result != mesh) {
    BKE_id_free(nullptr, mesh);
  }
  return result;
}

static void exec_from_info(const ModifierNodeInfo &info, GeoNodeExecParams &params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");

  std::optional<Field<float>> weight_field;
  if (info.set_vgroup) {
    Field<float> field = params.extract_input<Field<float>>("Weight");
    if (field.node().depends_on_input() || fn::evaluate_constant_field(field) != 1.0f) {
      weight_field = std::move(field);
    }
  }
  std::optional<Field<float>> radius_field;
  if (info.type == eModifierType_Skin) {
    radius_field = params.extract_input<Field<float>>("Radius");
  }
  Vector<Object *> objects;
  for (const ObjectInput &object : info.objects) {
    objects.append(params.extract_input<Object *>(object.name));
  }
  const Vector<PropertySetter> setters = extract_property_setters(info, params);

  Depsgraph *depsgraph = const_cast<Depsgraph *>(params.depsgraph());
  if (info.needs_depsgraph && depsgraph == nullptr) {
    params.error_message_add(NodeWarningType::Error, TIP_("The scene time is not available"));
    params.set_output("Mesh", std::move(geometry_set));
    return;
  }

  Object *self_object = const_cast<Object *>(params.self_object());
  Object *temp_object = nullptr;
  if (self_object == nullptr) {
    temp_object = static_cast<Object *>(BKE_id_new_nomain(ID_OB, "Modifier Node"));
    self_object = temp_object;
  }

  const ModifierTypeInfo &mti = *BKE_modifier_get_info(info.type);
  std::mutex messages_mutex;
  Set<std::string> messages;
  std::atomic<bool> is_disabled = false;

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr) {
      return;
    }
    ModifierData *md = BKE_modifier_new(info.type);
    /* Not part of an object, so that errors don't look for the original modifier. */
    md->mode |= eModifierMode_Virtual;
    PointerRNA ptr = RNA_pointer_create_discrete(&self_object->id, *info.srna, md);
    for (const PropertySetter &setter : setters) {
      setter(ptr);
    }
    for (const int i : IndexRange(info.objects.size())) {
      Object *object = objects[i];
      info.objects[i].set(*md, object == self_object ? nullptr : object);
    }
    if (weight_field) {
      info.set_vgroup(*md, weight_group_name);
    }

    if (mti.is_disabled && mti.is_disabled(nullptr, md, false)) {
      is_disabled = true;
      BKE_modifier_free(md);
      return;
    }

    Mesh *mesh = BKE_mesh_copy_for_eval(*src_mesh);
    if (weight_field) {
      add_weight_group(*mesh, *weight_field);
    }
    if (radius_field) {
      add_skin_data(*mesh, *radius_field);
    }

    ModifierEvalContext ctx{depsgraph, self_object, ModifierApplyFlag(0)};
    Mesh *result = run_modifier(mti, *md, ctx, mesh);
    if (result != nullptr) {
      if (weight_field) {
        remove_weight_group(*result);
      }
      if (radius_field && result != mesh) {
        CustomData_free_layers(&result->vert_data, CD_MVERT_SKIN);
      }
      geometry::debug_randomize_mesh_order(result);
    }
    geometry_set.replace_mesh(result);

    if (md->error) {
      std::lock_guard lock{messages_mutex};
      messages.add(std::string(md->error));
    }
    BKE_modifier_free(md);
  });

  if (temp_object) {
    BKE_id_free(nullptr, temp_object);
  }
  if (is_disabled) {
    params.error_message_add(NodeWarningType::Info,
                             TIP_("Disabled, an object or setting it needs is missing"));
  }
  for (const std::string &message : messages) {
    params.error_message_add(NodeWarningType::Warning, message);
  }
  params.set_output("Mesh", std::move(geometry_set));
}

template<int I> static void node_declare(NodeDeclarationBuilder &b)
{
  declare_from_info(node_infos()[I], b);
}

template<int I> static void node_geo_exec(GeoNodeExecParams params)
{
  exec_from_info(node_infos()[I], params);
}

template<int I> static void register_one()
{
  const ModifierNodeInfo &info = node_infos()[I];
  static blender::bke::bNodeType ntype;
  geo_node_type_base(&ntype, info.idname);
  ntype.ui_name = info.ui_name;
  ntype.ui_description = info.description;
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare<I>;
  ntype.geometry_node_execute = node_geo_exec<I>;
  blender::bke::node_register_type(ntype);
}

template<int... Is> static void register_all(std::integer_sequence<int, Is...> /*indices*/)
{
  (register_one<Is>(), ...);
}

static void node_register()
{
  BLI_assert(int(node_infos().size()) == NODES_NUM);
  register_all(std::make_integer_sequence<int, NODES_NUM>());
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_modifier_nodes_cc
