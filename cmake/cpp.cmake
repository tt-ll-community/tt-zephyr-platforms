# SPDX-License-Identifier: Apache-2.0

# ------------------------------------------------------------------------
# Function: add_preprocessed_file
#
# Usage:
#   add_preprocessed_file(<target-name>
#     INPUT    <in-file>
#     OUTPUT   <out-file>
#     [COMPILER <path-to-cc>]
#     [INCLUDES <inc-dir> [<inc-dir> ...]]
#     [FLAGS    <extra-flags>]
#   )
# ------------------------------------------------------------------------
function(add_preprocessed_file name)
  cmake_parse_arguments(PPF "" "COMPILER;INPUT;OUTPUT;FLAGS" "INCLUDES" ${ARGN})

  # pick compiler
  if(PPF_COMPILER)
    set(_cc ${PPF_COMPILER})
  else()
    set(_cc ${CMAKE_C_COMPILER})
  endif()

  # build include flags
  set(_incs "")
  foreach(d IN LISTS PPF_INCLUDES)
    list(APPEND _incs "-I${d}")
  endforeach()

  # custom command to run the preprocessor
  add_custom_command(
    OUTPUT      ${PPF_OUTPUT}
    COMMAND     ${_cc} -xc -E -P ${_incs} ${PPF_FLAGS} ${PPF_INPUT} -o ${PPF_OUTPUT}
    DEPENDS     ${PPF_INPUT}
    COMMENT     "Preprocessing ${PPF_INPUT} → ${PPF_OUTPUT}"
    VERBATIM
  )
endfunction()
