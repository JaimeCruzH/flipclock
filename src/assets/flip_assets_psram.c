#include "flip_assets_psram.h"
#include "flip_assets.h"

#include <string.h>
#include "esp_heap_caps.h"

extern const uint8_t flip_assets_blob[];

static void rebase(lv_image_dsc_t *d, const uint8_t *nuevo)
{
    d->data = nuevo + (d->data - flip_assets_blob);
}

void flip_assets_use_psram(void)
{
    /* El fondo es lo ultimo que mete gen_assets.py en el blob, asi que su
     * offset mas su tamaño es el tamaño total. Se calcula aqui para no
     * depender de que el generador emita una constante. */
    const size_t total = (size_t)(img_case_bg.data - flip_assets_blob)
                       + img_case_bg.data_size;

    uint8_t *ram = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!ram) return;                 /* sin PSRAM libre: seguir desde flash */
    memcpy(ram, flip_assets_blob, total);

    for (int c = 0; c < 4; c++)
        for (int d = 0; d < 10; d++)
            for (int h = 0; h < 2; h++)
                rebase(&img_digit[c][d][h], ram);
    rebase(&img_case_bg, ram);
}
