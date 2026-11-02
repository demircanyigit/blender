/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#define WB_RESOLVE_GROUP_SIZE 8

/* Resources bind slots. */

/* Textures. */
/* Slot 0-1 are reserved by curves and pointcloud attributes. */
#define WB_MATCAP_SLOT 2
#define WB_TEXTURE_SLOT 3
#define WB_TILE_ARRAY_SLOT 4
#define WB_TILE_DATA_SLOT 5
#define WB_CURVES_UV_SLOT 6
#define WB_CURVES_COLOR_SLOT 7

/* UBOs (Storage buffers in Workbench Next). */
#define WB_MATERIAL_SLOT 0
#define WB_WORLD_SLOT 1
/* Storage buffer. */
#define WB_COLOR_RAMP_SLOT 3

/* Number of colors stored for every color ramp of the materials. */
#define WB_COLOR_RAMP_SIZE 256

/* Keep in sync with #Material::viewport_color_blend and #Material::viewport_color_channel. */
#define WB_COLOR_BLEND_MIX 1
#define WB_COLOR_BLEND_MULTIPLY 2
#define WB_COLOR_BLEND_TWO_COLORS 3
#define WB_COLOR_BLEND_RAMP 4
#define WB_COLOR_CHANNEL_BRIGHTNESS 0
