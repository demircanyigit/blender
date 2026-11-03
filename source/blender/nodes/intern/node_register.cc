/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "NOD_composite.hh"
#include "NOD_geometry.hh"
#include "NOD_register.hh"
#include "NOD_socket.hh"

#include "BKE_node.hh"
#include "BKE_node_legacy_types.hh"

#include "BLT_translation.hh"

#include "RNA_define.hh"

#include "UI_resources.hh"

namespace blender {

static bool node_undefined_poll(const bke::bNodeType * /*ntype*/,
                                const bNodeTree * /*nodetree*/,
                                const char ** /*r_disabled_hint*/)
{
  /* this type can not be added deliberately, it's just a placeholder */
  return false;
}

/* register fallback types used for undefined tree, nodes, sockets */
static void register_undefined_types()
{
  /* NOTE: these types are not registered in the type hashes,
   * they are just used as placeholders in case the actual types are not registered.
   */

  bke::NodeTreeTypeUndefined.type = NTREE_UNDEFINED;
  bke::NodeTreeTypeUndefined.idname = "NodeTreeUndefined";
  bke::NodeTreeTypeUndefined.ui_name = N_("Undefined");
  bke::NodeTreeTypeUndefined.ui_description = N_("Undefined Node Tree Type");

  bke::node_type_base_custom(bke::NodeTypeUndefined, "NodeUndefined", "Undefined", "UNDEFINED", 0);
  bke::NodeTypeUndefined.poll = node_undefined_poll;

  bke::NodeSocketTypeUndefined.idname = "NodeSocketUndefined";
  /* extra type info for standard socket types */
  bke::NodeSocketTypeUndefined.type = SOCK_CUSTOM;
  bke::NodeSocketTypeUndefined.subtype = PROP_NONE;

  bke::NodeSocketTypeUndefined.use_link_limits_of_type = true;
  bke::NodeSocketTypeUndefined.input_link_limit = 0xFFF;
  bke::NodeSocketTypeUndefined.output_link_limit = 0xFFF;
}

class SimulationZoneType : public bke::bNodeZoneType {
 public:
  SimulationZoneType()
  {
    this->input_idname = "GeometryNodeSimulationInput";
    this->output_idname = "GeometryNodeSimulationOutput";
    this->input_type = GEO_NODE_SIMULATION_INPUT;
    this->output_type = GEO_NODE_SIMULATION_OUTPUT;
    this->theme_id = TH_NODE_ZONE_SIMULATION;
  }

  const int &get_corresponding_output_id(const bNode &input_bnode) const override
  {
    BLI_assert(input_bnode.type_legacy == this->input_type);
    return static_cast<NodeGeometrySimulationInput *>(input_bnode.storage)->output_node_id;
  }
};

class RepeatZoneType : public bke::bNodeZoneType {
 public:
  RepeatZoneType()
  {
    this->input_idname = "GeometryNodeRepeatInput";
    this->output_idname = "GeometryNodeRepeatOutput";
    this->input_type = GEO_NODE_REPEAT_INPUT;
    this->output_type = GEO_NODE_REPEAT_OUTPUT;
    this->theme_id = TH_NODE_ZONE_REPEAT;
  }

  const int &get_corresponding_output_id(const bNode &input_bnode) const override
  {
    BLI_assert(input_bnode.type_legacy == this->input_type);
    return static_cast<NodeGeometryRepeatInput *>(input_bnode.storage)->output_node_id;
  }
};

class ForeachGeometryElementZoneType : public bke::bNodeZoneType {
 public:
  ForeachGeometryElementZoneType()
  {
    this->input_idname = "GeometryNodeForeachGeometryElementInput";
    this->output_idname = "GeometryNodeForeachGeometryElementOutput";
    this->input_type = GEO_NODE_FOREACH_GEOMETRY_ELEMENT_INPUT;
    this->output_type = GEO_NODE_FOREACH_GEOMETRY_ELEMENT_OUTPUT;
    this->theme_id = TH_NODE_ZONE_FOREACH_GEOMETRY_ELEMENT;
  }

  const int &get_corresponding_output_id(const bNode &input_bnode) const override
  {
    BLI_assert(input_bnode.type_legacy == this->input_type);
    return static_cast<NodeGeometryForeachGeometryElementInput *>(input_bnode.storage)
        ->output_node_id;
  }
};

class ClosureZoneType : public bke::bNodeZoneType {
 public:
  ClosureZoneType()
  {
    this->input_idname = "NodeClosureInput";
    this->output_idname = "NodeClosureOutput";
    this->input_type = NODE_CLOSURE_INPUT;
    this->output_type = NODE_CLOSURE_OUTPUT;
    this->theme_id = TH_NODE_ZONE_CLOSURE;
  }

  const int &get_corresponding_output_id(const bNode &input_bnode) const override
  {
    BLI_assert(input_bnode.type_legacy == this->input_type);
    return static_cast<NodeClosureInput *>(input_bnode.storage)->output_node_id;
  }
};

/* Show geometry nodes that do a modeling operation with that operation's icon, recolored, and the
 * others with an icon drawn for them (`GEO_N_*`). */
static void set_geometry_node_icons()
{
  const struct {
    const char *idname;
    int icon;
  } node_icons[] = {
      {"GeometryNodeBevelMesh", ICON_GEO_MOD_BEVEL},
      {"GeometryNodeClipMesh", ICON_GEO_BISECT},
      {"GeometryNodeCollectionInfo", ICON_GEO_OUTLINER_COLLECTION},
      {"GeometryNodeCurvePrimitiveBezierSegment", ICON_GEO_CURVE_BEZCURVE},
      {"GeometryNodeCurvePrimitiveCircle", ICON_GEO_CURVE_NCIRCLE},
      {"GeometryNodeCurveToMesh", ICON_GEO_OUTLINER_OB_MESH},
      {"GeometryNodeCurveToPoints", ICON_GEO_OUTLINER_OB_POINTCLOUD},
      {"GeometryNodeDeleteGeometry", ICON_GEO_TRASH},
      {"GeometryNodeDiceMesh", ICON_GEO_KNIFE},
      {"GeometryNodeDisplaceMesh", ICON_GEO_MOD_DISPLACE},
      {"GeometryNodeDissolveMesh", ICON_GEO_MOD_DECIM},
      {"GeometryNodeDuplicateElements", ICON_GEO_DUPLICATE},
      {"GeometryNodeExtrudeMesh", ICON_GEO_EXTRUDE},
      {"GeometryNodeFlipFaces", ICON_GEO_NORMALS_FACE},
      {"GeometryNodeInstanceOnPoints", ICON_GEO_MOD_PARTICLE_INSTANCE},
      {"GeometryNodeIsotropicRemesh", ICON_GEO_MOD_REMESH},
      {"GeometryNodeMergeByDistance", ICON_GEO_AUTOMERGE_OFF},
      {"GeometryNodeMeshBoolean", ICON_GEO_MOD_BOOLEAN},
      {"GeometryNodeMeshCircle", ICON_GEO_MESH_CIRCLE},
      {"GeometryNodeMeshCone", ICON_GEO_MESH_CONE},
      {"GeometryNodeMeshCube", ICON_GEO_MESH_CUBE},
      {"GeometryNodeMeshCylinder", ICON_GEO_MESH_CYLINDER},
      {"GeometryNodeMeshGrid", ICON_GEO_MESH_GRID},
      {"GeometryNodeMeshIcoSphere", ICON_GEO_MESH_ICOSPHERE},
      {"GeometryNodeMeshToCurve", ICON_GEO_OUTLINER_OB_CURVE},
      {"GeometryNodeMeshToPoints", ICON_GEO_OUTLINER_OB_POINTCLOUD},
      {"GeometryNodeMeshToVolume", ICON_GEO_OUTLINER_OB_VOLUME},
      {"GeometryNodeMeshUVSphere", ICON_GEO_MESH_UVSPHERE},
      {"GeometryNodeObjectInfo", ICON_GEO_OBJECT_DATA},
      {"GeometryNodePointsToCurves", ICON_GEO_OUTLINER_OB_CURVES},
      {"GeometryNodePointsToVolume", ICON_GEO_OUTLINER_OB_VOLUME},
      {"GeometryNodeQuadriFlowRemesh", ICON_GEO_MOD_REMESH},
      {"GeometryNodeRelaxMesh", ICON_GEO_SMOOTH_VERTICES},
      {"GeometryNodeResampleMesh", ICON_GEO_RESAMPLE},
      {"GeometryNodeScaleElements", ICON_GEO_RESIZE},
      {"GeometryNodeSetMaterial", ICON_GEO_MATERIAL},
      {"GeometryNodeSetPosition", ICON_GEO_TRANSLATE},
      {"GeometryNodeSmoothMesh", ICON_GEO_MOD_SMOOTH},
      {"GeometryNodeSplitEdges", ICON_GEO_MOD_EDGESPLIT},
      {"GeometryNodeStringToCurves", ICON_GEO_OUTLINER_OB_FONT},
      {"GeometryNodeSubdivisionSurface", ICON_GEO_MOD_SUBSURF},
      {"GeometryNodeSweep", ICON_GEO_MOD_SCREW},
      {"GeometryNodeTransform", ICON_GEO_TRANSFORM},
      {"GeometryNodeTriangulate", ICON_GEO_MOD_TRIANGULATE},
      {"GeometryNodeUVUnwrap", ICON_GEO_UV},
      {"GeometryNodeVolumeToMesh", ICON_GEO_OUTLINER_OB_MESH},
      {"GeometryNodeVoronoiFracture", ICON_GEO_MOD_EXPLODE},
      {"FunctionNodeAlignEulerToVector", ICON_GEO_N_ALIGN_EULER_TO_VECTOR},
      {"FunctionNodeAlignRotationToVector", ICON_GEO_N_ALIGN_ROTATION_TO_VECTOR},
      {"FunctionNodeAxesToRotation", ICON_GEO_N_AXES_TO_ROTATION},
      {"FunctionNodeAxisAngleToRotation", ICON_GEO_N_AXIS_ANGLE_TO_ROTATION},
      {"FunctionNodeBitMath", ICON_GEO_N_BIT_MATH},
      {"FunctionNodeBooleanMath", ICON_GEO_N_BOOLEAN_MATH},
      {"FunctionNodeCombineColor", ICON_GEO_N_COMBINE_COLOR},
      {"FunctionNodeCombineMatrix", ICON_GEO_N_COMBINE_MATRIX},
      {"FunctionNodeCombineTransform", ICON_GEO_N_COMBINE_TRANSFORM},
      {"FunctionNodeCompare", ICON_GEO_N_COMPARE},
      {"FunctionNodeEulerToRotation", ICON_GEO_N_EULER_TO_ROTATION},
      {"FunctionNodeFindInString", ICON_GEO_N_FIND_IN_STRING},
      {"FunctionNodeFloatToInt", ICON_GEO_N_FLOAT_TO_INT},
      {"FunctionNodeFormatString", ICON_GEO_N_FORMAT_STRING},
      {"FunctionNodeHashValue", ICON_GEO_N_HASH_VALUE},
      {"FunctionNodeInputBool", ICON_GEO_N_INPUT_BOOL},
      {"FunctionNodeInputColor", ICON_GEO_N_INPUT_COLOR},
      {"FunctionNodeInputInt", ICON_GEO_N_INPUT_INT},
      {"FunctionNodeInputRotation", ICON_GEO_N_INPUT_ROTATION},
      {"FunctionNodeInputSpecialCharacters", ICON_GEO_N_INPUT_SPECIAL_CHARACTERS},
      {"FunctionNodeInputString", ICON_GEO_N_INPUT_STRING},
      {"FunctionNodeInputVector", ICON_GEO_N_INPUT_VECTOR},
      {"FunctionNodeIntegerMath", ICON_GEO_N_INTEGER_MATH},
      {"FunctionNodeInvertMatrix", ICON_GEO_N_INVERT_MATRIX},
      {"FunctionNodeInvertRotation", ICON_GEO_N_INVERT_ROTATION},
      {"FunctionNodeMatchString", ICON_GEO_N_MATCH_STRING},
      {"FunctionNodeMatrixDeterminant", ICON_GEO_N_MATRIX_DETERMINANT},
      {"FunctionNodeMatrixMultiply", ICON_GEO_N_MATRIX_MULTIPLY},
      {"FunctionNodeMatrixSVD", ICON_GEO_N_MATRIX_SVD},
      {"FunctionNodeProjectPoint", ICON_GEO_N_PROJECT_POINT},
      {"FunctionNodeQuaternionToRotation", ICON_GEO_N_QUATERNION_TO_ROTATION},
      {"FunctionNodeRandomValue", ICON_GEO_N_RANDOM_VALUE},
      {"FunctionNodeReplaceString", ICON_GEO_N_REPLACE_STRING},
      {"FunctionNodeRotateEuler", ICON_GEO_N_ROTATE_EULER},
      {"FunctionNodeRotateRotation", ICON_GEO_N_ROTATE_ROTATION},
      {"FunctionNodeRotateVector", ICON_GEO_N_ROTATE_VECTOR},
      {"FunctionNodeRotationToAxisAngle", ICON_GEO_N_ROTATION_TO_AXIS_ANGLE},
      {"FunctionNodeRotationToEuler", ICON_GEO_N_ROTATION_TO_EULER},
      {"FunctionNodeRotationToQuaternion", ICON_GEO_N_ROTATION_TO_QUATERNION},
      {"FunctionNodeSeparateColor", ICON_GEO_N_SEPARATE_COLOR},
      {"FunctionNodeSeparateMatrix", ICON_GEO_N_SEPARATE_MATRIX},
      {"FunctionNodeSeparateTransform", ICON_GEO_N_SEPARATE_TRANSFORM},
      {"FunctionNodeSliceString", ICON_GEO_N_SLICE_STRING},
      {"FunctionNodeStringLength", ICON_GEO_N_STRING_LENGTH},
      {"FunctionNodeStringToValue", ICON_GEO_N_STRING_TO_VALUE},
      {"FunctionNodeTransformDirection", ICON_GEO_N_TRANSFORM_DIRECTION},
      {"FunctionNodeTransformPoint", ICON_GEO_N_TRANSFORM_POINT},
      {"FunctionNodeTransposeMatrix", ICON_GEO_N_TRANSPOSE_MATRIX},
      {"FunctionNodeValueToString", ICON_GEO_N_VALUE_TO_STRING},
      {"GeometryNodeAccumulateField", ICON_GEO_N_ACCUMULATE_FIELD},
      {"GeometryNodeAttributeDomainSize", ICON_GEO_N_ATTRIBUTE_DOMAIN_SIZE},
      {"GeometryNodeAttributeStatistic", ICON_GEO_N_ATTRIBUTE_STATISTIC},
      {"GeometryNodeBake", ICON_GEO_N_BAKE},
      {"GeometryNodeBlurAttribute", ICON_GEO_N_BLUR_ATTRIBUTE},
      {"GeometryNodeBoneInfo", ICON_GEO_N_BONE_INFO},
      {"GeometryNodeBoundBox", ICON_GEO_N_BOUND_BOX},
      {"GeometryNodeCameraInfo", ICON_GEO_N_CAMERA_INFO},
      {"GeometryNodeCaptureAttribute", ICON_GEO_N_CAPTURE_ATTRIBUTE},
      {"GeometryNodeConvexHull", ICON_GEO_N_CONVEX_HULL},
      {"GeometryNodeCornersOfEdge", ICON_GEO_N_CORNERS_OF_EDGE},
      {"GeometryNodeCornersOfFace", ICON_GEO_N_CORNERS_OF_FACE},
      {"GeometryNodeCornersOfVertex", ICON_GEO_N_CORNERS_OF_VERTEX},
      {"GeometryNodeCubeGridTopology", ICON_GEO_N_CUBE_GRID_TOPOLOGY},
      {"GeometryNodeCurveArc", ICON_GEO_N_CURVE_ARC},
      {"GeometryNodeCurveEndpointSelection", ICON_GEO_N_CURVE_ENDPOINT_SELECTION},
      {"GeometryNodeCurveHandleTypeSelection", ICON_GEO_N_CURVE_HANDLE_TYPE_SELECTION},
      {"GeometryNodeCurveLength", ICON_GEO_N_CURVE_LENGTH},
      {"GeometryNodeCurveOfPoint", ICON_GEO_N_CURVE_OF_POINT},
      {"GeometryNodeCurvePrimitiveLine", ICON_GEO_N_CURVE_PRIMITIVE_LINE},
      {"GeometryNodeCurvePrimitiveQuadrilateral", ICON_GEO_N_CURVE_PRIMITIVE_QUADRILATERAL},
      {"GeometryNodeCurveQuadraticBezier", ICON_GEO_N_CURVE_QUADRATIC_BEZIER},
      {"GeometryNodeCurveSetHandles", ICON_GEO_N_CURVE_SET_HANDLES},
      {"GeometryNodeCurveSpiral", ICON_GEO_N_CURVE_SPIRAL},
      {"GeometryNodeCurveSplineType", ICON_GEO_N_CURVE_SPLINE_TYPE},
      {"GeometryNodeCurveStar", ICON_GEO_N_CURVE_STAR},
      {"GeometryNodeCurvesToGreasePencil", ICON_GEO_N_CURVES_TO_GREASE_PENCIL},
      {"GeometryNodeDecimateMesh", ICON_GEO_N_DECIMATE_MESH},
      {"GeometryNodeDeformCurvesOnSurface", ICON_GEO_N_DEFORM_CURVES_ON_SURFACE},
      {"GeometryNodeDistributePointsInGrid", ICON_GEO_N_DISTRIBUTE_POINTS_IN_GRID},
      {"GeometryNodeDistributePointsInVolume", ICON_GEO_N_DISTRIBUTE_POINTS_IN_VOLUME},
      {"GeometryNodeDistributePointsOnFaces", ICON_GEO_N_DISTRIBUTE_POINTS_ON_FACES},
      {"GeometryNodeDualMesh", ICON_GEO_N_DUAL_MESH},
      {"GeometryNodeEdgePathsToCurves", ICON_GEO_N_EDGE_PATHS_TO_CURVES},
      {"GeometryNodeEdgePathsToSelection", ICON_GEO_N_EDGE_PATHS_TO_SELECTION},
      {"GeometryNodeEdgesOfCorner", ICON_GEO_N_EDGES_OF_CORNER},
      {"GeometryNodeEdgesOfVertex", ICON_GEO_N_EDGES_OF_VERTEX},
      {"GeometryNodeEdgesToFaceGroups", ICON_GEO_N_EDGES_TO_FACE_GROUPS},
      {"GeometryNodeFaceOfCorner", ICON_GEO_N_FACE_OF_CORNER},
      {"GeometryNodeFieldAtIndex", ICON_GEO_N_FIELD_AT_INDEX},
      {"GeometryNodeFieldAverage", ICON_GEO_N_FIELD_AVERAGE},
      {"GeometryNodeFieldMinAndMax", ICON_GEO_N_FIELD_MIN_AND_MAX},
      {"GeometryNodeFieldOnDomain", ICON_GEO_N_FIELD_ON_DOMAIN},
      {"GeometryNodeFieldToGrid", ICON_GEO_N_FIELD_TO_GRID},
      {"GeometryNodeFieldToList", ICON_GEO_N_FIELD_TO_LIST},
      {"GeometryNodeFieldVariance", ICON_GEO_N_FIELD_VARIANCE},
      {"GeometryNodeFillCurve", ICON_GEO_N_FILL_CURVE},
      {"GeometryNodeFilletCurve", ICON_GEO_N_FILLET_CURVE},
      {"GeometryNodeGeometryToInstance", ICON_GEO_N_GEOMETRY_TO_INSTANCE},
      {"GeometryNodeGetGeometryBundle", ICON_GEO_N_GET_GEOMETRY_BUNDLE},
      {"GeometryNodeGetNamedGrid", ICON_GEO_N_GET_NAMED_GRID},
      {"GeometryNodeGizmoDial", ICON_GEO_N_GIZMO_DIAL},
      {"GeometryNodeGizmoLinear", ICON_GEO_N_GIZMO_LINEAR},
      {"GeometryNodeGizmoTransform", ICON_GEO_N_GIZMO_TRANSFORM},
      {"GeometryNodeGreasePencilToCurves", ICON_GEO_N_GREASE_PENCIL_TO_CURVES},
      {"GeometryNodeGridAdvect", ICON_GEO_N_GRID_ADVECT},
      {"GeometryNodeGridClip", ICON_GEO_N_GRID_CLIP},
      {"GeometryNodeGridCurl", ICON_GEO_N_GRID_CURL},
      {"GeometryNodeGridDilateAndErode", ICON_GEO_N_GRID_DILATE_AND_ERODE},
      {"GeometryNodeGridDivergence", ICON_GEO_N_GRID_DIVERGENCE},
      {"GeometryNodeGridGradient", ICON_GEO_N_GRID_GRADIENT},
      {"GeometryNodeGridInfo", ICON_GEO_N_GRID_INFO},
      {"GeometryNodeGridLaplacian", ICON_GEO_N_GRID_LAPLACIAN},
      {"GeometryNodeGridMean", ICON_GEO_N_GRID_MEAN},
      {"GeometryNodeGridMedian", ICON_GEO_N_GRID_MEDIAN},
      {"GeometryNodeGridPrune", ICON_GEO_N_GRID_PRUNE},
      {"GeometryNodeGridToMesh", ICON_GEO_N_GRID_TO_MESH},
      {"GeometryNodeGridToPoints", ICON_GEO_N_GRID_TO_POINTS},
      {"GeometryNodeGridVoxelize", ICON_GEO_N_GRID_VOXELIZE},
      {"GeometryNodeImageInfo", ICON_GEO_N_IMAGE_INFO},
      {"GeometryNodeImageTexture", ICON_GEO_N_IMAGE_TEXTURE},
      {"GeometryNodeImportCSV", ICON_GEO_N_IMPORT_CSV},
      {"GeometryNodeImportOBJ", ICON_GEO_N_IMPORT_OBJ},
      {"GeometryNodeImportPLY", ICON_GEO_N_IMPORT_PLY},
      {"GeometryNodeImportSTL", ICON_GEO_N_IMPORT_STL},
      {"GeometryNodeImportText", ICON_GEO_N_IMPORT_TEXT},
      {"GeometryNodeImportVDB", ICON_GEO_N_IMPORT_VDB},
      {"GeometryNodeIndexOfNearest", ICON_GEO_N_INDEX_OF_NEAREST},
      {"GeometryNodeIndexSwitch", ICON_GEO_N_INDEX_SWITCH},
      {"GeometryNodeInputActiveCamera", ICON_GEO_N_INPUT_ACTIVE_CAMERA},
      {"GeometryNodeInputCollection", ICON_GEO_N_INPUT_COLLECTION},
      {"GeometryNodeInputCurveHandlePositions", ICON_GEO_N_INPUT_CURVE_HANDLE_POSITIONS},
      {"GeometryNodeInputCurveTilt", ICON_GEO_N_INPUT_CURVE_TILT},
      {"GeometryNodeInputEdgeSmooth", ICON_GEO_N_INPUT_EDGE_SMOOTH},
      {"GeometryNodeInputID", ICON_GEO_N_INPUT_ID},
      {"GeometryNodeInputImage", ICON_GEO_N_INPUT_IMAGE},
      {"GeometryNodeInputIndex", ICON_GEO_N_INPUT_INDEX},
      {"GeometryNodeInputInstanceBounds", ICON_GEO_N_INPUT_INSTANCE_BOUNDS},
      {"GeometryNodeInputInstanceRotation", ICON_GEO_N_INPUT_INSTANCE_ROTATION},
      {"GeometryNodeInputInstanceScale", ICON_GEO_N_INPUT_INSTANCE_SCALE},
      {"GeometryNodeInputMaterial", ICON_GEO_N_INPUT_MATERIAL},
      {"GeometryNodeInputMaterialIndex", ICON_GEO_N_INPUT_MATERIAL_INDEX},
      {"GeometryNodeInputMeshEdgeAngle", ICON_GEO_N_INPUT_MESH_EDGE_ANGLE},
      {"GeometryNodeInputMeshEdgeNeighbors", ICON_GEO_N_INPUT_MESH_EDGE_NEIGHBORS},
      {"GeometryNodeInputMeshEdgeVertices", ICON_GEO_N_INPUT_MESH_EDGE_VERTICES},
      {"GeometryNodeInputMeshFaceArea", ICON_GEO_N_INPUT_MESH_FACE_AREA},
      {"GeometryNodeInputMeshFaceIsPlanar", ICON_GEO_N_INPUT_MESH_FACE_IS_PLANAR},
      {"GeometryNodeInputMeshFaceNeighbors", ICON_GEO_N_INPUT_MESH_FACE_NEIGHBORS},
      {"GeometryNodeInputMeshIsland", ICON_GEO_N_INPUT_MESH_ISLAND},
      {"GeometryNodeInputMeshVertexNeighbors", ICON_GEO_N_INPUT_MESH_VERTEX_NEIGHBORS},
      {"GeometryNodeInputNamedAttribute", ICON_GEO_N_INPUT_NAMED_ATTRIBUTE},
      {"GeometryNodeInputNamedLayerSelection", ICON_GEO_N_INPUT_NAMED_LAYER_SELECTION},
      {"GeometryNodeInputNormal", ICON_GEO_N_INPUT_NORMAL},
      {"GeometryNodeInputObject", ICON_GEO_N_INPUT_OBJECT},
      {"GeometryNodeInputPosition", ICON_GEO_N_INPUT_POSITION},
      {"GeometryNodeInputRadius", ICON_GEO_N_INPUT_RADIUS},
      {"GeometryNodeInputSceneTime", ICON_GEO_N_INPUT_SCENE_TIME},
      {"GeometryNodeInputShadeSmooth", ICON_GEO_N_INPUT_SHADE_SMOOTH},
      {"GeometryNodeInputShortestEdgePaths", ICON_GEO_N_INPUT_SHORTEST_EDGE_PATHS},
      {"GeometryNodeInputSplineCyclic", ICON_GEO_N_INPUT_SPLINE_CYCLIC},
      {"GeometryNodeInputSplineResolution", ICON_GEO_N_INPUT_SPLINE_RESOLUTION},
      {"GeometryNodeInputTangent", ICON_GEO_N_INPUT_TANGENT},
      {"GeometryNodeInputVoxelIndex", ICON_GEO_N_INPUT_VOXEL_INDEX},
      {"GeometryNodeInstanceTransform", ICON_GEO_N_INSTANCE_TRANSFORM},
      {"GeometryNodeInstancesToPoints", ICON_GEO_N_INSTANCES_TO_POINTS},
      {"GeometryNodeInterpolateCurves", ICON_GEO_N_INTERPOLATE_CURVES},
      {"GeometryNodeIsViewport", ICON_GEO_N_IS_VIEWPORT},
      {"GeometryNodeJoinGeometry", ICON_GEO_N_JOIN_GEOMETRY},
      {"GeometryNodeListGetItem", ICON_GEO_N_LIST_GET_ITEM},
      {"GeometryNodeListLength", ICON_GEO_N_LIST_LENGTH},
      {"GeometryNodeMaterialSelection", ICON_GEO_N_MATERIAL_SELECTION},
      {"GeometryNodeMenuSwitch", ICON_GEO_N_MENU_SWITCH},
      {"GeometryNodeMergeLayers", ICON_GEO_N_MERGE_LAYERS},
      {"GeometryNodeMeshFaceSetBoundaries", ICON_GEO_N_MESH_FACE_SET_BOUNDARIES},
      {"GeometryNodeMeshLine", ICON_GEO_N_MESH_LINE},
      {"GeometryNodeMeshToDensityGrid", ICON_GEO_N_MESH_TO_DENSITY_GRID},
      {"GeometryNodeMeshToSDFGrid", ICON_GEO_N_MESH_TO_SDF_GRID},
      {"GeometryNodeOffsetCornerInFace", ICON_GEO_N_OFFSET_CORNER_IN_FACE},
      {"GeometryNodeOffsetPointInCurve", ICON_GEO_N_OFFSET_POINT_IN_CURVE},
      {"GeometryNodePoints", ICON_GEO_N_POINTS},
      {"GeometryNodePointsOfCurve", ICON_GEO_N_POINTS_OF_CURVE},
      {"GeometryNodePointsToSDFGrid", ICON_GEO_N_POINTS_TO_SDF_GRID},
      {"GeometryNodePointsToVertices", ICON_GEO_N_POINTS_TO_VERTICES},
      {"GeometryNodeProximity", ICON_GEO_N_PROXIMITY},
      {"GeometryNodeRaycast", ICON_GEO_N_RAYCAST},
      {"GeometryNodeRealizeInstances", ICON_GEO_N_REALIZE_INSTANCES},
      {"GeometryNodeRemoveAttribute", ICON_GEO_N_REMOVE_ATTRIBUTE},
      {"GeometryNodeReplaceMaterial", ICON_GEO_N_REPLACE_MATERIAL},
      {"GeometryNodeResampleCurve", ICON_GEO_N_RESAMPLE_CURVE},
      {"GeometryNodeReverseCurve", ICON_GEO_N_REVERSE_CURVE},
      {"GeometryNodeRotateInstances", ICON_GEO_N_ROTATE_INSTANCES},
      {"GeometryNodeSDFGridBoolean", ICON_GEO_N_SDF_GRID_BOOLEAN},
      {"GeometryNodeSDFGridFillet", ICON_GEO_N_SDF_GRID_FILLET},
      {"GeometryNodeSDFGridLaplacian", ICON_GEO_N_SDF_GRID_LAPLACIAN},
      {"GeometryNodeSDFGridMean", ICON_GEO_N_SDF_GRID_MEAN},
      {"GeometryNodeSDFGridMeanCurvature", ICON_GEO_N_SDF_GRID_MEAN_CURVATURE},
      {"GeometryNodeSDFGridMedian", ICON_GEO_N_SDF_GRID_MEDIAN},
      {"GeometryNodeSDFGridOffset", ICON_GEO_N_SDF_GRID_OFFSET},
      {"GeometryNodeSampleCurve", ICON_GEO_N_SAMPLE_CURVE},
      {"GeometryNodeSampleGrid", ICON_GEO_N_SAMPLE_GRID},
      {"GeometryNodeSampleGridIndex", ICON_GEO_N_SAMPLE_GRID_INDEX},
      {"GeometryNodeSampleIndex", ICON_GEO_N_SAMPLE_INDEX},
      {"GeometryNodeSampleNearest", ICON_GEO_N_SAMPLE_NEAREST},
      {"GeometryNodeSampleNearestSurface", ICON_GEO_N_SAMPLE_NEAREST_SURFACE},
      {"GeometryNodeSampleUVSurface", ICON_GEO_N_SAMPLE_UV_SURFACE},
      {"GeometryNodeScaleInstances", ICON_GEO_N_SCALE_INSTANCES},
      {"GeometryNodeSelfObject", ICON_GEO_N_SELF_OBJECT},
      {"GeometryNodeSeparateComponents", ICON_GEO_N_SEPARATE_COMPONENTS},
      {"GeometryNodeSeparateGeometry", ICON_GEO_N_SEPARATE_GEOMETRY},
      {"GeometryNodeSetCurveHandlePositions", ICON_GEO_N_SET_CURVE_HANDLE_POSITIONS},
      {"GeometryNodeSetCurveNormal", ICON_GEO_N_SET_CURVE_NORMAL},
      {"GeometryNodeSetCurveRadius", ICON_GEO_N_SET_CURVE_RADIUS},
      {"GeometryNodeSetCurveTilt", ICON_GEO_N_SET_CURVE_TILT},
      {"GeometryNodeSetGeometryBundle", ICON_GEO_N_SET_GEOMETRY_BUNDLE},
      {"GeometryNodeSetGeometryName", ICON_GEO_N_SET_GEOMETRY_NAME},
      {"GeometryNodeSetGreasePencilColor", ICON_GEO_N_SET_GREASE_PENCIL_COLOR},
      {"GeometryNodeSetGreasePencilDepth", ICON_GEO_N_SET_GREASE_PENCIL_DEPTH},
      {"GeometryNodeSetGreasePencilSoftness", ICON_GEO_N_SET_GREASE_PENCIL_SOFTNESS},
      {"GeometryNodeSetGridBackground", ICON_GEO_N_SET_GRID_BACKGROUND},
      {"GeometryNodeSetGridTransform", ICON_GEO_N_SET_GRID_TRANSFORM},
      {"GeometryNodeSetID", ICON_GEO_N_SET_ID},
      {"GeometryNodeSetInstanceTransform", ICON_GEO_N_SET_INSTANCE_TRANSFORM},
      {"GeometryNodeSetMaterialIndex", ICON_GEO_N_SET_MATERIAL_INDEX},
      {"GeometryNodeSetMeshNormal", ICON_GEO_N_SET_MESH_NORMAL},
      {"GeometryNodeSetPointRadius", ICON_GEO_N_SET_POINT_RADIUS},
      {"GeometryNodeSetShadeSmooth", ICON_GEO_N_SET_SHADE_SMOOTH},
      {"GeometryNodeSetSplineCyclic", ICON_GEO_N_SET_SPLINE_CYCLIC},
      {"GeometryNodeSetSplineResolution", ICON_GEO_N_SET_SPLINE_RESOLUTION},
      {"GeometryNodeSortElements", ICON_GEO_N_SORT_ELEMENTS},
      {"GeometryNodeSplineLength", ICON_GEO_N_SPLINE_LENGTH},
      {"GeometryNodeSplineParameter", ICON_GEO_N_SPLINE_PARAMETER},
      {"GeometryNodeSplitToInstances", ICON_GEO_N_SPLIT_TO_INSTANCES},
      {"GeometryNodeStoreNamedAttribute", ICON_GEO_N_STORE_NAMED_ATTRIBUTE},
      {"GeometryNodeStoreNamedGrid", ICON_GEO_N_STORE_NAMED_GRID},
      {"GeometryNodeStringJoin", ICON_GEO_N_STRING_JOIN},
      {"GeometryNodeSubdivideCurve", ICON_GEO_N_SUBDIVIDE_CURVE},
      {"GeometryNodeSubdivideMesh", ICON_GEO_N_SUBDIVIDE_MESH},
      {"GeometryNodeSwitch", ICON_GEO_N_SWITCH},
      {"GeometryNodeTool3DCursor", ICON_GEO_N_TOOL3_D_CURSOR},
      {"GeometryNodeToolActiveElement", ICON_GEO_N_TOOL_ACTIVE_ELEMENT},
      {"GeometryNodeToolFaceSet", ICON_GEO_N_TOOL_FACE_SET},
      {"GeometryNodeToolMousePosition", ICON_GEO_N_TOOL_MOUSE_POSITION},
      {"GeometryNodeToolSelection", ICON_GEO_N_TOOL_SELECTION},
      {"GeometryNodeToolSetFaceSet", ICON_GEO_N_TOOL_SET_FACE_SET},
      {"GeometryNodeToolSetSelection", ICON_GEO_N_TOOL_SET_SELECTION},
      {"GeometryNodeTranslateInstances", ICON_GEO_N_TRANSLATE_INSTANCES},
      {"GeometryNodeTrimCurve", ICON_GEO_N_TRIM_CURVE},
      {"GeometryNodeUVPackIslands", ICON_GEO_N_UV_PACK_ISLANDS},
      {"GeometryNodeUVTangent", ICON_GEO_N_UV_TANGENT},
      {"GeometryNodeVertexOfCorner", ICON_GEO_N_VERTEX_OF_CORNER},
      {"GeometryNodeViewer", ICON_GEO_N_VIEWER},
      {"GeometryNodeViewportTransform", ICON_GEO_N_VIEWPORT_TRANSFORM},
      {"GeometryNodeVolumeCube", ICON_GEO_N_VOLUME_CUBE},
      {"GeometryNodeWarning", ICON_GEO_N_WARNING},
      {"NodeCombineBundle", ICON_GEO_N_COMBINE_BUNDLE},
      {"NodeEnableOutput", ICON_GEO_N_ENABLE_OUTPUT},
      {"NodeEvaluateClosure", ICON_GEO_N_EVALUATE_CLOSURE},
      {"NodeGetBundleItem", ICON_GEO_N_GET_BUNDLE_ITEM},
      {"NodeGroupInput", ICON_GEO_N_GROUP_INPUT},
      {"NodeGroupOutput", ICON_GEO_N_GROUP_OUTPUT},
      {"NodeJoinBundle", ICON_GEO_N_JOIN_BUNDLE},
      {"NodeSeparateBundle", ICON_GEO_N_SEPARATE_BUNDLE},
      {"NodeStoreBundleItem", ICON_GEO_N_STORE_BUNDLE_ITEM},
      {"ShaderNodeBlackbody", ICON_GEO_N_BLACKBODY},
      {"ShaderNodeClamp", ICON_GEO_N_CLAMP},
      {"ShaderNodeCombineXYZ", ICON_GEO_N_COMBINE_XYZ},
      {"ShaderNodeFloatCurve", ICON_GEO_N_FLOAT_CURVE},
      {"ShaderNodeGamma", ICON_GEO_N_GAMMA},
      {"ShaderNodeMapRange", ICON_GEO_N_MAP_RANGE},
      {"ShaderNodeMath", ICON_GEO_N_MATH},
      {"ShaderNodeMix", ICON_GEO_N_MIX},
      {"ShaderNodeRGBCurve", ICON_GEO_N_RGB_CURVE},
      {"ShaderNodeRadialTiling", ICON_GEO_N_RADIAL_TILING},
      {"ShaderNodeSeparateXYZ", ICON_GEO_N_SEPARATE_XYZ},
      {"ShaderNodeTexBrick", ICON_GEO_N_TEX_BRICK},
      {"ShaderNodeTexChecker", ICON_GEO_N_TEX_CHECKER},
      {"ShaderNodeTexGabor", ICON_GEO_N_TEX_GABOR},
      {"ShaderNodeTexGradient", ICON_GEO_N_TEX_GRADIENT},
      {"ShaderNodeTexMagic", ICON_GEO_N_TEX_MAGIC},
      {"ShaderNodeTexNoise", ICON_GEO_N_TEX_NOISE},
      {"ShaderNodeTexVoronoi", ICON_GEO_N_TEX_VORONOI},
      {"ShaderNodeTexWave", ICON_GEO_N_TEX_WAVE},
      {"ShaderNodeTexWhiteNoise", ICON_GEO_N_TEX_WHITE_NOISE},
      {"ShaderNodeValToRGB", ICON_GEO_N_VAL_TO_RGB},
      {"ShaderNodeValue", ICON_GEO_N_VALUE},
      {"ShaderNodeVectorCurve", ICON_GEO_N_VECTOR_CURVE},
      {"ShaderNodeVectorMath", ICON_GEO_N_VECTOR_MATH},
      {"ShaderNodeVectorRotate", ICON_GEO_N_VECTOR_ROTATE},
  };
  for (const auto &item : node_icons) {
    bke::bNodeType *ntype = bke::node_type_find(item.idname);
    if (ntype == nullptr) {
      continue;
    }
    ntype->ui_icon = item.icon;
    /* Also for scripts, the Add menu reads it from there. */
    if (ntype->rna_ext.srna) {
      RNA_def_struct_ui_icon(ntype->rna_ext.srna, item.icon);
    }
  }
}

static void register_zone_types()
{
  static SimulationZoneType simulation_zone_type;
  static RepeatZoneType repeat_zone_type;
  static ForeachGeometryElementZoneType foreach_geometry_element_zone_type;
  static ClosureZoneType closure_zone_type;
  bke::register_node_zone_type(simulation_zone_type);
  bke::register_node_zone_type(repeat_zone_type);
  bke::register_node_zone_type(foreach_geometry_element_zone_type);
  bke::register_node_zone_type(closure_zone_type);
}

void register_nodes()
{
  register_zone_types();

  register_undefined_types();

  register_standard_node_socket_types();

  register_node_tree_type_geo();
  register_node_tree_type_cmp();

  register_node_type_frame();
  register_node_type_reroute();
  register_node_type_group_input();
  register_node_type_group_output();

  register_compositor_nodes();
  register_shader_nodes();
  register_texture_nodes();
  register_geometry_nodes();
  register_function_nodes();

  set_geometry_node_icons();
}

}  // namespace blender
