#include "Key.h"
#include "cmsis_os.h"
#include "main.h"

/* 引脚已在 CubeMX 的 MX_GPIO_Init 里配成输入+上拉，这里无需再配置 */
void Key_Init(void)
{
    /* 空：GPIO 由 MX_GPIO_Init 初始化 */
}

/* 返回 0=无按键；1~4=对应按键(按下)。带 20ms 消抖并等待松手。
   注意：在 OS 任务里用 osDelay 让出 CPU（不能用阻塞式 Delay） */
uint8_t Key_GetNum(void)
{
    if(HAL_GPIO_ReadPin(Key1_GPIO_Port, Key1_Pin) == GPIO_PIN_RESET)
    {
        osDelay(20);
        while(HAL_GPIO_ReadPin(Key1_GPIO_Port, Key1_Pin) == GPIO_PIN_RESET) osDelay(20);
        return 1;
    }
    if(HAL_GPIO_ReadPin(Key2_GPIO_Port, Key2_Pin) == GPIO_PIN_RESET)
    {
        osDelay(20);
        while(HAL_GPIO_ReadPin(Key2_GPIO_Port, Key2_Pin) == GPIO_PIN_RESET) osDelay(20);
        return 2;
    }
    if(HAL_GPIO_ReadPin(Key3_GPIO_Port, Key3_Pin) == GPIO_PIN_RESET)
    {
        osDelay(20);
        while(HAL_GPIO_ReadPin(Key3_GPIO_Port, Key3_Pin) == GPIO_PIN_RESET) osDelay(20);
        return 3;
    }
    if(HAL_GPIO_ReadPin(Key4_GPIO_Port, Key4_Pin) == GPIO_PIN_RESET)
    {
        osDelay(20);
        while(HAL_GPIO_ReadPin(Key4_GPIO_Port, Key4_Pin) == GPIO_PIN_RESET) osDelay(20);
        return 4;
    }
    return 0;
}
