# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  ../include
  ../../makesrna
)

set(INC_SYS
)

set(SRC
  space_toolbox.cc
)

set(LIB
  PRIVATE bf::blenkernel
  PRIVATE bf::blenlib
  PRIVATE bf::blenloader
  PRIVATE bf::dna
  PRIVATE bf::intern::guardedalloc
  PRIVATE bf::windowmanager
)

blender_add_lib(bf_editor_space_toolbox "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
