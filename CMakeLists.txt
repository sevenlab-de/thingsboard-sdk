zephyr_include_directories(include)

if(CONFIG_THINGSBOARD)
    zephyr_library_named(thingsboard)

    function(thingsboard_add_json_target base_json_schema generate_parser generate_encoder json_schemas)
        set(header_file ${CMAKE_CURRENT_BINARY_DIR}/generated/${base_json_schema}_serde.h)
        set(source_file ${CMAKE_CURRENT_BINARY_DIR}/generated/${base_json_schema}_serde.c)
        set(gen_args $<$<BOOL:${generate_parser}>:-p> $<$<BOOL:${generate_encoder}>:-e>)
        add_custom_command(
            OUTPUT
                ${source_file}
                ${header_file}
            COMMAND
                ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_json_parser.py
                ${gen_args}
                ${CMAKE_CURRENT_BINARY_DIR}/generated/
                ${json_schemas}
            DEPENDS ${json_schemas}
            COMMAND_EXPAND_LISTS
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        add_custom_target(
        ${base_json_schema}_serde
        DEPENDS
            ${header_file}
        )
        add_dependencies(thingsboard ${base_json_schema}_serde)
        zephyr_library_sources(${source_file})
    endfunction()

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

        thingsboard_add_json_target(
            thingsboard_attributes
            TRUE FALSE
            $<TARGET_PROPERTY:thingsboard,JSON_SCHEMAS>
        )
        thingsboard_add_json_target(
            thingsboard_telemetry
            FALSE TRUE
            $<TARGET_PROPERTY:thingsboard,TELEMETRY_JSON_SCHEMAS>
        )
        thingsboard_add_json_target(
            thingsboard_rpc_request
            FALSE TRUE
            ${CMAKE_CURRENT_SOURCE_DIR}/thingsboard_rpc_request.jsonschema
        )

        zephyr_library_sources(
            src/thingsboard_messages_json.c
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
    thingsboard_add_json_target(
        thingsboard_provision_response
        TRUE FALSE
        thingsboard_provision_response.jsonschema
    )

    zephyr_library_sources(
        src/thingsboard.c
        src/socket.c
    )

    zephyr_library_sources_ifdef(
        CONFIG_THINGSBOARD_DTLS
        src/socket_dtls.c
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
