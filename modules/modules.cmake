# ZMK v0.3 selects the keymap in its module extension before Devicetree.
# This extension runs afterwards and supplies a build-local header.
if("zen_left" IN_LIST SHIELD_AS_LIST OR "zen_right" IN_LIST SHIELD_AS_LIST)
  if(NOT KEYMAP_FILE)
    message(FATAL_ERROR "ZEN AML generation requires ZMK's selected KEYMAP_FILE")
  endif()
  set(zen_aml_keymap "${KEYMAP_FILE}")
  set(zen_aml_generator "${CMAKE_CURRENT_LIST_DIR}/../scripts/generate-aml-exclusions.py")
  set(zen_aml_header "${CMAKE_BINARY_DIR}/aml-exclusions.h")
  execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "${zen_aml_generator}"
      "${zen_aml_keymap}" "${zen_aml_header}" --mouse-layer 1 --key-count 50
      --depfile "${CMAKE_BINARY_DIR}/aml-keymap-inputs.txt"
    RESULT_VARIABLE zen_aml_result
    ERROR_VARIABLE zen_aml_error
  )
  if(NOT zen_aml_result EQUAL 0)
    message(FATAL_ERROR "ZEN AML generation failed: ${zen_aml_error}")
  endif()
  file(STRINGS "${CMAKE_BINARY_DIR}/aml-keymap-inputs.txt" zen_aml_inputs)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${zen_aml_inputs} "${zen_aml_generator}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/aml_keymap.py")
  list(APPEND DTS_EXTRA_CPPFLAGS "-I${CMAKE_BINARY_DIR}")
endif()
