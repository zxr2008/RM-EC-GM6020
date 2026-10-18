/**
  ******************************************************************************
  * @file    ControlTask.cpp
  * @brief
  *
  *   - TIM6 1 kHz：速度环 / 位置环
  *   - 速度给定 ±100 rpm、0.2 Hz；位置给定 ±60°、0.1 Hz
  *   - 电机为电流模式：发 0x1FE 电流帧（16384 ≈ 3 A）
  *   - 速度环：速度前馈 Kff·ω_ref + 摩擦前馈 + PID，输出限幅 ±3000
  *   - 反馈用电机回传 rpm，经一阶低通 α=0.012（约 83 ms）
  *   - 掉线 100 ms 输出清零并复位 PID
  ******************************************************************************
  */

#include "ControlTask.h"
#include "Gm6020.h"
#include "Pid.h"
#include "main.h"

#include <cmath>
#include <cstdint>

/* hcan1 / htim6 / hiwdg 由 CubeMX 在 main.c / can.c / tim.c / iwdg.c 中定义 */
extern "C" {
extern CAN_HandleTypeDef  hcan1;
extern TIM_HandleTypeDef  htim6;
extern IWDG_HandleTypeDef hiwdg;
}

/* ------------------------------------------------------------------------- */
/* 给 Ozone 观察的全局量（速度单位 rpm，位置单位 rad）                       */
/* ------------------------------------------------------------------------- */
extern "C" {
volatile float    g_pos_ref   = 0.0f;   /* rad，相对进入位置模式时的角度 */
volatile float    g_pos_fdb   = 0.0f;
volatile float    g_pos_ref_deg = 0.0f; /* deg，Ozone 直接看角度制 */
volatile float    g_pos_fdb_deg = 0.0f;
volatile float    g_vel_ref   = 0.0f;   /* rpm */
volatile float    g_vel_fdb   = 0.0f;   /* rpm，低通后 */
volatile float    g_vel_cmd   = 0.0f;   /* 速度环给定（rpm） */
volatile float    g_volt      = 0.0f;   /* 输出电流给定 */
volatile float    g_temp      = 0.0f;
volatile uint32_t g_loop_cnt  = 0;
volatile uint32_t g_rx_cnt    = 0;
volatile uint8_t  g_fault_flag = 0;

/* 工作模式：0 = 速度跟随正弦；1 = 位置跟随正弦。
   用 J-Link/Ozone 直接写这个变量就能切换曲线，不用重新烧录。 */
volatile int32_t  g_mode      = 1;

/* 在线可调参数（用 J-Link/Ozone 直接写） */
volatile float    g_spd_kp      = 14.0f;
volatile float    g_spd_ki      = 0.5f;
volatile float    g_spd_kd      = 1.0f;
volatile float    g_cur_per_rpm = 2.5f;
volatile float    g_fric_low    = 650.0f;
volatile float    g_fric_high   = 60.0f;
volatile float    g_fdb_alpha   = 0.012f;
volatile float    g_out_limit   = 3000.0f;
}

namespace {

constexpr float kPi       = 3.14159265359f;
constexpr float kTwoPi    = 6.28318530718f;
constexpr float kRadToRpm = 60.0f / kTwoPi;
constexpr float kRadToDeg = 180.0f / kPi;

/* ========================================================================= */
/* 参数区                                                                    */
/* ========================================================================= */
namespace cfg {

constexpr uint8_t kMotorId = 1;          // GM6020 拨码 ID
constexpr float   kLoopDt  = 0.001f;     // TIM6 1 kHz

/* 速度正弦：±100 rpm，0.2 Hz */
constexpr float kSpdAmpRpm = 100.0f;
constexpr float kSpdFreqHz = 0.2f;

/* 位置正弦：±60°，0.1 Hz */
constexpr float kPosAmpDeg  = 60.0f;
constexpr float kPosFreqHz  = 0.1f;
constexpr float kPosSpdLimit = 20.0f;    // 位置环输出限幅 rad/s

constexpr float kRampInSec = 1.5f;       // 起振渐变

/* 电流模式：0x1FE（16384 ≈ 3 A） */
constexpr bool  kCurrentMode = true;
constexpr float kOutLimit    = 3000.0f;  // 输出限幅（≈0.55 A）

/* 速度前馈：每 rpm 给多少电流 */
constexpr float kCurPerRpm = 2.5f;

/* 速度反馈一阶低通 α（≈83 ms） */
constexpr float kFdbAlpha = 0.012f;

/* 摩擦前馈：零速 650，高速 60，|ω_ref| ≥ 25 rpm 后保持 60 */
constexpr float kFricLow  = 500.0f;
constexpr float kFricHigh = 60.0f;

/* 速度环 PID（电流模式下整定） */
constexpr float kSpdKp = 14.0f, kSpdKi = 0.5f, kSpdKd = 1.0f;
/* 位置环 P */
constexpr float kPosKp = 30.0f, kPosKi = 0.0f, kPosKd = 0.0f;

constexpr uint32_t kCanTimeoutMs = 100;

} // namespace cfg

/* ========================================================================= */
/* 对象与状态                                                                */
/* ========================================================================= */
Gm6020 g_motor(cfg::kMotorId);
Pid    g_spd_pid(cfg::kSpdKp, cfg::kSpdKi, cfg::kSpdKd, cfg::kOutLimit, -cfg::kOutLimit);
Pid    g_pos_pid(cfg::kPosKp, cfg::kPosKi, cfg::kPosKd, cfg::kPosSpdLimit, -cfg::kPosSpdLimit);

float    g_t          = 0.0f;
float    g_theta0     = 0.0f;
float    g_w_filt     = 0.0f;
bool     g_pos_init   = false;
bool     g_motor_seen = false;
uint32_t g_last_rx_ms = 0;

/* ========================================================================= */
/* CAN 发送                                                                  */
/* ========================================================================= */
void SendFrame(void) {
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0) return;

    CAN_TxHeaderTypeDef hdr = {0};
    hdr.StdId = g_motor.txId();
    hdr.IDE   = CAN_ID_STD;
    hdr.RTR   = CAN_RTR_DATA;
    hdr.DLC   = 8;

    uint8_t data[8] = {0};
    g_motor.encode(data);

    uint32_t mailbox = 0;
    (void)HAL_CAN_AddTxMessage(&hcan1, &hdr, data, &mailbox);
}

/* ========================================================================= */
/* 摩擦前馈（按参考 rpm 平滑换向，只在电机落后时补偿）                       */
/* ========================================================================= */
float FrictionFeedforward(float w_ref_rpm, float w_filt_rpm) {
    float dir = w_ref_rpm / 0.5f;          // ±0.5 rpm 过渡带：过零更快给满
    if (dir >  1.0f) dir =  1.0f;
    if (dir < -1.0f) dir = -1.0f;

    /* 衰减看实测转速：卡住时不管给定多大，静摩擦都按零速给足；
       真正转起来后前馈再退掉。用给定速度会在“卡住但给定很大”时
       把前馈错误地衰减掉，导致要等积分慢慢积累才能冲开。 */
    float a = std::fabs(w_filt_rpm) / 25.0f;
    if (a > 1.0f) a = 1.0f;

    const float mag = g_fric_low - (g_fric_low - g_fric_high) * a;
    float out = mag * dir;

    /* 只补偿“电机还没跟上给定”的情况。若实测速度已经超过给定（同方向
       冲过头），摩擦前馈立即退掉，否则它会继续推着电机过冲，PID 的
       反向制动力被抵消，低转速下就会反复粘滑/抖动。 */
    const float lag = (w_ref_rpm - w_filt_rpm) *
                      (w_ref_rpm >= 0.0f ? 1.0f : -1.0f);
    if (lag <= 0.0f) return 0.0f;
    if (lag < 0.5f) return out * (lag / 0.5f);
    return out;
}

/* ========================================================================= */
/* 1 kHz 控制                                                                */
/* ========================================================================= */
void RunControl(void) {
    /* 在线参数生效 */
    static float last_kp = -1.0f, last_ki = -1.0f, last_kd = -1.0f, last_lim = -1.0f;
    if (g_spd_kp != last_kp || g_spd_ki != last_ki || g_spd_kd != last_kd) {
        g_spd_pid.setParams(g_spd_kp, g_spd_ki, g_spd_kd);
        last_kp = g_spd_kp; last_ki = g_spd_ki; last_kd = g_spd_kd;
    }
    if (g_out_limit != last_lim) {
        g_spd_pid.setOutputLimit(g_out_limit, -g_out_limit);
        last_lim = g_out_limit;
    }

    const uint32_t now    = HAL_GetTick();
    const bool     online = g_motor_seen &&
                            ((now - g_last_rx_ms) < cfg::kCanTimeoutMs);

    g_temp = g_motor.temp();

    if (!online) {
        g_spd_pid.reset();
        g_pos_pid.reset();
        g_motor.setVoltage(0);
        g_pos_ref = 0.0f;
        g_pos_fdb = 0.0f;
        g_pos_ref_deg = 0.0f;
        g_pos_fdb_deg = 0.0f;
        g_vel_ref = 0.0f;
        g_vel_fdb = 0.0f;
        g_vel_cmd = 0.0f;
        g_volt    = 0.0f;
        g_fault_flag = 1;
        return;
    }
    g_fault_flag = 0;

    /* 模式切换：复位时间/滤波/PID，位置模式重新取当前角度为中心，
       让位置/速度正弦都从零相位、1.5 s 起振重新开始。 */
    static int32_t last_mode = -1;
    if (g_mode != last_mode) {
        g_t        = 0.0f;
        g_w_filt   = 0.0f;
        g_pos_init = false;
        g_spd_pid.reset();
        g_pos_pid.reset();
        last_mode  = g_mode;
    }

    const float ramp = (g_t < cfg::kRampInSec) ? (g_t / cfg::kRampInSec) : 1.0f;
    float u = 0.0f;

    if (g_mode == 0) {
        /* ---------- 速度环：给定 rpm 正弦 ---------- */
        g_t += cfg::kLoopDt;
        const float w_ref = cfg::kSpdAmpRpm * ramp *
                            std::sin(kTwoPi * cfg::kSpdFreqHz * g_t);

        /* 反馈：电机内置 rpm + 一阶低通 */
        g_w_filt += g_fdb_alpha *
                    (static_cast<float>(g_motor.velRpm()) - g_w_filt);

        u = g_cur_per_rpm * w_ref
          + FrictionFeedforward(w_ref, g_w_filt)
          + g_spd_pid.calc(w_ref, g_w_filt);

        g_vel_ref = w_ref;
        g_vel_fdb = g_w_filt;
        g_vel_cmd = w_ref;
    } else {
        /* ---------- 位置环：给定 rad 正弦 ---------- */
        if (!g_pos_init) {
            g_theta0   = g_motor.angle();
            g_pos_init = true;
        }
        g_t += cfg::kLoopDt;

        const float w_sin = kTwoPi * cfg::kPosFreqHz;
        const float amp   = cfg::kPosAmpDeg * kPi / 180.0f;
        const float th_ref = g_theta0 + amp * ramp * std::sin(w_sin * g_t);
        const float th_fb  = g_motor.angle();

        float w_ref = amp * w_sin * std::cos(w_sin * g_t)
                    + g_pos_pid.calc(th_ref, th_fb);
        if (w_ref >  cfg::kPosSpdLimit) w_ref =  cfg::kPosSpdLimit;
        if (w_ref < -cfg::kPosSpdLimit) w_ref = -cfg::kPosSpdLimit;

        const float w_ref_rpm = w_ref * kRadToRpm;

        g_w_filt += g_fdb_alpha *
                    (static_cast<float>(g_motor.velRpm()) - g_w_filt);

        u = g_cur_per_rpm * w_ref_rpm
          + FrictionFeedforward(w_ref_rpm, g_w_filt)
          + g_spd_pid.calc(w_ref_rpm, g_w_filt);

        g_pos_ref = th_ref - g_theta0;
        g_pos_fdb = th_fb  - g_theta0;
        g_pos_ref_deg = g_pos_ref * kRadToDeg;
        g_pos_fdb_deg = g_pos_fdb * kRadToDeg;
        g_vel_ref = w_ref_rpm;
        g_vel_fdb = g_w_filt;
        g_vel_cmd = w_ref_rpm;
    }

    if (u >  g_out_limit) u =  g_out_limit;
    if (u < -g_out_limit) u = -g_out_limit;

    g_motor.setVoltage(static_cast<int16_t>(u));
    g_volt = u;
}

} // namespace

/* ========================================================================= */
/* 对外接口                                                                  */
/* ========================================================================= */
extern "C" void ControlTaskInit(void) {
    /* 调试暂停时不让看门狗复位 */
    __HAL_DBGMCU_FREEZE_IWDG();

    /* 本工程只用 CAN1；关掉没用到的串口中断，避免抢占 1 kHz 任务 */
    HAL_NVIC_DisableIRQ(USART3_IRQn);
    HAL_NVIC_DisableIRQ(USART6_IRQn);

    /* 电流模式：发 0x1FE */
    g_motor.setCurrentMode(cfg::kCurrentMode);

    /* 电机未上电时总线没有 ACK，开启自动退出 bus-off */
    hcan1.Init.AutoBusOff = ENABLE;
    if (HAL_CAN_Init(&hcan1) != HAL_OK) Error_Handler();

    CAN_FilterTypeDef f = {0};
    f.FilterBank           = 0;
    f.FilterMode           = CAN_FILTERMODE_IDMASK;
    f.FilterScale          = CAN_FILTERSCALE_32BIT;
    f.FilterIdHigh         = 0x0000;
    f.FilterIdLow          = 0x0000;
    f.FilterMaskIdHigh     = 0x0000;
    f.FilterMaskIdLow      = 0x0000;
    f.FilterFIFOAssignment = CAN_RX_FIFO0;
    f.FilterActivation     = ENABLE;
    f.SlaveStartFilterBank = 14;
    if (HAL_CAN_ConfigFilter(&hcan1, &f) != HAL_OK) Error_Handler();

    if (HAL_CAN_Start(&hcan1) != HAL_OK) Error_Handler();
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        Error_Handler();
    }

    /* 上电先发两帧 0，防止驱动器残留旧指令 */
    g_motor.setVoltage(0);
    SendFrame();
    HAL_Delay(20);
    SendFrame();

    g_last_rx_ms = HAL_GetTick();
    if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK) Error_Handler();
    HAL_IWDG_Refresh(&hiwdg);
}

extern "C" void MainTask(void) {
    g_loop_cnt = g_loop_cnt + 1u;
    RunControl();
    SendFrame();
    HAL_IWDG_Refresh(&hiwdg);
}

extern "C" void CanFeedbackCallback(uint32_t std_id, const uint8_t *data) {
    if (std_id != g_motor.rxId()) return;
    g_motor.decode(data);
    g_last_rx_ms = HAL_GetTick();
    g_motor_seen = true;
    g_rx_cnt = g_rx_cnt + 1u;
}

/* 说明：HAL_TIM_PeriodElapsedCallback / HAL_CAN_RxFifo0MsgPendingCallback
   已经实现在 Core/Src/stm32f4xx_it.c，这里不再重复定义。 */
