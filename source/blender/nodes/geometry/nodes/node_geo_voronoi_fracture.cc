/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"
#include "DNA_pointcloud_types.h"

#include "BLI_math_matrix.hh"

#include "BKE_curves.hh"
#include "BKE_instances.hh"
#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_voronoi_fracture.hh"
#include "GEO_randomize.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_voronoi_fracture_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Closed mesh to cut into pieces");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  b.add_input<decl::Geometry>("Cell Points")
      .description(
          "Each point becomes the center of a cell, which makes one piece. More points in an area "
          "make smaller pieces there");
  b.add_input<decl::Float>("Cut Gap")
      .default_value(0.0f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Distance between the inside faces of neighboring pieces");
  b.add_input<decl::Bool>("Separate Islands")
      .default_value(true)
      .description(
          "Make a separate piece of every part of a cell that isn't connected to the rest of it");

  b.add_output<decl::Int>("Piece Index")
      .field_on_all()
      .description("Number of the piece, which can be used to split the mesh into the pieces");
  b.add_output<decl::Int>("Cell Index")
      .field_on_all()
      .description("Index of the point in the cell points that this piece was made from");
  b.add_output<decl::Bool>("Interior")
      .field_on_all()
      .description("The faces that were made by the cuts");
}

static Vector<float3> gather_cell_points(const GeometrySet &geometry)
{
  Vector<float3> points;
  if (const PointCloud *pointcloud = geometry.get_pointcloud()) {
    points.extend(pointcloud->positions());
  }
  if (const Mesh *mesh = geometry.get_mesh()) {
    points.extend(mesh->vert_positions());
  }
  if (const Curves *curves = geometry.get_curves()) {
    points.extend(curves->geometry.wrap().positions());
  }
  if (const bke::Instances *instances = geometry.get_instances()) {
    for (const float4x4 &transform : instances->transforms()) {
      points.append(transform.location());
    }
  }
  return points;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const GeometrySet points_geometry = params.extract_input<GeometrySet>("Cell Points");

  const Vector<float3> points = gather_cell_points(points_geometry);
  if (points.is_empty()) {
    params.error_message_add(NodeWarningType::Info, TIP_("There are no cell points"));
    params.set_output("Mesh", std::move(geometry_set));
    return;
  }

  geometry::MeshVoronoiFractureParams fracture_params;
  fracture_params.cut_gap = params.extract_input<float>("Cut Gap");
  fracture_params.separate_islands = params.extract_input<bool>("Separate Islands");
  fracture_params.piece_index_id = params.get_output_anonymous_attribute_id_if_needed(
      "Piece Index");
  fracture_params.cell_index_id = params.get_output_anonymous_attribute_id_if_needed("Cell Index");
  fracture_params.interior_id = params.get_output_anonymous_attribute_id_if_needed("Interior");

  std::atomic<bool> has_non_manifold = false;
  std::atomic<bool> has_error = false;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->faces_num == 0) {
      return;
    }
    geometry::MeshVoronoiFractureError error = geometry::MeshVoronoiFractureError::NoError;
    Mesh *result = geometry::mesh_voronoi_fracture(*src_mesh, points, fracture_params, &error);
    if (result == nullptr) {
      if (error == geometry::MeshVoronoiFractureError::NonManifold) {
        has_non_manifold = true;
      }
      else {
        has_error = true;
      }
      return;
    }
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });

  if (has_non_manifold) {
    params.error_message_add(
        NodeWarningType::Error,
        TIP_("The mesh must be closed and manifold, because the pieces are filled with its inside"));
  }
  if (has_error) {
    params.error_message_add(NodeWarningType::Error, TIP_("The mesh could not be fractured"));
  }
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeVoronoiFracture");
  ntype.ui_name = "Voronoi Fracture";
  ntype.ui_description =
      "Cut a closed mesh into solid pieces, one for the area around each of the given points";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_voronoi_fracture_cc
