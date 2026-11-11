# SPDX-FileCopyrightText: 2026 Blender Authors (build integration)
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC PUBLIC src)
set(INC_SYS)
set(SRC
  src/allocator.cpp
  src/indexgenerator.cpp
  src/simplifier.cpp
  src/meshoptimizer.h
)
set(LIB)
blender_add_lib(extern_meshoptimizer "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
add_library(bf::extern::meshoptimizer ALIAS extern_meshoptimizer)
