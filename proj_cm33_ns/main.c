/*******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for non-secure
*                    application in the CM33 CPU
*
* Related Document : See README.md
*
********************************************************************************
* Copyright 2023-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/

#include "cybsp.h"
#include "retarget_io_init.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define CM55_BOOT_WAIT_TIME_USEC      (10U)
#define GPIO_INTERRUPT_PRIORITY       (7U)
#define PORT_INTR_MASK                (0x00000001UL << 8U)
#define MASKED_TRUE                   (1U)
#define USER_BTN_PRESS_DELAY_MS       (50U)
#define DPLL_ENABLE_TIMEOUT_MS        (10000U)
#define DPLL_INTPUT_FREQ_HZ           (50000000U)
#define DPLL_FREQ_HP_HZ               (400000000U)
#define DPLL_FREQ_LP_HZ               (140000000U)
#define UART_HP_DIV                   (86U)
#define UART_LP_DIV                   (30U)
#define UART_ULP_DIV                  (10U)
#define WAIT_FOR_TX_COMPLETE()        while (!(Cy_SCB_UART_IsTxComplete \
                                                       (CYBSP_DEBUG_UART_HW)))
/* App boot address for CM55 project */
#define CM55_APP_BOOT_ADDR          (CYMEM_CM33_0_m55_nvm_START + \
                                        CYBSP_MCUBOOT_HEADER_SIZE)

/******************************************************************************
 * Typedefs
 ******************************************************************************/
typedef enum
{
    HIGH_PERFORMANCE = 0U,
    LOW_POWER,
    ULTRA_LOW_POWER,
    SYSTEM_DEEP_SLEEP,
    SYSTEM_HIBERNATE
} en_power_mode_t;

/******************************************************************************
 * Global Variables
 ******************************************************************************/

/* Interrupt configuration structure */
cy_stc_sysint_t intrCfg =
{
    CYBSP_USER_BTN_IRQ,
    GPIO_INTERRUPT_PRIORITY
};

volatile bool user_btn_flag = false;

en_power_mode_t next_power_state = HIGH_PERFORMANCE;

/*******************************************************************************
* Function Name: gpio_interrupt_handler
********************************************************************************
* Summary:
* USER BUTTON 1 interrupt handler.
*
* Parameters:
* void
*
* Return:
* void
*
*******************************************************************************/
static void gpio_interrupt_handler(void)
{
    if (Cy_GPIO_GetInterruptStatus(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN))
    {
        user_btn_flag = true;
        Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN);
    }
    if (Cy_GPIO_GetInterruptStatus(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN))
    {
        Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    }
}

/******************************************************************************
 * Function Name: dpll_lp_set_freq
 ******************************************************************************
 * Summary:
 *  Configures the DPLL-LP (Low Power DPLL) frequency.
 *
 * Parameters:
 *  uint32_t frequency
 *
 * Return:
 *  void
 *
 ******************************************************************************/
static void dpll_lp_set_freq(uint32_t freq)
{
    /** Define a PLL configuration structure **/
    cy_stc_pll_config_t dpll_hp;

    /** Set the input frequency of the PLL **/
    dpll_hp.inputFreq = DPLL_INTPUT_FREQ_HZ;

    /** Set the output mode of the PLL to auto **/
    dpll_hp.outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO;

    /** Set the desired output frequency of the PLL **/
    dpll_hp.outputFreq = freq;

    /** Disable the DPLL_HP_0 PLL path **/
    Cy_SysClk_PllDisable(SRSS_DPLL_LP_0_PATH_NUM);

    /** Configure the PLL with the specified settings **/
    if (CY_SYSCLK_SUCCESS !=
            Cy_SysClk_PllConfigure(SRSS_DPLL_LP_0_PATH_NUM, &dpll_hp))
    {
        /** Handle error if PLL configuration fails **/
         handle_app_error();
    }

    /** Enable the DPLL_HP_0 PLL path with a timeout **/
    if (CY_SYSCLK_SUCCESS !=
            Cy_SysClk_PllEnable(SRSS_DPLL_LP_0_PATH_NUM, DPLL_ENABLE_TIMEOUT_MS))
    {
        /** Handle error if PLL enable fails **/
         handle_app_error();
    }
}

/******************************************************************************
 * Function Name: main
 ******************************************************************************
 * Summary:
 * This is the main function of the CM33 non-secure application.
 *
 * Parameters:
*  void
 *
 * Return:
 *  int
 *
 ******************************************************************************/
int main(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_en_syspm_status_t status;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    if (CY_RSLT_SUCCESS != result)
    {
        /* Board Initialization failed */
        handle_app_error();
    }

    /* Initialize retarget-io middleware */
    init_retarget_io();

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");
    printf("************************ "
           "PSOC Edge MCU: Switching Power Mode "
           "************************ \r\n");

    /* Check the reset reason */
    if(CY_SYSLIB_RESET_HIB_WAKEUP ==
        (Cy_SysLib_GetResetReason() & CY_SYSLIB_RESET_HIB_WAKEUP))
    {
        /* The reset has occurred on a wakeup from Hibernate power mode */
        printf("Wakeup from the Hibernate mode\r\n");

        /* Clear reset reason bit */
        Cy_SysLib_ClearResetReason();
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Debounce after enabling internal pull-up on button */
    while (0UL == Cy_GPIO_Read(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN))
    {
        Cy_SysLib_Delay(USER_BTN_PRESS_DELAY_MS);
    }
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN);

    /* Initialize the interrupt and register interrupt callback */
    Cy_SysInt_Init(&intrCfg, &gpio_interrupt_handler);

    NVIC_ClearPendingIRQ(intrCfg.intrSrc);
    /* Enable the interrupt in the NVIC */
    NVIC_EnableIRQ(intrCfg.intrSrc);

    /* Set idle power mode configuration of SOCMEM to deepsleep */
    Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

    /*CY_CM55_APP_BOOT_ADDR must be updated if CM55 memory layout is changed.*/
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_USEC);

    printf("Press BTN1 to switch power mode\r\n");
    for (;;)
    {
        if(user_btn_flag)
        {
            while (0UL == Cy_GPIO_Read(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN))
            {
                Cy_SysLib_Delay(USER_BTN_PRESS_DELAY_MS);
            }
            
            user_btn_flag = false;
            
            switch (next_power_state)
            {
                case HIGH_PERFORMANCE:
                {
                    /** Enter Low Power (LP) mode.*/
                    status = Cy_SysPm_SystemEnterHp();

                    if (CY_SYSPM_SUCCESS != status)
                    {
                        handle_app_error();
                    }

                    /** Check if the system successfully entered ULP mode. */
                    if (Cy_SysPm_ReadStatus() & CY_SYSPM_STATUS_SYSTEM_HP)
                    {
                        /** Set the RRAM to HP voltage mode*/
                        Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_HP);
                        
                        Cy_SysClk_ClkHfSetDivider(CY_CFG_SYSCLK_CLKHF0, CY_SYSCLK_CLKHF_DIVIDE_BY_2);

                        dpll_lp_set_freq(DPLL_FREQ_HP_HZ);

                        /** Set the peripheral clock divider for the debug UART */
                        Cy_SysClk_PeriPclkSetDivider((en_clk_dst_t)CYBSP_DEBUG_UART_CLK_DIV_GRP_NUM,
                                CY_SYSCLK_DIV_16_BIT, 1U, UART_HP_DIV);
                    }

                    printf("Power Mode: System High Performance.\r\n");
                    WAIT_FOR_TX_COMPLETE();
                    next_power_state = LOW_POWER;
                    break;
                }

                case LOW_POWER:
                {
                    dpll_lp_set_freq(DPLL_FREQ_LP_HZ);
                    /** Enter Low Power (LP) mode.*/
                    status = Cy_SysPm_SystemEnterLp();
                    if (CY_SYSPM_SUCCESS != status)
                    {
                        handle_app_error();
                    }

                    /** Check if the system successfully entered ULP mode. */
                    if (Cy_SysPm_ReadStatus() & CY_SYSPM_STATUS_SYSTEM_LP)
                    {
                        /** Set the RRAM to LP voltage mode for lower power 
                         * consumption. */
                        Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_LP);
                        
                        Cy_SysClk_ClkHfSetDivider(CY_CFG_SYSCLK_CLKHF0, CY_SYSCLK_CLKHF_DIVIDE_BY_2);

                        /** Set the peripheral clock divider for the debug UART */
                        Cy_SysClk_PeriPclkSetDivider((en_clk_dst_t)CYBSP_DEBUG_UART_CLK_DIV_GRP_NUM,
                                CY_SYSCLK_DIV_16_BIT, 1U, UART_LP_DIV);
                    }

                    printf("Power Mode: System Low Power.\r\n");
                    WAIT_FOR_TX_COMPLETE();
                    next_power_state = ULTRA_LOW_POWER;
                    break;
                }

                case ULTRA_LOW_POWER:
                {
                    /** Disable the high-performance DPLL and Directly clock 
                     * from IHO (50 MHz)*/
                    
                    Cy_SysClk_PllDisable(SRSS_DPLL_LP_0_PATH_NUM);

                    /** Enter Ultra-Low Power (ULP) mode.*/
                    status = Cy_SysPm_SystemEnterUlp();
                    if (CY_SYSPM_SUCCESS != status)
                    {
                        handle_app_error();
                    }

                    /** Check if the system successfully entered ULP mode. */
                    if (Cy_SysPm_ReadStatus() & CY_SYSPM_STATUS_SYSTEM_ULP)
                    {
                        /** Set the RRAM to ULP voltage mode for lower power 
                         * consumption. */
                        Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_ULP);

                        /** Set the high-frequency clock (CLKHF) to no divide */
                        Cy_SysClk_ClkHfSetDivider(CY_CFG_SYSCLK_CLKHF0, CY_SYSCLK_CLKHF_NO_DIVIDE);

                        /** Set the peripheral clock divider for the debug UART */
                        Cy_SysClk_PeriPclkSetDivider((en_clk_dst_t)CYBSP_DEBUG_UART_CLK_DIV_GRP_NUM,
                                CY_SYSCLK_DIV_16_BIT, 1U, UART_ULP_DIV);
                    }
                    printf("Power Mode: System Ultra Low Power.\r\n");
                    WAIT_FOR_TX_COMPLETE();

                    next_power_state = SYSTEM_DEEP_SLEEP;
                    break;
                }

                case SYSTEM_DEEP_SLEEP:
                {
                    printf("Power Mode: System Deep Sleep.\r\n");
                    WAIT_FOR_TX_COMPLETE();

                    /*Enter deep sleep mode,and wait for interrupt to wake
                     * up the CPU*/
                    Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);

                    next_power_state = SYSTEM_HIBERNATE;
                    break;
                }

                case SYSTEM_HIBERNATE:
                {
                    printf("Power Mode: System Hibernate.\r\n");
                    WAIT_FOR_TX_COMPLETE();

                    /*Set the wake-up source for hibernate mode to pin 1 low*/
                    Cy_SysPm_SetHibernateWakeupSource(CY_SYSPM_HIBERNATE_PIN1_LOW);

                    Cy_SysPm_SystemEnterHibernate();
                    /* Should never reach here, as the system should be in
                     * hibernate mode now */
                    break;
                }

                default:
                {
                    break;
                }
            }
        }
    }
}

/* [] END OF FILE */