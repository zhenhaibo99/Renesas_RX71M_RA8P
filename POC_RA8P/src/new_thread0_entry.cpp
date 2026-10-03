#include "new_thread0.h"
#include "board/board_resources.hpp"

/* FSP 生成的 New Thread 入口。栈和优先级在 ra_gen/new_thread0.c 里配置。 */
/* pvParameters 里是任务句柄，本应用不用。 */
void new_thread0_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED (pvParameters); /* 显式丢掉未使用参数，避免告警。 */
    board_app_run();                       /* 进入板级初始化，并在里面循环，不再返回。 */
}
