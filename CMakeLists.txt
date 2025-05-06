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
        # based on `zephyr_nanopb_sources()`
        function(thingsboard_set_proto proto_file)
            list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
            include(nanopb)

            # Turn off the default nanopb behavior
            set(NANOPB_GENERATE_CPP_STANDALONE OFF)

            get_filename_component(proto_path ${proto_file} DIRECTORY)
            if ("${proto_path}_" STREQUAL "_")
                set(proto_path ${CMAKE_CURRENT_SOURCE_DIR})
            endif()
            nanopb_generate_cpp(proto_srcs proto_hdrs RELPATH ${proto_path} ${proto_file})

            get_filename_component(proto_inc_path ${proto_hdrs} DIRECTORY)
            target_include_directories(thingsboard PUBLIC ${proto_inc_path})
            target_sources(thingsboard PRIVATE ${proto_srcs} ${proto_hdrs})

            # Create unique target name for generated header list
            string(MD5 unique_chars "${proto_hdrs}")
            set(gen_target_name thingsboard_proto_${unique_chars})

            add_custom_target(${gen_target_name} DEPENDS ${proto_hdrs})
            add_dependencies(nanopb_generated_headers ${gen_target_name})
        endfunction()
        if (NOT CONFIG_THINGSBOARD_PROTOBUF_CUSTOM_PROTO_FILE)
            thingsboard_set_proto(thingsboard.proto)
        endif()
        zephyr_library_sources(src/thingsboard_messages_protobuf.c)
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
