#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Muestra el reloj nocturno con brillo reducido. */
void night_ui_show(void);

#if defined(NIGHT_TTF_BENCHMARK)
void night_ui_run_benchmark(void);
#endif

#ifdef __cplusplus
}
#endif
