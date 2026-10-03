#include "new_thread0.h"
#include "board/board_resources.hpp"

/* New Thread entry function */
/* pvParameters contains TaskHandle_t */
void new_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED (pvParameters);
    board_app_run();
}
