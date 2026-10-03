/*
* Copyright (c) 2016 - 2026 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/

/***********************************************************************************************************************
 * File Name    : freertos_object_init.h
 * Description  : Header file for FreeRTOS Objects declarations
 **********************************************************************************************************************/

#ifndef FREERTOS_OBJECT_INIT_H
#define FREERTOS_OBJECT_INIT_H

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include "FreeRTOS.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Exported global variables and functions
 **********************************************************************************************************************/

/* FreeRTOS Object Handle Declarations */

/* Function prototypes */
extern void Kernel_Object_init(void);
extern void Object_init_manual(void);

/* Start user code for user declarations. Do not edit comment generated here */
/* End user code. Do not edit comment generated here */

#endif /* FREERTOS_OBJECT_INIT_H */