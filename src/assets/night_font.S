/* Solo se incluye en la variante de prueba Tiny TTF. */
#if defined(NIGHT_TTF_USE) && NIGHT_TTF_USE
    .section .rodata.night_font,"a",@progbits
    .global night_font_ttf
    .global night_font_ttf_end
    .align 4
night_font_ttf:
    .incbin "vendor/lvgl9/scripts/built_in_font/Montserrat-Medium.ttf"
night_font_ttf_end:
#endif
