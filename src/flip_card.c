#include "flip_card.h"
#include "assets/flip_assets.h"

#include <stdio.h>

#include <stdlib.h>

/*
 * Como se consigue el volteo sin guardar ni un solo fotograma:
 *
 *   fase 1 (angulo 0 -> 90)   la hoja ALTA vieja se pliega hacia el eje.
 *                       scale_y sigue |cos(angulo)| con el pivote en su borde
 *                       INFERIOR, asi que parece caer sobre la ranura.
 *                       Detras ya esta puesta la mitad alta del valor nuevo.
 *
 *   fase 2 (angulo 90 -> 180) la hoja BAJA nueva se despliega desde el eje,
 *                       con el pivote en su borde SUPERIOR, y aterriza sobre
 *                       la mitad baja vieja.
 *
 * El reparto no es mitad y mitad en tiempo: la hoja acelera al caer, asi que
 * la primera fase se lleva ~65% del tiempo y la segunda ~35%.
 *
 * Las dos cifras de la tarjeta son dos imagenes independientes que reciben
 * exactamente la misma escala y el mismo pivote relativo, asi que se leen como
 * una sola hoja: al ser escalado puramente vertical no hay costura horizontal.
 *
 * El oscurecimiento de la hoja en movimiento no es un objeto encima, sino la
 * propiedad image_recolor de la propia imagen: un objeto extra costaria otra
 * capa de blending en cada frame.
 */

/*
 * Un flip real cae en ~200 ms, pero aqui cada fotograma cuesta ~78 ms, asi que
 * la duracion se elige por CUANTAS POSICIONES caben, no por realismo:
 *   260 ms -> 4 o 5 fotogramas    380 ms -> 5    480 ms -> 6
 * Con 6 quedan cuatro posiciones intermedias claras. Por debajo de eso la
 * caida se lee como un salto; muy por encima, como algo que flota.
 */
#define FLIP_MS         480

/* La hoja arranca ya inclinada: el primer fotograma en la posicion de reposo
 * no se distingue del estado anterior al volteo, o sea que se desperdicia. */
#define FLIP_DEG0        25
#define LEAF_DARK_MAX   150     /* con pocos fotogramas, oscurecer mas se lee como un parpadeo */
#define DROP_SHADOW_MAX 120     /* sombra que la hoja proyecta al aterrizar */
#define SCALE_FULL      256

struct flip_card_t {
    lv_obj_t *cont;
    int       card_idx;
    int       value;
    int       next;
    lv_obj_t *bg_top[2];    /* mitades altas: durante el volteo ya son las nuevas */
    lv_obj_t *bg_bot[2];    /* mitades bajas: siguen siendo las viejas */
    lv_obj_t *leaf[2];      /* la hoja en movimiento */
    lv_obj_t *drop;         /* sombra proyectada sobre la mitad baja */
    bool      animating;
    bool      phase2;
    uint32_t  frames;       /* fotogramas reales del ultimo volteo (diagnostico) */
    uint32_t  t_start;
};

static const lv_image_dsc_t *sprite(const flip_card_t *c, int col, int value, int half)
{
    /* col 0/1 dentro de la tarjeta -> columna 0..3 de la escena */
    int pos = c->card_idx * 2 + col;
    int digit = (col == 0) ? (value / 10) % 10 : value % 10;
    return &img_digit[pos][digit][half];
}

static void set_halves(flip_card_t *c, int value, bool top, bool bot)
{
    for (int i = 0; i < 2; i++) {
        if (top) lv_image_set_src(c->bg_top[i], sprite(c, i, value, 0));
        if (bot) lv_image_set_src(c->bg_bot[i], sprite(c, i, value, 1));
    }
}

static void leaf_show(flip_card_t *c, bool show)
{
    for (int i = 0; i < 2; i++) {
        if (show) lv_obj_remove_flag(c->leaf[i], LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(c->leaf[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void leaf_set_scale(flip_card_t *c, int32_t scale, lv_opa_t dark)
{
    for (int i = 0; i < 2; i++) {
        lv_image_set_scale_y(c->leaf[i], scale < 1 ? 1 : scale);
        lv_obj_set_style_image_recolor_opa(c->leaf[i], dark, 0);
    }
}

/* Entra en la segunda mitad: la hoja pasa a ser la mitad BAJA nueva, y su
 * pivote salta del borde inferior al superior. */
static void enter_phase2(flip_card_t *c)
{
    c->phase2 = true;
    for (int i = 0; i < 2; i++) {
        lv_image_set_src(c->leaf[i], sprite(c, i, c->next, 1));
        lv_obj_set_pos(c->leaf[i], i * FLIP_CELL_W, FLIP_HALF_H);
        lv_image_set_pivot(c->leaf[i], 0, 0);
    }
    lv_obj_remove_flag(c->drop, LV_OBJ_FLAG_HIDDEN);
}

static void flip_exec_cb(void *var, int32_t t)
{
    flip_card_t *c = (flip_card_t *)var;
    c->frames++;

    /*
     * Una sola variable para todo el volteo: el angulo de la hoja, de 0 grados
     * (levantada) a 180 (caida). Lo que se ve en pantalla es su altura
     * aparente, que es |cos(angulo)| -no una parabola inventada-, y por eso el
     * paso por el canto (90 grados, altura cero) sale solo, sin costura entre
     * las dos mitades del movimiento.
     *
     * El angulo avanza con f(p) = 0,75p + 0,25p^2: casi lineal, con solo una
     * pizca de aceleracion al final.
     *
     * La curva anterior (0,35p + 0,65p^2) imitaba mejor la gravedad, pero con
     * tan pocos fotogramas hacia lo contrario de lo que parece: amontonaba el
     * movimiento visible en un tramo cortisimo. Medido, las posiciones caian en
     * 0deg, 24deg, 64deg, 120deg, 180deg -o sea alturas del 100%, 91%, 43%,
     * 50% y 100%-, con los dos primeros fotogramas indistinguibles del reposo y
     * los dos del medio a la misma altura. Se percibia UNA sola posicion
     * intermedia. Lo que se lee como progreso es el ANGULO, asi que repartirlo
     * por igual reparte por igual lo que se ve.
     */
    int32_t p = t;                                       /* 0..1000 */
    int32_t f = (75 * p) / 100 + (25 * p * p) / 100000;  /* 0..1000 */
    int32_t deg = FLIP_DEG0 + ((180 - FLIP_DEG0) * f) / 1000;

    if (deg >= 90 && !c->phase2) enter_phase2(c);

    /* altura aparente = |cos(deg)| = |sin(deg + 90)| */
    int32_t s = (SCALE_FULL * LV_ABS(lv_trigo_sin((int16_t)(deg + 90)))) / LV_TRIGO_SIN_MAX;

    /* golpe seco al aterrizar: un rebote corto en el ultimo tramo */
    if (p > 880) {
        int32_t b = p - 880;                             /* 0..120 */
        s += (9 * b * (120 - b)) / (60 * 60);            /* 0 -> 9 -> 0 */
    }
    leaf_set_scale(c, s, (lv_opa_t)((LEAF_DARK_MAX * (SCALE_FULL - s)) / SCALE_FULL));

    if (c->phase2) {
        /* la sombra se retira a medida que la hoja se acerca a su sitio */
        lv_opa_t sh = (lv_opa_t)((DROP_SHADOW_MAX * (SCALE_FULL - s)) / SCALE_FULL);
        lv_obj_set_style_bg_opa(c->drop, sh, 0);
    }
}

static void flip_done_cb(lv_anim_t *a)
{
    flip_card_t *c = (flip_card_t *)a->var;
    uint32_t ms = lv_tick_elaps(c->t_start);
    printf("[flip] %lu fotogramas en %lu ms = %lu fps\n",
           (unsigned long)c->frames, (unsigned long)ms,
           (unsigned long)(ms ? c->frames * 1000UL / ms : 0));
    c->value = c->next;
    c->animating = false;
    set_halves(c, c->value, true, true);
    leaf_show(c, false);
    lv_obj_add_flag(c->drop, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *new_image(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *o = lv_image_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_image_recolor(o, lv_color_black(), 0);
    lv_obj_set_style_image_recolor_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

flip_card_t *flip_card_create(lv_obj_t *parent, int card_idx, int x, int y)
{
    flip_card_t *c = lv_malloc(sizeof(flip_card_t));
    LV_ASSERT_MALLOC(c);
    lv_memzero(c, sizeof(flip_card_t));
    c->card_idx = card_idx;

    c->cont = lv_obj_create(parent);
    lv_obj_remove_style_all(c->cont);
    lv_obj_set_pos(c->cont, x, y);
    lv_obj_set_size(c->cont, FLIP_CARD_W, FLIP_CARD_H);
    lv_obj_remove_flag(c->cont, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 2; i++) {
        c->bg_top[i] = new_image(c->cont, i * FLIP_CELL_W, 0);
        c->bg_bot[i] = new_image(c->cont, i * FLIP_CELL_W, FLIP_HALF_H);
    }

    /* La sombra va DEBAJO de la hoja (se crea antes): la hoja esta delante de
     * su propia sombra. Degradado de oscuro junto al eje a nada mas abajo. */
    c->drop = lv_obj_create(c->cont);
    lv_obj_remove_style_all(c->drop);
    lv_obj_set_pos(c->drop, 0, FLIP_HALF_H);
    lv_obj_set_size(c->drop, FLIP_CARD_W, FLIP_HALF_H / 2);
    lv_obj_remove_flag(c->drop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(c->drop, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_color(c->drop, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_dir(c->drop, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(c->drop, 0, 0);
    lv_obj_set_style_bg_grad_stop(c->drop, 255, 0);
    lv_obj_set_style_bg_opa(c->drop, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(c->drop, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 2; i++) {
        c->leaf[i] = new_image(c->cont, i * FLIP_CELL_W, 0);
        lv_obj_add_flag(c->leaf[i], LV_OBJ_FLAG_HIDDEN);
    }

    c->value = 0;
    set_halves(c, 0, true, true);
    return c;
}

void flip_card_set_value(flip_card_t *card, int value, bool anim)
{
    if (!card) return;
    value = ((value % 100) + 100) % 100;

    if (card->animating) {
        /* Un cambio durante el volteo: lo termina de golpe y salta al nuevo
         * valor. Solo puede pasar si el reloj se pone en hora mientras gira. */
        lv_anim_delete(card, flip_exec_cb);
        card->animating = false;
        card->phase2 = false;
        leaf_show(card, false);
        lv_obj_add_flag(card->drop, LV_OBJ_FLAG_HIDDEN);
        card->value = card->next;
    }

    if (value == card->value) return;

    if (!anim) {
        card->value = value;
        set_halves(card, value, true, true);
        return;
    }

    card->next = value;
    card->animating = true;
    card->phase2 = false;
    card->frames = 0;
    card->t_start = lv_tick_get();

    /* la mitad alta pasa YA al valor nuevo: queda tapada por la hoja que cae */
    set_halves(card, value, true, false);

    for (int i = 0; i < 2; i++) {
        lv_image_set_src(card->leaf[i], sprite(card, i, card->value, 0));
        lv_obj_set_pos(card->leaf[i], i * FLIP_CELL_W, 0);
        lv_image_set_pivot(card->leaf[i], 0, FLIP_HALF_H);
    }
    leaf_set_scale(card, SCALE_FULL, LV_OPA_TRANSP);
    leaf_show(card, true);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_exec_cb(&a, flip_exec_cb);
    lv_anim_set_completed_cb(&a, flip_done_cb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, FLIP_MS);
    lv_anim_start(&a);
}

int flip_card_get_value(const flip_card_t *card)
{
    return card ? card->value : 0;
}

bool flip_card_is_animating(const flip_card_t *card)
{
    return card ? card->animating : false;
}
