/* SPDX-FileCopyrightText: 2020-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once
#pragma create_info

#include "gpu_shader_compat.hh"
#include "workbench_defines.hh"

namespace workbench::color {

struct Materials {
  [[storage(WB_MATERIAL_SLOT, read)]] float4 (&materials_data)[];

  void material_data_get(int handle,
                         float4 attr_color,
                         float3 &color,
                         float &alpha,
                         float &roughness,
                         float &metallic,
                         int &ramp_row,
                         float &ramp_value,
                         float &ramp_factor)
  {
    /* Every material takes 3 elements, see #Material. */
    float4 data = materials_data[handle * 3];
    float4 data_b = materials_data[handle * 3 + 1];
    float4 data_blend = materials_data[handle * 3 + 2];

    color = (data.r == -1) ? attr_color.rgb : data.rgb;
    /* Face set colors, stored in display space. */
    if (data.r == -2) {
      color = pow(attr_color.rgb, float3(2.2f));
    }

    float color_factor = data_b.a;
    int color_blend = int(data_blend.x);
    int color_channel = int(data_blend.y);
    /* Value picking a color from the two colors or the ramp. */
    float color_value = (color_channel == WB_COLOR_CHANNEL_BRIGHTNESS) ?
                            dot(attr_color.rgb, float3(0.2126f, 0.7152f, 0.0722f)) :
                            attr_color[color_channel - 1];

    if (color_blend == WB_COLOR_BLEND_MIX) {
      color = mix(color, attr_color.rgb, color_factor);
    }
    else if (color_blend == WB_COLOR_BLEND_MULTIPLY) {
      color *= mix(float3(1.0f), attr_color.rgb, color_factor);
    }
    else if (color_blend == WB_COLOR_BLEND_TWO_COLORS) {
      color = mix(color, data_b.rgb, saturate(color_value) * color_factor);
    }

    /* The ramp isn't linear, so it can't be interpolated from the vertices. */
    ramp_row = (color_blend == WB_COLOR_BLEND_RAMP) ? int(data_blend.z) : -1;
    ramp_value = color_value;
    ramp_factor = color_factor;

    uint encoded_data = floatBitsToUint(data.w);
    alpha = float((encoded_data >> 16u) & 0xFFu) * (1.0f / 255.0f);
    roughness = float((encoded_data >> 8u) & 0xFFu) * (1.0f / 255.0f);
    metallic = float(encoded_data & 0xFFu) * (1.0f / 255.0f);
  }
};

struct ColorRamps {
  [[storage(WB_COLOR_RAMP_SLOT, read)]] float4 (&color_ramps_data)[];

  float3 ramp_color(int row, float value)
  {
    float position = saturate(value) * float(WB_COLOR_RAMP_SIZE - 1);
    int index = min(int(position), WB_COLOR_RAMP_SIZE - 2);
    int offset = row * WB_COLOR_RAMP_SIZE + index;
    return mix(color_ramps_data[offset].rgb,
               color_ramps_data[offset + 1].rgb,
               position - float(index));
  }
};

}  // namespace workbench::color
