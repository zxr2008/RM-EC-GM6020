#ifndef CONTROLTASK_H
#define CONTROLTASK_H

/* 这个头文件同时被 C(main.c) 和 C++(ControlTask.cpp) 包含，
   所以不能用 C++ 专属的 <cstdint>，否则 C 编译器会报 "cstdint: No such file" */
#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

// 供 C 中断处理(stm32f4xx_it.c 及 HAL 弱回调)调用的接口
#ifdef __cplusplus
extern "C" {
#endif

// 上电初始化: 配置 CAN 过滤器、启动 CAN 与 1kHz 控制定时器
// 在 main.c 中 MX_XXX_Init() 全部执行完后调用一次
void ControlTaskInit(void);

// 1kHz 控制入口, 在 TIM6 周期中断(HAL_TIM_PeriodElapsedCallback)中调用
void MainTask(void);

// CAN 接收回调, 在 HAL_CAN_RxFifo0MsgPendingCallback 中调用
void CanFeedbackCallback(uint32_t std_id, const uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif // CONTROLTASK_H
