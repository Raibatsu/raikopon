# Dekopon frontend
add_executable(citra_switch
    citra_switch.cpp
    config.cpp
    config.h
    default_ini.h
    emu_window.cpp
    emu_window.h
    emulation.cpp
    horizon_sys_stubs.cpp
)

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
# oaknut_horizon_jit is pulled in transitively via oaknut
target_link_libraries(citra_switch PRIVATE citra_core citra_common inih glad)

target_link_libraries(citra_switch PRIVATE
    ${SWITCH_EGL} ${SWITCH_GLESV2} ${SWITCH_GLAPI} ${SWITCH_DRM_NOUVEAU})

nx_generate_nacp(OUTPUT dekopon.nacp
    NAME "Dekopon"
    AUTHOR "Azahar Team \& PalindromicBreadLoaf"
    VERSION "0.0.1"
)

nx_create_nro(citra_switch
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dekopon.nro"
    NACP "${CMAKE_CURRENT_BINARY_DIR}/dekopon.nacp"
)
