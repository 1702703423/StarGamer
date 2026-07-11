#ifndef GUI_START_H
#define GUI_START_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*GUI_StartReadyCallback_t)(void);

int GUI_Start_Show(GUI_StartReadyCallback_t ready_callback);

#ifdef __cplusplus
}
#endif

#endif
