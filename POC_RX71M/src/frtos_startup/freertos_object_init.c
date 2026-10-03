/*
* Copyright (c) 2016 - 2026 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/

/***********************************************************************************************************************
 * File Name    : freertos_object_init.c
 * Description  : define Objects initialization
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include "freertos_start.h"
#include "../frtos_skeleton/task_function.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private global variables and functions
 **********************************************************************************************************************/

/* Start user code for user variables and functions initialization. Do not edit comment generated here */
#include "freertos_helper.h"
/* End user code. Do not edit comment generated here */

/***********************************************************************************************************************
 * Function Name: Kernel_Object_init
 * Description  : This function initializes FreeRTOS objects.
 * Arguments    : None.
 * Return Value : None.
 **********************************************************************************************************************/
void Kernel_Object_init(void)
{
    /************** task creation ****************************/

    /************** semaphore creation ***********************/

    /************** queue creation ***************************/

    /************** software time creation **************************/

    /************** event groups creation ********************/

    /************** stream buffer creation *************************/

    /************** message buffer creation *********************/
    /* Start user code for user initialization for Kernel_Object_init. Do not edit comment generated here */
    /* End user code. Do not edit comment generated here */

} /* End of function Kernel_Object_init()*/

/***********************************************************************************************************************
 * Function Name : Object_init_manual
 * Description   : This function re-initializes FreeRTOS objects and should be called at runtime.
 * Arguments     : None.
 * Return value  : None.
 **********************************************************************************************************************/
void Object_init_manual(void)
{
    /************** task creation ****************************/
    /* Start user code for user initialization for Object_init_manual. Do not edit comment generated here */
    /* End user code. Do not edit comment generated here */
} /* End of function Object_init_manual()*/

/* Start user code for others. Do not edit comment generated here */
/* End user code. Do not edit comment generated here */