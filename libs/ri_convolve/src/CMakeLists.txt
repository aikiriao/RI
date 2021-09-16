target_sources(${LIB_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ri_fft_convolve.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ri_karatsuba.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ri_zerolatency_fft_convolve.c
    )
