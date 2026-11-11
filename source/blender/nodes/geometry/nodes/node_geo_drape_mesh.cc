/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <mutex>

#include "DNA_mesh_types.h"
#include "DNA_object_types.h"

#include "BLI_array.hh"
#include "BLI_math_matrix.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_drape.hh"
#include "GEO_realize_instances.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_drape_mesh_cc {

enum class Space {
  Object = 0,
  World = 1,
};

static const EnumPropertyItem space_items[] = {
    {int(Space::Object),
     "OBJECT",
     ICON_NONE,
     N_("Object"),
     N_("Fall along the -Z axis of the object, so down turns with it")},
    {int(Space::World),
     "WORLD",
     ICON_NONE,
     N_("World"),
     N_("Fall along the -Z axis of the world, so down stays down when the object turns")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh that falls as cloth");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  b.add_input<decl::Geometry>("Collider")
      .supported_type({GeometryComponent::Type::Mesh, GeometryComponent::Type::Instance})
      .description("Meshes the cloth lands on. Instances, like a whole collection, are used too");
  b.add_input<decl::Float>("Cage")
      .subtype(PROP_DISTANCE)
      .description("Push the collider out along its normals by this much, so the cloth rests "
                   "further away. Negative pulls it in");
  b.add_input<decl::Bool>("Floor").default_value(true).description(
      "Land on a flat floor under the lowest point of the cloth and the collider");
  b.add_input<decl::Bool>("Pin")
      .default_value(false)
      .hide_value()
      .field_on_all()
      .description("Points that stay where they are");
  b.add_input<decl::Menu>("Space")
      .static_items(space_items)
      .default_value(Space::World)
      .description("Which way is down");
  b.add_input<decl::Float>("Time")
      .default_value(2.0f)
      .min(0.0f)
      .max(60.0f)
      .subtype(PROP_TIME_ABSOLUTE)
      .description("Seconds the cloth falls and settles for");
  b.add_input<decl::Float>("Gravity")
      .default_value(9.81f)
      .min(0.0f)
      .subtype(PROP_ACCELERATION)
      .description("How hard the cloth is pulled down");
  b.add_input<decl::Float>("Stiffness")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much the cloth resists stretching");
  b.add_input<decl::Float>("Bending")
      .default_value(0.1f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much the cloth resists folding, from silk at 0 to leather near 1");
  b.add_input<decl::Float>("Shrink")
      .default_value(0.0f)
      .min(-1.0f)
      .max(0.95f)
      .subtype(PROP_FACTOR)
      .description("Pull the cloth taut between the pins. Negative values give it slack");
  b.add_input<decl::Float>("Pressure")
      .default_value(0.0f)
      .description("Puff up closed meshes, like cushions and pillows");
  b.add_input<decl::Float>("Thickness")
      .default_value(0.01f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Distance the cloth keeps from what it touches");
  b.add_input<decl::Float>("Friction")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much the cloth grips what it touches instead of sliding off");
  b.add_input<decl::Bool>("Self Collision")
      .default_value(false)
      .description("Keep folds from passing through each other. Slower");
  b.add_input<decl::Int>("Quality")
      .default_value(8)
      .min(1)
      .max(100)
      .description("Solver steps per 1/60 second. Higher keeps the cloth from stretching and from "
                   "passing through thin colliders");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  GeometrySet collider_set = params.extract_input<GeometrySet>("Collider");
  if (collider_set.has_instances()) {
    collider_set = geometry::realize_instances(collider_set,
                                               geometry::RealizeInstancesOptions())
                       .geometry;
  }
  const Field<bool> pin_field = params.extract_input<Field<bool>>("Pin");
  const Space space = params.extract_input<Space>("Space");

  geometry::MeshDrapeParams settings;
  settings.floor = params.extract_input<bool>("Floor");
  settings.time = params.extract_input<float>("Time");
  settings.gravity = params.extract_input<float>("Gravity");
  settings.stiffness = params.extract_input<float>("Stiffness");
  settings.bending = params.extract_input<float>("Bending");
  settings.shrink = params.extract_input<float>("Shrink");
  settings.pressure = params.extract_input<float>("Pressure");
  settings.thickness = params.extract_input<float>("Thickness");
  settings.friction = params.extract_input<float>("Friction");
  settings.self_collision = params.extract_input<bool>("Self Collision");
  settings.substeps = params.extract_input<int>("Quality");

  const Object *self_object = params.self_object();
  if (space == Space::World && self_object != nullptr) {
    settings.mesh_to_space = self_object->object_to_world();
  }
  /* The collider comes in the same space as the mesh. */
  settings.collider = collider_set.get_mesh();
  settings.collider_to_space = settings.mesh_to_space;
  settings.collider_cage = params.extract_input<float>("Cage");

  std::mutex error_mutex;
  const char *error = nullptr;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->verts_num == 0) {
      return;
    }
    geometry::MeshDrapeParams mesh_settings = settings;
    Array<bool> pin(src_mesh->verts_num);
    const bke::MeshFieldContext context{*src_mesh, AttrDomain::Point};
    FieldEvaluator evaluator{context, src_mesh->verts_num};
    evaluator.add_with_destination(pin_field, pin.as_mutable_span());
    evaluator.evaluate();
    mesh_settings.pin = pin;

    const char *mesh_error = nullptr;
    Mesh *result = geometry::mesh_drape(*src_mesh, mesh_settings, &mesh_error);
    if (mesh_error != nullptr) {
      std::lock_guard lock{error_mutex};
      error = mesh_error;
    }
    if (result != nullptr) {
      geometry_set.replace_mesh(result);
    }
  });

  if (error != nullptr) {
    params.error_message_add(NodeWarningType::Error, TIP_(error));
  }
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeDrapeMesh");
  ntype.ui_name = "Drape";
  ntype.ui_description = "Let a mesh fall as cloth and keep the shape it rests in";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_drape_mesh_cc
