set(
    CODEX_UPDATE_PUBLIC_KEY_DER
    ""
    CACHE FILEPATH
    "DER-encoded P-256 update verification public key"
)

if(CODEX_UPDATE_PUBLIC_KEY_DER)
    if(NOT EXISTS "${CODEX_UPDATE_PUBLIC_KEY_DER}")
        message(FATAL_ERROR
            "CODEX_UPDATE_PUBLIC_KEY_DER does not exist: "
            "${CODEX_UPDATE_PUBLIC_KEY_DER}"
        )
    endif()
    file(READ "${CODEX_UPDATE_PUBLIC_KEY_DER}" CODEX_UPDATE_PUBLIC_KEY_HEX HEX)
else()
    # Development-only staging key. The production build driver always passes
    # CODEX_UPDATE_PUBLIC_KEY_DER and the final-image audit rejects this key.
    string(CONCAT CODEX_UPDATE_PUBLIC_KEY_HEX
        "3059301306072a8648ce3d020106082a8648ce3d03010703420004943b6b50"
        "a3c51c15a8fb3b710080837d608bb6fbd91b08d888ad3b38bbf29bbad87ede"
        "c016cfe2b15de1d46cecc7f77f0b1ab06eea9c32e8cc4de5ebe1c54aa3"
    )
endif()

string(LENGTH "${CODEX_UPDATE_PUBLIC_KEY_HEX}" CODEX_UPDATE_PUBLIC_KEY_HEX_LENGTH)
if(CODEX_UPDATE_PUBLIC_KEY_HEX_LENGTH LESS 128)
    message(FATAL_ERROR "Update verification DER is unexpectedly short")
endif()
math(EXPR CODEX_UPDATE_PUBLIC_KEY_HEX_REMAINDER
    "${CODEX_UPDATE_PUBLIC_KEY_HEX_LENGTH} % 2"
)
if(NOT CODEX_UPDATE_PUBLIC_KEY_HEX_REMAINDER EQUAL 0)
    message(FATAL_ERROR "Update verification DER has an invalid byte length")
endif()
string(REGEX MATCHALL ".." CODEX_UPDATE_PUBLIC_KEY_BYTE_LIST
    "${CODEX_UPDATE_PUBLIC_KEY_HEX}"
)
list(TRANSFORM CODEX_UPDATE_PUBLIC_KEY_BYTE_LIST PREPEND "0x")
list(JOIN CODEX_UPDATE_PUBLIC_KEY_BYTE_LIST ", " CODEX_UPDATE_PUBLIC_KEY_BYTES)

set(CODEX_UPDATE_GENERATED_INCLUDE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
file(MAKE_DIRECTORY "${CODEX_UPDATE_GENERATED_INCLUDE_DIR}/codex")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/update_public_key_config.h.in"
    "${CODEX_UPDATE_GENERATED_INCLUDE_DIR}/codex/update_public_key_config.h"
    @ONLY
)

idf_component_register(
    SRCS
        "update_manifest.cpp"
        "update_crypto.cpp"
        "update_session.cpp"
        "firmware_update.cpp"
        "update_protocol.cpp"
        "update_identity.cpp"
        "compatibility_pack.cpp"
        "service_pack.cpp"
        "service_pack_storage.cpp"
        "service_resource_runtime.cpp"
        "device_update_storage.cpp"
        "user_content_pack.cpp"
        "user_content_storage.cpp"
        "user_content_protocol.cpp"
    INCLUDE_DIRS "include" "${CODEX_UPDATE_GENERATED_INCLUDE_DIR}"
    REQUIRES codex_core mbedtls app_update esp_partition nvs_flash esp_timer
             esp_hw_support efuse
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
