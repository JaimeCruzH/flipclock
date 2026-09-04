#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Entra en deep sleep; el boton RESET/EN lo despierta mediante reset externo. */
void power_manager_enter_deep_sleep(void);

#ifdef __cplusplus
}
#endif
