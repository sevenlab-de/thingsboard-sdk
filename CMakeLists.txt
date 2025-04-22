zephyr_include_directories(include)

if(CONFIG_THINGSBOARD)
    zephyr_library_named(thingsboard)

    set_property(
        TARGET thingsboard
        PROPERTY JSON_SCHEMAS
        ${CMAKE_CURRENT_SOURCE_DIR}/thingsboard_attr.jsonschema
    )

    set_property(
        TARGET thingsboard
        PROPERTY TELEMETRY_JSON_SCHEMAS
        ${CMAKE_CURRENT_SOURCE_DIR}/thingsboard_telemetry.jsonschema
    )

    add_custom_command(
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_attr_serde.c
            ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_attr_serde.h
        COMMAND
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_json_parser.py -p ${CMAKE_CURRENT_BINARY_DIR}/generated/ $<TARGET_PROPERTY:thingsboard,JSON_SCHEMAS>
        DEPENDS ${JSON_SCHEMAS}
        COMMAND_EXPAND_LISTS
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_custom_command(
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_telemetry_serde.c
            ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_telemetry_serde.h
        COMMAND
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_json_parser.py -e ${CMAKE_CURRENT_BINARY_DIR}/generated/ $<TARGET_PROPERTY:thingsboard,TELEMETRY_JSON_SCHEMAS>
        DEPENDS ${TELEMETRY_JSON_SCHEMAS}
        COMMAND_EXPAND_LISTS
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_custom_command(
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/generated/provision_response_serde.c
            ${CMAKE_CURRENT_BINARY_DIR}/generated/provision_response_serde.h
        COMMAND
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_json_parser.py -p ${CMAKE_CURRENT_BINARY_DIR}/generated/ provision_response.jsonschema
        DEPENDS provision_response.jsonschema
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_custom_target(attr_serde DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_attr_serde.h)
    add_custom_target(telemetry_serde DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_telemetry_serde.h)
    add_dependencies(thingsboard attr_serde telemetry_serde)

    zephyr_library_sources(
        src/coap_client.c
        src/thingsboard.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_attr_serde.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/thingsboard_telemetry_serde.c
    )

    zephyr_library_sources_ifdef(
        CONFIG_THINGSBOARD_USE_PROVISIONING
        src/provision.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/provision_response_serde.c
    )

    zephyr_library_sources_ifdef(
        CONFIG_THINGSBOARD_FOTA
        src/tb_fota.c
    )

    zephyr_include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)

    if (NOT CONFIG_THINGSBOARD_FOTA)
        message(WARNING "Thingsboard FOTA module is disabled!")
    endif()
endif()
