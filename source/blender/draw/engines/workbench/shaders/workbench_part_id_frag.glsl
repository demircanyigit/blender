/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/workbench_effect_outline_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(workbench_part_id)

void main()
{
  out_part_id = part_id;
}
