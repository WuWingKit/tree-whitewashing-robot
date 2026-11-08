/* Project-specific adaptation using ST HAL from the user's ALIENTEK package.
 * See SOURCE_REVIEW.md; no F103 binary or vendor application copied unchanged. */
#include "stm32f4xx_hal.h"
#include "board_config.h"
#include "rc_control.h"

static TIM_HandleTypeDef pwm;
static UART_HandleTypeDef serial;
static IWDG_HandleTypeDef watchdog;
static rc_control_t control;
static rc_parser_t parser;
static volatile uint8_t rx[128],head,tail,rx_fault;
static const uint32_t channels[]={TIM_CHANNEL_1,TIM_CHANNEL_2,TIM_CHANNEL_3,TIM_CHANNEL_4};
static GPIO_TypeDef *const dir1[]={GPIOG,GPIOG,GPIOD,GPIOD};
static GPIO_TypeDef *const dir2[]={GPIOG,GPIOF,GPIOD,GPIOE};
static const uint16_t pins1[]={GPIO_PIN_11,GPIO_PIN_13,GPIO_PIN_3,GPIO_PIN_7};
static const uint16_t pins2[]={GPIO_PIN_12,GPIO_PIN_8,GPIO_PIN_6,GPIO_PIN_2};
static const int signs[]={MOTOR1_SIGN,MOTOR2_SIGN,MOTOR3_SIGN,MOTOR4_SIGN};

static void outputs(void) {
    unsigned i;
    for(i=0;i<4;i++) {
        int value=control.output[i<2?0:1]*signs[i];
        if(!(MOTOR_ENABLE_MASK&(1u<<i))) value=0;
        /* PWM is zero BEFORE changing direction. Never PWM the IN pins. */
        if(!value) {
            __HAL_TIM_SET_COMPARE(&pwm,channels[i],0);
            HAL_GPIO_WritePin(dir1[i],pins1[i],GPIO_PIN_RESET);
            HAL_GPIO_WritePin(dir2[i],pins2[i],GPIO_PIN_RESET);
        } else {
            HAL_GPIO_WritePin(dir1[i],pins1[i],value>0?GPIO_PIN_RESET:GPIO_PIN_SET);
            HAL_GPIO_WritePin(dir2[i],pins2[i],value>0?GPIO_PIN_SET:GPIO_PIN_RESET);
            if(value<0) value=-value;
            __HAL_TIM_SET_COMPARE(&pwm,channels[i],(uint32_t)value*(PWM_PERIOD+1)/1000u);
        }
    }
}
static void fatal(void) {
    /* Works even before HAL PWM initialization; reset drives pins to inputs. */
    __HAL_RCC_TIM1_CLK_ENABLE();TIM1->CCR1=0;TIM1->CCR2=0;TIM1->CCR3=0;TIM1->CCR4=0;
    TIM1->BDTR&=~TIM_BDTR_MOE;
    __disable_irq();while(1) {} /* IWDG resets if it was already started. */
}
static void check(HAL_StatusTypeDef status) {if(status!=HAL_OK) fatal();}
static void clock_init(void) {
    RCC_OscInitTypeDef o={0};RCC_ClkInitTypeDef c={0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    /* ALIENTEK F407 board: HSE 8 MHz, 336/8/2 => 168 MHz. */
    o.OscillatorType=RCC_OSCILLATORTYPE_HSE;o.HSEState=RCC_HSE_ON;
    o.PLL.PLLState=RCC_PLL_ON;o.PLL.PLLSource=RCC_PLLSOURCE_HSE;
    o.PLL.PLLM=8;o.PLL.PLLN=336;o.PLL.PLLP=RCC_PLLP_DIV2;o.PLL.PLLQ=7;
    check(HAL_RCC_OscConfig(&o));
    c.ClockType=RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK;c.AHBCLKDivider=RCC_SYSCLK_DIV1;
    c.APB1CLKDivider=RCC_HCLK_DIV4;c.APB2CLKDivider=RCC_HCLK_DIV2;
    check(HAL_RCC_ClockConfig(&c,FLASH_LATENCY_5));
}
static void gpio_init(void) {
    GPIO_InitTypeDef g={0};unsigned i;
    __HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();__HAL_RCC_GPIOF_CLK_ENABLE();__HAL_RCC_GPIOG_CLK_ENABLE();
    /* Disable board SRAM (CS PG10). Do NOT enable FSMC or SRAM routines. */
    HAL_GPIO_WritePin(GPIOG,GPIO_PIN_10,GPIO_PIN_SET);
    g.Pin=GPIO_PIN_10;g.Mode=GPIO_MODE_OUTPUT_PP;g.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG,&g);
    for(i=0;i<4;i++) {
        HAL_GPIO_WritePin(dir1[i],pins1[i],GPIO_PIN_RESET);
        HAL_GPIO_WritePin(dir2[i],pins2[i],GPIO_PIN_RESET);
        g.Pin=pins1[i];HAL_GPIO_Init(dir1[i],&g);
        g.Pin=pins2[i];HAL_GPIO_Init(dir2[i],&g);
    }
    /* Optional active-low stop button: PE0 -> normally-open button -> GND. */
    g.Pin=GPIO_PIN_0;g.Mode=GPIO_MODE_INPUT;g.Pull=GPIO_PULLUP;HAL_GPIO_Init(GPIOE,&g);
    g.Pin=GPIO_PIN_9|GPIO_PIN_10;g.Mode=GPIO_MODE_OUTPUT_PP;g.Pull=GPIO_NOPULL;
    HAL_GPIO_WritePin(GPIOF,g.Pin,GPIO_PIN_SET);HAL_GPIO_Init(GPIOF,&g);
}
static void pwm_init(void) {
    GPIO_InitTypeDef g={0};TIM_OC_InitTypeDef oc={0};unsigned i;
    __HAL_RCC_TIM1_CLK_ENABLE();
    pwm.Instance=TIM1;pwm.Init.Prescaler=0;pwm.Init.Period=PWM_PERIOD;
    pwm.Init.CounterMode=TIM_COUNTERMODE_UP;pwm.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1;
    pwm.Init.RepetitionCounter=0;check(HAL_TIM_PWM_Init(&pwm));
    oc.OCMode=TIM_OCMODE_PWM1;oc.Pulse=0;oc.OCPolarity=TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity=TIM_OCNPOLARITY_HIGH;oc.OCFastMode=TIM_OCFAST_DISABLE;
    for(i=0;i<4;i++) check(HAL_TIM_PWM_ConfigChannel(&pwm,&oc,channels[i]));
    g.Pin=GPIO_PIN_9|GPIO_PIN_11|GPIO_PIN_13|GPIO_PIN_14;g.Mode=GPIO_MODE_AF_PP;
    g.Pull=GPIO_PULLDOWN;g.Speed=GPIO_SPEED_FREQ_HIGH;g.Alternate=GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOE,&g);
    for(i=0;i<4;i++) check(HAL_TIM_PWM_Start(&pwm,channels[i]));
}
static void uart_init(void) {
    GPIO_InitTypeDef g={0};
    __HAL_RCC_USART2_CLK_ENABLE();
    g.Pin=GPIO_PIN_2|GPIO_PIN_3;g.Mode=GPIO_MODE_AF_PP;g.Pull=GPIO_PULLUP;
    g.Speed=GPIO_SPEED_FREQ_HIGH;g.Alternate=GPIO_AF7_USART2;HAL_GPIO_Init(GPIOA,&g);
    serial.Instance=USART2;serial.Init.BaudRate=115200;
    serial.Init.WordLength=UART_WORDLENGTH_8B;serial.Init.StopBits=UART_STOPBITS_1;
    serial.Init.Parity=UART_PARITY_NONE;serial.Init.Mode=UART_MODE_TX_RX;
    serial.Init.HwFlowCtl=UART_HWCONTROL_NONE;serial.Init.OverSampling=UART_OVERSAMPLING_16;
    check(HAL_UART_Init(&serial));
    HAL_NVIC_SetPriority(USART2_IRQn,2,0);HAL_NVIC_EnableIRQ(USART2_IRQn);
    __HAL_UART_ENABLE_IT(&serial,UART_IT_RXNE);__HAL_UART_ENABLE_IT(&serial,UART_IT_ERR);
}
void USART2_IRQHandler(void) {
    uint32_t status=USART2->SR;
    if(status&(USART_SR_RXNE|USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)) {
        uint8_t byte=(uint8_t)USART2->DR;
        if(status&(USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)) rx_fault=1;
        else if(status&USART_SR_RXNE) {
            uint8_t next=(uint8_t)((head+1u)&127u);
            if(next==tail) rx_fault=1;
            else {rx[head]=byte;head=next;}
        }
    }
}
void SysTick_Handler(void) {HAL_IncTick();}
void HardFault_Handler(void) {fatal();}
void MemManage_Handler(void) {fatal();}
void BusFault_Handler(void) {fatal();}
void UsageFault_Handler(void) {fatal();}

int main(void) {
    uint32_t last_status=0;uint8_t raw[RC_FRAME_SIZE];rc_frame_t frame;
    HAL_Init();clock_init();gpio_init();pwm_init();uart_init();
    rc_control_init(&control,HAL_GetTick());
    watchdog.Instance=IWDG;watchdog.Init.Prescaler=IWDG_PRESCALER_64;
    watchdog.Init.Reload=500;check(HAL_IWDG_Init(&watchdog)); /* ~1s, LSI tolerance applies */
    while(1) {
        uint32_t now=HAL_GetTick();unsigned budget=64;
        rc_control_estop(&control,HAL_GPIO_ReadPin(GPIOE,GPIO_PIN_0)==GPIO_PIN_RESET,now);
        if(rx_fault) {
            __disable_irq();tail=head;rx_fault=0;__enable_irq();
            parser.used=0;rc_control_stop(&control,now);
        }
        while(tail!=head && budget--) {
            uint8_t byte=rx[tail];tail=(uint8_t)((tail+1u)&127u);
            if(rc_feed(&parser,byte,&frame)) rc_control_command(&control,&frame,now);
        }
        rc_control_tick(&control,now);outputs();
        if((uint32_t)(now-last_status)>=100u) {
            last_status=now;frame.type=control.estop?RC_STATUS_ESTOP:
                (control.armed?RC_STATUS_ARMED:RC_STATUS_DISARMED);
            frame.seq=control.last_seq;frame.left=control.output[0];frame.right=control.output[1];
            rc_encode(raw,&frame);
            if(HAL_UART_Transmit(&serial,raw,RC_FRAME_SIZE,5)!=HAL_OK) {
                rc_control_stop(&control,now);outputs();
            }
        }
        HAL_GPIO_WritePin(GPIOF,GPIO_PIN_9,control.armed?GPIO_PIN_RESET:GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOF,GPIO_PIN_10,(now/500u)&1u?GPIO_PIN_RESET:GPIO_PIN_SET);
        HAL_IWDG_Refresh(&watchdog);
    }
}
