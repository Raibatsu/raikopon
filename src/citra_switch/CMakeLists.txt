# The replacement __libnx_exception_entry is hand-written AArch64.
# And yes, this took as long as you'd expect it to.
enable_language(ASM)

# Dekopon frontend
add_executable(citra_switch
    applets/swkbd.cpp
    applets/swkbd.h
    applets/swkbd_horizon.cpp
    applets/swkbd_horizon.h
    canvas.cpp
    canvas.h
    citra_switch.cpp
    config.cpp
    config.h
    default_ini.h
    emu_window.cpp
    emu_window.h
    emulation.cpp
    game_settings.cpp
    game_settings.h
    harfbuzz_stubs.cpp
    # The exception entry stub must be a strong symbol in the executable itself so the linker picks
    # it over libnx's weak __libnx_exception_entry.
    horizon_exception.cpp
    horizon_exception_entry.S
    horizon_exception_entry.h
    horizon_sys_stubs.cpp
    ingame_cheats.cpp
    ingame_cheats.h
    ingame_settings.cpp
    ingame_settings.h
    input.cpp
    input.h
    layout_editor.cpp
    layout_editor.h
    keyboard_prompt.h
    menu.cpp
    menu.h
    menu_data.cpp
    menu_data.h
    rail_icons.h
    settings_model.cpp
    settings_model.h
    updater.cpp
    updater.h
)

# oaknut_horizon_jit is pulled in transitively via oaknut
target_link_libraries(citra_switch PRIVATE citra_core citra_common inih)

# The launcher draws with a libnx software framebuffer and renders text with FreeType
find_path(SWITCH_FREETYPE_INCLUDE_DIR ft2build.h PATH_SUFFIXES freetype2 REQUIRED)
find_library(SWITCH_FREETYPE freetype REQUIRED)
find_library(SWITCH_PNG png REQUIRED)
find_library(SWITCH_BZ2 bz2 REQUIRED)
find_library(SWITCH_ZLIB z REQUIRED)
target_include_directories(citra_switch PRIVATE ${SWITCH_FREETYPE_INCLUDE_DIR})
# Order matters for the static newlib link: freetype -> png -> bz2 -> z.
target_link_libraries(citra_switch PRIVATE
    ${SWITCH_FREETYPE} ${SWITCH_PNG} ${SWITCH_BZ2} ${SWITCH_ZLIB})

# GitHub release polling + download for the in-app updater (Updates tab / updater.cpp).
find_path(SWITCH_CURL_INCLUDE_DIR curl/curl.h REQUIRED)
find_library(SWITCH_CURL curl REQUIRED)
find_path(SWITCH_MBEDTLS_INCLUDE_DIR mbedtls/sha256.h REQUIRED)
find_library(SWITCH_MBEDTLS mbedtls REQUIRED)
find_library(SWITCH_MBEDX509 mbedx509 REQUIRED)
find_library(SWITCH_MBEDCRYPTO mbedcrypto REQUIRED)
target_include_directories(citra_switch PRIVATE
    ${SWITCH_CURL_INCLUDE_DIR} ${SWITCH_MBEDTLS_INCLUDE_DIR})
# Order matters for the static newlib link: curl -> mbedtls -> mbedx509 -> mbedcrypto.
target_link_libraries(citra_switch PRIVATE
    ${SWITCH_CURL} ${SWITCH_MBEDTLS} ${SWITCH_MBEDX509} ${SWITCH_MBEDCRYPTO})
# nlohmann/json (vendored, see externals/CMakeLists.txt) for parsing GitHub API responses.
target_link_libraries(citra_switch PRIVATE json-headers)

# The GLES bring-up path pulls the switch-mesa EGL/GLES driver. It is mutually exclusive with NXVK
# until NXVK either supports nouveau or Zink (or both).
if (ENABLE_OPENGL)
    # EGL provided by switch-mesa
    find_path(SWITCH_GL_INCLUDE_DIR EGL/egl.h REQUIRED)
    find_library(SWITCH_EGL EGL REQUIRED)
    find_library(SWITCH_GLESV2 GLESv2 REQUIRED)
    find_library(SWITCH_GLAPI glapi REQUIRED)
    find_library(SWITCH_DRM_NOUVEAU drm_nouveau REQUIRED)

    target_include_directories(citra_switch PRIVATE ${SWITCH_GL_INCLUDE_DIR})
    # Prepend our externals/glad so the GLES2 loader is found
    target_include_directories(citra_switch BEFORE PRIVATE
        $<TARGET_PROPERTY:glad,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(citra_switch PRIVATE glad
        ${SWITCH_EGL} ${SWITCH_GLESV2} ${SWITCH_GLAPI} ${SWITCH_DRM_NOUVEAU})
endif()

# Vulkan supported provided by the NXVK project
if (ENABLE_VULKAN)
    target_link_libraries(citra_switch PRIVATE nxvk)
endif()

set(RAIKOPON_NRO_NAME "raikopon")
# Single source of truth for the app version: feeds both the NACP metadata below and
# raikopon_version.h (generated), which the in-app updater compares GitHub release tags against.
# Bump this by hand for every release. Keep it short - the NACP DisplayVersion field must be
# under 0x10 bytes on real hardware.
set(RAIKOPON_VERSION "1.6.0")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/raikopon_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/raikopon_version.h"
    @ONLY
)
target_include_directories(citra_switch PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

nx_generate_nacp(OUTPUT ${RAIKOPON_NRO_NAME}.nacp
    NAME "Raikopon"
    AUTHOR "Azahar Team \& PalindromicBreadLoaf \& Raibatsu"
    VERSION "${RAIKOPON_VERSION}"
)

set(RAIKOPON_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
file(MAKE_DIRECTORY "${RAIKOPON_ROMFS_DIR}")

# switch-curl links against switch-mbedtls, which (unlike a curl build backed by the Switch's own
# ssl: service) has no access to a system trust store, so the in-app updater's HTTPS requests need
# their own CA bundle to verify GitHub's certificate against. Sourced from curl's own official
# Mozilla CA extraction (https://curl.se/docs/caextract.html) - re-fetch this file periodically
# from that URL to pick up root rotations; it is not auto-updated by this build.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cacert.pem"
    "${RAIKOPON_ROMFS_DIR}/cacert.pem"
    COPYONLY
)
dkp_add_asset_target(raikopon_romfs "${RAIKOPON_ROMFS_DIR}")

# Compile deko3d shaders with uam and pack them into the same romfs.
# Do note that the Deko3D backend was never finished and currently isn't planned to be.
if (ENABLE_DEKO3D)
    find_program(UAM_EXE NAMES uam
        PATHS "$ENV{DEVKITPRO}/tools/bin" "${DEVKITPRO}/tools/bin" "/opt/devkitpro/tools/bin"
        REQUIRED)

    set(DK_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/shaders")
    set(DK_ROMFS_SHADER_DIR "${RAIKOPON_ROMFS_DIR}/shaders")
    file(MAKE_DIRECTORY "${DK_ROMFS_SHADER_DIR}")

    set(DK_DKSH_FILES "")
    # Compile one glsl shader to dksh.
    function(dk_add_shader NAME STAGE)
        add_custom_command(
            OUTPUT "${DK_ROMFS_SHADER_DIR}/${NAME}.dksh"
            COMMAND "${UAM_EXE}" -s ${STAGE}
                -o "${DK_ROMFS_SHADER_DIR}/${NAME}.dksh" "${DK_SHADER_SRC_DIR}/${NAME}.glsl"
            DEPENDS "${DK_SHADER_SRC_DIR}/${NAME}.glsl"
            COMMENT "uam ${STAGE}: ${NAME}.glsl -> ${NAME}.dksh"
            VERBATIM
        )
        set(DK_DKSH_FILES ${DK_DKSH_FILES} "${DK_ROMFS_SHADER_DIR}/${NAME}.dksh" PARENT_SCOPE)
    endfunction()

    dk_add_shader(present_vsh vert)
    dk_add_shader(present_fsh frag)
    dk_add_shader(ubershader_vsh vert)
    dk_add_shader(ubershader_fsh frag)

    add_custom_target(dekopon_shaders DEPENDS ${DK_DKSH_FILES})
    add_dependencies(raikopon_romfs dekopon_shaders)
    set_property(TARGET raikopon_romfs APPEND PROPERTY DKP_ASSET_FILES ${DK_DKSH_FILES})
endif()

nx_create_nro(citra_switch
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${RAIKOPON_NRO_NAME}.nro"
    NACP "${CMAKE_CURRENT_BINARY_DIR}/${RAIKOPON_NRO_NAME}.nacp"
    ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg"
    ROMFS raikopon_romfs
)
