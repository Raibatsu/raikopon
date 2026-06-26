# Dekopon frontend
add_executable(citra_switch
    citra_switch.cpp
)

nx_generate_nacp(OUTPUT dekopon.nacp
    NAME "Dekopon"
    AUTHOR "Azahar Team \& PalindromicBreadLoaf"
    VERSION "0.0.1"
)

nx_create_nro(citra_switch
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dekopon.nro"
    NACP "${CMAKE_CURRENT_BINARY_DIR}/dekopon.nacp"
)
