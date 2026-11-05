/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "AS_asset_catalog.hh"
#include "AS_asset_catalog_tree.hh"
#include "AS_asset_library.hh"
#include "AS_asset_representation.hh"

#include "BLI_multi_value_map.hh"
#include "BLI_string_utf8.h"

#include "DNA_space_types.h"

#include "BKE_asset.hh"
#include "BKE_idprop.hh"
#include "BKE_screen.hh"

#include "BLT_translation.hh"

#include "RNA_access.hh"

#include "ED_asset.hh"
#include "ED_asset_menu_utils.hh"
#include "ED_node.hh"

#include "node_intern.hh"

namespace blender::ed::space_node {

static bool node_add_menu_poll(const bContext *C, MenuType * /*mt*/)
{
  return CTX_wm_space_node(C);
}

static bool all_loading_finished()
{
  AssetLibraryReference all_library_ref = asset_system::all_library_reference();
  return asset::list::is_loaded(&all_library_ref);
}

static asset::AssetItemTree build_catalog_tree(const bContext &C, const bNodeTree &node_tree)
{
  asset::AssetFilterSettings type_filter{};
  type_filter.id_types = FILTER_ID_NT;
  auto meta_data_filter = [&](const AssetMetaData &meta_data) {
    const IDProperty *tree_type = BKE_asset_metadata_idprop_find(&meta_data, "type");
    if (tree_type == nullptr || IDP_int_get(tree_type) != node_tree.type) {
      return false;
    }
    return true;
  };
  const AssetLibraryReference library = asset_system::all_library_reference();
  asset_system::all_library_reload_catalogs_if_dirty();
  return asset::build_filtered_all_catalog_tree(library, C, type_filter, meta_data_filter);
}

/**
 * Used to avoid adding a separate root catalog when the assets have already been added to one of
 * the builtin menus.
 * TODO: The need to define the builtin menu labels here is completely non-ideal. We don't have
 * any UI introspection that can do this though. This can be solved in the near future by
 * removing the need to define the add menu completely, instead using a per-node-type path which
 * can be merged with catalog tree.
 */
static Set<StringRef> get_builtin_menus(const int tree_type)
{
  Set<StringRef> menus;
  switch (tree_type) {
    case NTREE_GEOMETRY:
      return {"Attribute",
              "Input",
              "Input/Constant",
              "Input/Gizmo",
              "Input/Group",
              "Input/Import",
              "Input/Scene",
              "Output",
              "Geometry",
              "Geometry/Material",
              "Geometry/Read",
              "Geometry/Sample",
              "Geometry/Write",
              "Geometry/Operations",
              "Curve",
              "Curve/Read",
              "Curve/Sample",
              "Curve/Write",
              "Curve/Operations",
              "Curve/Primitives",
              "Curve/Topology",
              "Grease Pencil",
              "Grease Pencil/Read",
              "Grease Pencil/Operations",
              "Grease Pencil/Write",
              "Instances",
              "Mesh",
              "Mesh/Read",
              "Mesh/Sample",
              "Mesh/Write",
              "Mesh/Operations",
              "Mesh/Primitives",
              "Mesh/Topology",
              "Mesh/UV",
              "Point",
              "Volume",
              "Volume/Operations",
              "Volume/Primitives",
              "Simulation",
              "Color",
              "Texture",
              "Utilities",
              "Utilities/Bundle",
              "Utilities/Closure",
              "Utilities/Text",
              "Utilities/Vector",
              "Utilities/Field",
              "Utilities/Math",
              "Utilities/Matrix",
              "Utilities/Rotation",
              "Utilities/Deprecated",
              "Group",
              "Layout"};
    case NTREE_COMPOSIT:
      return {"Input",
              "Input/Constant",
              "Input/Scene",
              "Output",
              "Color",
              "Color/Adjust",
              "Creative",
              "Filter",
              "Filter/Blur",
              "Keying",
              "Mask",
              "Tracking",
              "Transform",
              "Texture",
              "Utilities",
              "Utilities/Math",
              "Utilities/Vector",
              "Group",
              "Layout"};
    case NTREE_SHADER:
      return {"Input",
              "Output",
              "Shader",
              "Displacement",
              "Color",
              "Texture",
              "Utilities",
              "Utilities/Math",
              "Utilities/Vector",
              "Group",
              "Layout"};
  }
  return {};
}


/** The icon of a node group asset in the Add menu: the bundled ones have their own, the others
 * show the node group icon. */
static int asset_menu_icon(const asset_system::AssetRepresentation &asset)
{
  static const struct {
    const char *name;
    int icon;
  } asset_icons[] = {
      {"Array", ICON_GEO_N_ASSET_ARRAY},
      {"Attach Hair Curves to Surface", ICON_GEO_N_ASSET_ATTACH_HAIR_CURVES_TO_SURFACE},
      {"Blend Hair Curves", ICON_GEO_N_ASSET_BLEND_HAIR_CURVES},
      {"Box Selection", ICON_GEO_N_ASSET_BOX_SELECTION},
      {"Braid Hair Curves", ICON_GEO_N_ASSET_BRAID_HAIR_CURVES},
      {"Chromatic Aberration", ICON_GEO_N_ASSET_CHROMATIC_ABERRATION},
      {"Clump Hair Curves", ICON_GEO_N_ASSET_CLUMP_HAIR_CURVES},
      {"Combine Cylindrical", ICON_GEO_N_ASSET_COMBINE_CYLINDRICAL},
      {"Combine Spherical", ICON_GEO_N_ASSET_COMBINE_SPHERICAL},
      {"Create Guide Index Map", ICON_GEO_N_ASSET_CREATE_GUIDE_INDEX_MAP},
      {"Curl Hair Curves", ICON_GEO_N_ASSET_CURL_HAIR_CURVES},
      {"Curve Info", ICON_GEO_N_ASSET_CURVE_INFO},
      {"Curve Root", ICON_GEO_N_ASSET_CURVE_ROOT},
      {"Curve Segment", ICON_GEO_N_ASSET_CURVE_SEGMENT},
      {"Curve Tip", ICON_GEO_N_ASSET_CURVE_TIP},
      {"Curve to Tube", ICON_GEO_N_ASSET_CURVE_TO_TUBE},
      {"Displace Geometry", ICON_GEO_N_ASSET_DISPLACE_GEOMETRY},
      {"Displace Hair Curves", ICON_GEO_N_ASSET_DISPLACE_HAIR_CURVES},
      {"Duplicate Hair Curves", ICON_GEO_N_ASSET_DUPLICATE_HAIR_CURVES},
      {"Edge Length", ICON_GEO_N_ASSET_EDGE_LENGTH},
      {"Face Corner Angle", ICON_GEO_N_ASSET_FACE_CORNER_ANGLE},
      {"Frizz Hair Curves", ICON_GEO_N_ASSET_FRIZZ_HAIR_CURVES},
      {"Generate Hair Curves", ICON_GEO_N_ASSET_GENERATE_HAIR_CURVES},
      {"Geometry Input", ICON_GEO_N_ASSET_GEOMETRY_INPUT},
      {"Hair Attachment Info", ICON_GEO_N_ASSET_HAIR_ATTACHMENT_INFO},
      {"Hair Curves Noise", ICON_GEO_N_ASSET_HAIR_CURVES_NOISE},
      {"Instance on Elements", ICON_GEO_N_ASSET_INSTANCE_ON_ELEMENTS},
      {"Interpolate Hair Curves", ICON_GEO_N_ASSET_INTERPOLATE_HAIR_CURVES},
      {"Is Edge Boundary", ICON_GEO_N_ASSET_IS_EDGE_BOUNDARY},
      {"Is Edge Loose", ICON_GEO_N_ASSET_IS_EDGE_LOOSE},
      {"Is Edge Manifold", ICON_GEO_N_ASSET_IS_EDGE_MANIFOLD},
      {"Is UV Split", ICON_GEO_N_ASSET_IS_UV_SPLIT},
      {"Normal Selection", ICON_GEO_N_ASSET_NORMAL_SELECTION},
      {"Random Rotation", ICON_GEO_N_ASSET_RANDOM_ROTATION},
      {"Randomize Transforms", ICON_GEO_N_ASSET_RANDOMIZE_TRANSFORMS},
      {"Redistribute Curve Points", ICON_GEO_N_ASSET_REDISTRIBUTE_CURVE_POINTS},
      {"Restore Curve Segment Length", ICON_GEO_N_ASSET_RESTORE_CURVE_SEGMENT_LENGTH},
      {"Retime", ICON_GEO_N_ASSET_RETIME},
      {"Roll Hair Curves", ICON_GEO_N_ASSET_ROLL_HAIR_CURVES},
      {"Rotate Hair Curves", ICON_GEO_N_ASSET_ROTATE_HAIR_CURVES},
      {"Scatter on Surface", ICON_GEO_N_ASSET_SCATTER_ON_SURFACE},
      {"Sensor Noise", ICON_GEO_N_ASSET_SENSOR_NOISE},
      {"Separate Cylindrical", ICON_GEO_N_ASSET_SEPARATE_CYLINDRICAL},
      {"Separate Spherical", ICON_GEO_N_ASSET_SEPARATE_SPHERICAL},
      {"Sepia", ICON_GEO_N_ASSET_SEPIA},
      {"Set Hair Curve Profile", ICON_GEO_N_ASSET_SET_HAIR_CURVE_PROFILE},
      {"Shrinkwrap Hair Curves", ICON_GEO_N_ASSET_SHRINKWRAP_HAIR_CURVES},
      {"Smooth Geometry", ICON_GEO_N_ASSET_SMOOTH_GEOMETRY},
      {"Smooth Hair Curves", ICON_GEO_N_ASSET_SMOOTH_HAIR_CURVES},
      {"Smooth by Angle", ICON_GEO_N_ASSET_SMOOTH_BY_ANGLE},
      {"Sphere Selection", ICON_GEO_N_ASSET_SPHERE_SELECTION},
      {"Split Toning", ICON_GEO_N_ASSET_SPLIT_TONING},
      {"Straighten Hair Curves", ICON_GEO_N_ASSET_STRAIGHTEN_HAIR_CURVES},
      {"Trim Hair Curves", ICON_GEO_N_ASSET_TRIM_HAIR_CURVES},
      {"Tune Image", ICON_GEO_N_ASSET_TUNE_IMAGE},
      {"Unsharp Mask", ICON_GEO_N_ASSET_UNSHARP_MASK},
      {"Vignette", ICON_GEO_N_ASSET_VIGNETTE},
  };
  const StringRefNull name = asset.get_name();
  for (const auto &item : asset_icons) {
    if (name == item.name) {
      return item.icon;
    }
  }
  return ICON_GEO_N_GROUP;
}

static void node_catalog_assets_draw(const bContext *C, Menu *menu)
{
  SpaceNode &snode = *CTX_wm_space_node(C);
  const bNodeTree *edit_tree = snode.edittree;
  if (!edit_tree) {
    return;
  }
  if (!snode.runtime->assets_for_menu) {
    snode.runtime->assets_for_menu = std::make_shared<asset::AssetItemTree>(
        build_catalog_tree(*C, *edit_tree));
    return;
  }
  asset::AssetItemTree &tree = *snode.runtime->assets_for_menu;

  const std::optional<StringRefNull> menu_path = CTX_data_string_get(C, "asset_catalog_path");
  if (!menu_path) {
    return;
  }

  const std::optional<StringRefNull> operator_id = CTX_data_string_get(C, "operator_id");
  if (!operator_id) {
    return;
  }

  const Span<asset_system::AssetRepresentation *> assets = tree.assets_per_path.lookup(
      menu_path->c_str());
  const asset_system::AssetCatalogTreeItem *catalog_item = tree.catalogs.find_item(
      menu_path->c_str());
  BLI_assert(catalog_item != nullptr);

  if (assets.is_empty() && !catalog_item->has_children()) {
    return;
  }

  ui::Layout *layout = menu->layout;
  bool add_separator = true;

  for (const asset_system::AssetRepresentation *asset : assets) {
    if (add_separator) {
      layout->separator();
      add_separator = false;
    }
    PointerRNA op_ptr = layout->op(*operator_id,
                                   IFACE_(asset->get_name()),
                                   asset_menu_icon(*asset),
                                   wm::OpCallContext::InvokeRegionWin,
                                   UI_ITEM_NONE);
    asset::operator_asset_reference_props_set(*asset, op_ptr);
  }

  const Set<StringRef> all_builtin_menus = get_builtin_menus(edit_tree->type);

  catalog_item->foreach_child([&](const asset_system::AssetCatalogTreeItem &item) {
    if (all_builtin_menus.contains_as(item.catalog_path().str())) {
      return;
    }
    if (add_separator) {
      layout->separator();
      add_separator = false;
    }
    asset::draw_node_menu_for_catalog(item, *operator_id, "NODE_MT_node_catalog_assets", *layout);
  });
}

static void node_unassigned_assets_draw(const bContext *C, Menu *menu)
{
  SpaceNode &snode = *CTX_wm_space_node(C);
  const bNodeTree *edit_tree = snode.edittree;
  if (!edit_tree) {
    return;
  }

  const std::optional<StringRefNull> operator_id = CTX_data_string_get(C, "operator_id");
  if (!operator_id) {
    return;
  }

  if (!snode.runtime->assets_for_menu) {
    snode.runtime->assets_for_menu = std::make_shared<asset::AssetItemTree>(
        build_catalog_tree(*C, *edit_tree));
    return;
  }
  asset::AssetItemTree &tree = *snode.runtime->assets_for_menu;
  for (const asset_system::AssetRepresentation *asset : tree.unassigned_assets) {
    PointerRNA op_ptr = menu->layout->op(*operator_id,
                                         IFACE_(asset->get_name()),
                                         asset_menu_icon(*asset),
                                         wm::OpCallContext::InvokeRegionWin,
                                         UI_ITEM_NONE);
    asset::operator_asset_reference_props_set(*asset, op_ptr);
  }
}

static void root_catalogs_draw(const bContext *C, Menu *menu, const StringRefNull operator_id)
{
  SpaceNode &snode = *CTX_wm_space_node(C);
  ui::Layout *layout = menu->layout;
  const bNodeTree *edit_tree = snode.edittree;
  if (!edit_tree) {
    return;
  }

  snode.runtime->assets_for_menu = std::make_shared<asset::AssetItemTree>(
      build_catalog_tree(*C, *edit_tree));

  const bool loading_finished = all_loading_finished();

  asset::AssetItemTree &tree = *snode.runtime->assets_for_menu;
  if (tree.catalogs.is_empty() && loading_finished && tree.unassigned_assets.is_empty()) {
    return;
  }

  layout->separator();

  if (!loading_finished) {
    layout->label(IFACE_("Loading Asset Libraries"), ICON_INFO);
  }

  const Set<StringRef> all_builtin_menus = get_builtin_menus(edit_tree->type);

  tree.catalogs.foreach_root_item([&](const asset_system::AssetCatalogTreeItem &item) {
    if (!all_builtin_menus.contains_as(item.catalog_path().str())) {
      asset::draw_node_menu_for_catalog(item, operator_id, "NODE_MT_node_catalog_assets", *layout);
    }
  });

  if (!tree.unassigned_assets.is_empty()) {
    layout->separator();
    layout->menu("NODE_MT_node_unassigned_assets", IFACE_("Unassigned"), ICON_FILE_HIDDEN);
  }
}

static void add_root_catalogs_draw(const bContext *C, Menu *menu)
{
  const StringRefNull operator_id = "NODE_OT_add_group_asset";

  menu->layout->context_string_set("operator_id", operator_id);
  root_catalogs_draw(C, menu, operator_id);
}

static void swap_root_catalogs_draw(const bContext *C, Menu *menu)
{
  const StringRefNull operator_id = "NODE_OT_swap_group_asset";

  menu->layout->context_string_set("operator_id", operator_id);
  root_catalogs_draw(C, menu, operator_id);
}

MenuType catalog_assets_menu_type()
{
  MenuType type{};
  STRNCPY_UTF8(type.idname, "NODE_MT_node_catalog_assets");
  type.poll = node_add_menu_poll;
  type.draw = node_catalog_assets_draw;
  type.listener = asset::list::asset_reading_region_listen_fn;
  type.flag = MenuTypeFlag::ContextDependent;
  return type;
}

MenuType unassigned_assets_menu_type()
{
  MenuType type{};
  STRNCPY_UTF8(type.idname, "NODE_MT_node_unassigned_assets");
  type.poll = node_add_menu_poll;
  type.draw = node_unassigned_assets_draw;
  type.listener = asset::list::asset_reading_region_listen_fn;
  type.flag = MenuTypeFlag::ContextDependent;
  type.description = N_(
      "Node group assets not assigned to a catalog.\n"
      "Catalogs can be assigned in the Asset Browser");
  return type;
}

MenuType add_root_catalogs_menu_type()
{
  MenuType type{};
  STRNCPY_UTF8(type.idname, "NODE_MT_node_add_root_catalogs");
  type.poll = node_add_menu_poll;
  type.draw = add_root_catalogs_draw;
  type.listener = asset::list::asset_reading_region_listen_fn;
  return type;
}

MenuType swap_root_catalogs_menu_type()
{
  MenuType type{};
  STRNCPY_UTF8(type.idname, "NODE_MT_node_swap_root_catalogs");
  type.poll = node_add_menu_poll;
  type.draw = swap_root_catalogs_draw;
  type.listener = asset::list::asset_reading_region_listen_fn;
  return type;
}

void ui_template_node_asset_menu_items(ui::Layout &layout,
                                       const bContext &C,
                                       const StringRef catalog_path,
                                       const ui::NodeAssetMenuOperatorType operator_type)
{
  SpaceNode &snode = *CTX_wm_space_node(&C);
  if (snode.runtime->assets_for_menu == nullptr) {
    return;
  }
  asset::AssetItemTree &tree = *snode.runtime->assets_for_menu;
  const asset_system::AssetCatalogTreeItem *item = tree.catalogs.find_item(catalog_path);
  if (!item) {
    return;
  }

  StringRef operator_id;

  switch (operator_type) {
    case ui::NodeAssetMenuOperatorType::Swap:
      operator_id = "NODE_OT_swap_group_asset";
      break;
    default:
      operator_id = "NODE_OT_add_group_asset";
  }

  ui::Layout *col = &layout.column(false);
  col->context_string_set("asset_catalog_path", item->catalog_path().str());
  col->context_string_set("operator_id", operator_id);
  col->menu_contents("NODE_MT_node_catalog_assets");
}

}  // namespace blender::ed::space_node
