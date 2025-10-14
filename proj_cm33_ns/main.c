/******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for non-secure
*                    application in the CM33 CPU
*
* Related Document : See README.md
*
********************************************************************************
* (c) 2023-2025, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
* 
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
******************************************************************************/

/******************************************************************************
* Header Files
******************************************************************************/

#include "cybsp.h"
#include "retarget_io_init.h"

/******************************************************************************
* Macros
******************************************************************************/
#define CM55_BOOT_WAIT_TIME_USEC      (10U)
#define GPIO_INTERRUPT_PRIORITY       (7U)
#define PORT_INTR_MASK                (0x00000001UL << 8U)
#define MASKED_TRUE                   (1U)
#define USER_BTN_PRESS_DELAY_MS       (50U)
#define DPLL_ENABLE_TIMEOUT_MS        (10000U)
#define DPLL_INPUT_FREQ_HZ            (50000000U)  /* 50 MHz */
#define DPLL_HP_FINAL_FREQ_HZ         (400000000U) /* 400 MHz */
#define DPLL_LP_INITIAL_FREQ_HZ       (75000000U)  /* 75 MHz */
#define DPLL_LP_FINAL_FREQ_HZ         (140000000U) /* 140 MHz */
#define DPLL_ULP_INITIAL_FREQ_HZ      (41000000U)  /* 41 MHz */
#define DPLL_ULP_FINAL_FREQ_HZ        (50000000U)  /* 50 MHz */
#define UART_HP_DIV                   (86U)
#define UART_LP_DIV                   (30U)
#define UART_ULP_DIV                  (10U)

/* App boot address for CM55 project */
#define CM55_APP_BOOT_ADDR          (CYMEM_CM33_0_m55_nvm_START + \
                                        CYBSP_MCUBOOT_HEADER_SIZE)

/*****************************************************************************
 * Typedefs
 *****************************************************************************/
typedef enum
{
    HIGH_PERFORMANCE = 0U,
    LOW_POWER,
    ULTRA_LOW_POWER,
    SYSTEM_DEEP_SLEEP,
    SYSTEM_HIBERNATE
} en_power_mode_t;

/*****************************************************************************
 * Global Variables
 *****************************************************************************/

/* Interrupt configuration structure */
cy_stc_sysint_t intrCfg =
{
    CYBSP_USER_BTN_IRQ,
    GPIO_INTERRUPT_PRIORITY
};

volatile bool user_btn_flag = false;

en_power_mode_t next_power_state = HIGH_PERFORMANCE;

/******************************************************************************
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
******************************************************************************/
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

/*****************************************************************************
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
 *****************************************************************************/
static void dpll_lp_set_freq(uint32_t freq)
{
    /* Define a PLL configuration structure */
    cy_stc_pll_config_t dpll_lp;

    /* Set the input frequency of the PLL */
    dpll_lp.inputFreq = DPLL_INPUT_FREQ_HZ;

    /* Set the output mode of the PLL to auto */
    dpll_lp.outputMode = CY_SYSCLK_FLLPLL_OUTPUT_AUTO;

    /* Set the desired output frequency of the PLL */
    dpll_lp.outputFreq = freq;

    /* Disable the DPLL */
    Cy_SysClk_PllDisable(SRSS_DPLL_LP_0_PATH_NUM);

    /* Configure the DPLL with the specified settings */
    if (CY_SYSCLK_SUCCESS !=
            Cy_SysClk_PllConfigure(SRSS_DPLL_LP_0_PATH_NUM, &dpll_lp))
    {
        /* Handle error if PLL configuration fails */
         handle_app_error();
    }

    /* Enable the DPLL path with a timeout */
    if (CY_SYSCLK_SUCCESS !=
            Cy_SysClk_PllEnable(SRSS_DPLL_LP_0_PATH_NUM, DPLL_ENABLE_TIMEOUT_MS))
    {
        /* Handle error if DPLL enable fails */
         handle_app_error();
    }
}

/*****************************************************************************
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
 *****************************************************************************/
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

    /* CY_CM55_APP_BOOT_ADDR must be updated if CM55 memory layout is changed. */
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
                    status = Cy_SysPm_SystemEnterHp();

                    if (CY_SYSPM_SUCCESS != status)
                    {
                        handle_app_error();
                    }

                    /* Check if the system successfully entered ULP mode. */
                    if (Cy_SysPm_ReadStatus() & CY_SYSPM_STATUS_SYSTEM_HP)
                    {
                        /* Set the RRAM to HP voltage mode*/
                        Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_HP);
                        
                        Cy_SysClk_ClkHfSetDivider(CY_CFG_SYSCLK_CLKHF0, CY_SYSCLK_CLKHF_DIVIDE_BY_2);

                        dpll_lp_set_freq(DPLL_HP_FINAL_FREQ_HZ);

                        /* Set the peripheral clock divider for the debug UART */
                        Cy_SysClk_PeriPclkSetDivider((en_clk_dst_t)CYBSP_DEBUG_UART_CLK_DIV_GRP_NUM,
                                CY_SYSCLK_DIV_16_BIT, 1U, UART_HP_DIV);
                    }

                    printf("Power Mode: System High Performance.\r\n");
                    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW));

                    next_power_state = LOW_POWER;
                    break;
                }

                case LOW_POWER:
                {
                    /* Set the DPLL frequency to the initial frequency */
                    dpll_lp_set_freq(DPLL_LP_INITIAL_FREQ_HZ);

                    /* Attempt to enter the Low Power mode (LP) */
                    status = Cy_SysPm_SystemEnterLp();

                    /* Check if entering LP mode was successful */
                    if (CY_SYSPM_SUCCESS != status)
                    {
                        handle_app_error();
                    }

                    /* Set the DPLL frequency to the final frequency after
                     * LP mode is entered.
                     */
                    dpll_lp_set_freq(DPLL_LP_FINAL_FREQ_HZ);

                    /* Check if the system successfully entered ULP mode. */
                    if (Cy_SysPm_ReadStatus() & CY_SYSPM_STATUS_SYSTEM_LP)
                    {
                        /* Set the RRAM to LP voltage mode for lower power 
                         * consumption. */
                        Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_LP);
                        
                        Cy_SysClk_ClkHfSetDivider(CY_CFG_SYSCLK_CLKHF0, CY_SYSCLK_CLKHF_DIVIDE_BY_2);

                        /* Set the peripheral clock divider for the debug UART */
                        Cy_SysClk_PeriPclkSetDivider((en_clk_dst_t)CYBSP_DEBUG_UART_CLK_DIV_GRP_NUM,
                                CY_SYSCLK_DIV_16_BIT, 1U, UART_LP_DIV);
                    }

                    printf("Power Mode: System Low Power.\r\n");
                    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW));

                    next_power_state = ULTRA_LOW_POWER;
                    break;
                }

                case ULTRA_LOW_POWER:
                {
                    /* Set the DPLL frequency to the initial frequency */
                    dpll_lp_set_freq(DPLL_ULP_INITIAL_FREQ_HZ);

                    /* Attempt to enter the ultra-low-power mode (ULP) */
                    status = Cy_SysPm_SystemEnterUlp();
                    
                    /* Check if entering ULP mode was successful */
                    if (CY_SYSPM_SUCCESS != status)
                    {
                        handle_app_error();
                    }
                    
                    /* Set the DPLL frequency to the final frequency after ULP
                     * mode is entered.
                     */
                    dpll_lp_set_freq(DPLL_ULP_FINAL_FREQ_HZ);
                    
                    /* Check if the system successfully entered ULP mode. */
                    if (Cy_SysPm_ReadStatus() & CY_SYSPM_STATUS_SYSTEM_ULP)
                    {
                        /* Set the RRAM to ULP voltage mode for lower power 
                         * consumption.
                         */
                        Cy_RRAM_SetVoltageMode(RRAMC0, CY_RRAM_VMODE_ULP);

                        /* Set the high-frequency clock (CLKHF) to no divide */
                        Cy_SysClk_ClkHfSetDivider(CY_CFG_SYSCLK_CLKHF0, CY_SYSCLK_CLKHF_NO_DIVIDE);

                        /* Set the peripheral clock divider for the debug UART */
                        Cy_SysClk_PeriPclkSetDivider((en_clk_dst_t)CYBSP_DEBUG_UART_CLK_DIV_GRP_NUM,
                                CY_SYSCLK_DIV_16_BIT, 1U, UART_ULP_DIV);
                    }
                    printf("Power Mode: System Ultra Low Power.\r\n");
                    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW));

                    next_power_state = SYSTEM_DEEP_SLEEP;
                    break;
                }

                case SYSTEM_DEEP_SLEEP:
                {
                    printf("Power Mode: System Deep Sleep.\r\n");
                    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW));

                    /* Enter deep sleep mode,and wait for interrupt to wake up 
                     * the CPU.
                     */
                    Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);

                    next_power_state = SYSTEM_HIBERNATE;
                    break;
                }

                case SYSTEM_HIBERNATE:
                {
                    printf("Power Mode: System Hibernate.\r\n");
                    
                    /* Ensure all UART transmission is complete before
                     * proceeding.
                     */
                    while (!Cy_SCB_UART_IsTxComplete(CYBSP_DEBUG_UART_HW));

                    /* Deinitialize UART for successful wake-up recovery; 
                     * leaving it initialized may cause issues.
                     */
                    Cy_SCB_UART_DeInit(CYBSP_DEBUG_UART_HW);

                    /* Configure the wakeup source for Hibernate mode.
                     * In this case, the system will wake up when PIN1 is pulled
                     * low.
                     */
                    Cy_SysPm_SetHibernateWakeupSource(CY_SYSPM_HIBERNATE_PIN1_LOW);

                    Cy_SysPm_SystemEnterHibernate();
                    
                    /* Should never reach here, as the system should be in
                     * hibernate mode now.
                     */
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