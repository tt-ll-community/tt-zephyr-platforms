# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(bh_arc)
zephyr_include_directories(.)
zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)

zephyr_library_sources_ifndef(
  CONFIG_TT_SMC_RECOVERY
  aiclk_ppm.c
  clock_wave.c
  dvfs.c
  efuse.c
  eth.c
  fan_ctrl.c
  flash_info_table.c
  functional_efuse.c
  fw_table.c
  gddr.c
  harvesting.c
  i2c_messages.c
  noc.c
  noc_init.c
  pcie_dma.c
  pcie_msi.c
  pvt.c
  regulator.c
  serdes_eth.c
  telemetry.c
  telemetry_internal.c
  tensix_cg.c
  throttler.c
  vf_curve.c
  voltage.c
)

zephyr_library_sources_ifdef(CONFIG_TT_BH_ARC_SYSINIT init.c)

zephyr_library_sources(
  asic_state.c
  arc_dma.c
  avs.c
  cat.c
  cm2dm_msg.c
  dw_apb_i2c.c
  gpio.c
  init_common.c
  msgqueue.c
  noc2axi.c
  pcie.c
  pll.c
  post_code.c
  read_only_table.c
  smbus_target.c
  spi_eeprom.c
  timer.c
)
zephyr_linker_sources(DATA_SECTIONS iterables.ld)
if(CONFIG_ARC)
  zephyr_library_link_libraries(${ZEPHYR_CURRENT_MODULE_DIR}/zephyr/blobs/tt_blackhole_libpciesd.a)
endif()


list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

set(PROTOC ${PROTOBUF_PROTOC_EXECUTABLE})
set(SPIROM_PROTOBUFS ${ZEPHYR_CURRENT_MODULE_DIR}/lib/tenstorrent/bh_arc/spirom_protobufs)

message(STATUS "Generating Python files from protobufs")
set(OUTPUT_DIR ${ZEPHYR_BINARY_DIR}/python_proto_files)
file(MAKE_DIRECTORY ${OUTPUT_DIR})

foreach(PROTO fw_table.proto read_only.proto flash_info.proto)
    add_custom_command(
        OUTPUT      "${SPIROM_PROTOBUFS}/${PROTO}"
        COMMAND     ${CMAKE_C_COMPILER} -xc -E -P "-DNANOPB=1" "${SPIROM_PROTOBUFS}/in__${PROTO}" -o "${SPIROM_PROTOBUFS}/${PROTO}"
        DEPENDS     "${SPIROM_PROTOBUFS}/in__${PROTO}"
        COMMENT     "Preprocessing ${PPF_INPUT} → ${PPF_OUTPUT}"
        VERBATIM
    )

    zephyr_nanopb_sources(bh_arc "${SPIROM_PROTOBUFS}/${PROTO}")

    set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
        COMMAND ${CMAKE_C_COMPILER} -xc -E -P "-DNANOPB=0" "${SPIROM_PROTOBUFS}/in__${PROTO}" -o "${CMAKE_BINARY_DIR}/pb_min__${PROTO}"
        COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} "${CMAKE_BINARY_DIR}/pb_min__${PROTO}" -I "${CMAKE_BINARY_DIR}"
    )
endforeach(PROTO)


if(NOT DEFINED CONFIG_TT_SMC_RECOVERY)
	if(BOARD STREQUAL "tt_blackhole")
	# Map board revision names to folder name
	string(TOUPPER ${BOARD_REVISION} PROD_NAME)
	elseif(BOARD STREQUAL "native_sim")
	# Use P100 data files to stand in
	set(PROD_NAME "P100")
	else()
	message(FATAL_ERROR "No support for board ${BOARD}")
	endif()

	message(STATUS "Generating rom configuration binary files for ${PROD_NAME}")

	include(${ZEPHYR_CURRENT_MODULE_DIR}/cmake/bundle_version.cmake)

	# variable is needed to avoid protobuf version clashing
	if (PROD_NAME MATCHES "^P300")
	set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
		COMMAND PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
		  ${PYTHON_EXECUTABLE} ${ZEPHYR_CURRENT_MODULE_DIR}/scripts/encode_spirom_bins.py
		  --board ${PROD_NAME}_L --build-dir ${CMAKE_BINARY_DIR} --output ${CMAKE_BINARY_DIR}/generated_board_cfg
		  --bundle-version ${BUNDLE_VERSION_STRING}
	)
	set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
		COMMAND PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
		  ${PYTHON_EXECUTABLE} ${ZEPHYR_CURRENT_MODULE_DIR}/scripts/encode_spirom_bins.py
		  --board ${PROD_NAME}_R --build-dir ${CMAKE_BINARY_DIR} --output ${CMAKE_BINARY_DIR}/generated_board_cfg
		  --bundle-version ${BUNDLE_VERSION_STRING}
	)
	else()
	set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
		COMMAND PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
		  ${PYTHON_EXECUTABLE} ${ZEPHYR_CURRENT_MODULE_DIR}/scripts/encode_spirom_bins.py
		  --board ${PROD_NAME} --build-dir ${CMAKE_BINARY_DIR} --output ${CMAKE_BINARY_DIR}/generated_board_cfg
		  --bundle-version ${BUNDLE_VERSION_STRING}
	)
	endif()
endif()
