zephyr_include_directories(include)

if(CONFIG_THINGSBOARD)
    zephyr_library_named(thingsboard)

    if (CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON)
        set_property(
            TARGET thingsboard
            PROPERTY JSON_SCHEMAS
            ${CMAKE_CURRENT_SOURCE_DIR}/thingsboard_attributes.jsonschema
        )

        set_property(
            TARGET thingsboard
            PROPERTY TELEMETRY_JSON_SCHEMAS
            ${CMAKE_CURRENT_SOURCE_DIR}/thingsboard_telemetry.jsonschema
        )

        add_custom_command(
            OUTPUT
                ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_attributes_serde.c
                ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_attributes_serde.h
            COMMAND
                ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_json_parser.py
                    -p ${CMAKE_CURRENT_BINARY_DIR}/generated/
                    $<TARGET_PROPERTY:thingsboard,JSON_SCHEMAS>
            DEPENDS ${JSON_SCHEMAS}
            COMMAND_EXPAND_LISTS
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

        add_custom_command(
            OUTPUT
                ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_telemetry_serde.c
                ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_telemetry_serde.h
            COMMAND
                ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_json_parser.py
                    -e ${CMAKE_CURRENT_BINARY_DIR}/generated/
                    $<TARGET_PROPERTY:thingsboard,TELEMETRY_JSON_SCHEMAS>

                DEPENDS ${TELEMETRY_JSON_SCHEMAS}
            COMMAND_EXPAND_LISTS
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

        add_custom_target(attributes_serde DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_attributes_serde.h)
        add_custom_target(telemetry_serde DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_telemetry_serde.h)
        add_dependencies(thingsboard attributes_serde telemetry_serde)

        zephyr_library_sources(
            ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_attributes_serde.c
            ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_telemetry_serde.c
            src/timeseries_json.c
        )
    endif()

    if (CONFIG_THINGSBOARD_CONTENT_FORMAT_PROTOBUF)
        list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
        include(nanopb)

        zephyr_nanopb_sources(${ZEPHYR_CURRENT_LIBRARY} thingsboard.proto)
        zephyr_include_directories(${CMAKE_CURRENT_BINARY_DIR})
        zephyr_library_sources(src/protobuf.c)
    endif()

    # For some reason the provision messages are always encoded in JSON
    add_custom_command(
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/generated/provision_response_serde.c
            ${CMAKE_CURRENT_BINARY_DIR}/generated/provision_response_serde.h
        COMMAND
            ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_json_parser.py
                -p ${CMAKE_CURRENT_BINARY_DIR}/generated/
                provision_response.jsonschema
        DEPENDS provision_response.jsonschema
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    zephyr_library_sources_ifdef(
        CONFIG_THINGSBOARD_USE_PROVISIONING
        ${CMAKE_CURRENT_BINARY_DIR}/generated/provision_response_serde.c
    )

    zephyr_library_sources(
        src/coap_client.c
        src/thingsboard.c
    )

    zephyr_library_sources_ifdef(
        CONFIG_THINGSBOARD_USE_PROVISIONING
        src/provision.c
    )

    zephyr_library_sources_ifdef(
        CONFIG_THINGSBOARD_FOTA
        src/tb_fota.c
    )

    zephyr_library_sources_ifdef(
        CONFIG_THINGSBOARD_TIME
        src/tb_time.c
    )

    zephyr_include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)

    if (NOT CONFIG_THINGSBOARD_FOTA)
        message(WARNING "Thingsboard FOTA module is disabled!")
    endif()
endif()
