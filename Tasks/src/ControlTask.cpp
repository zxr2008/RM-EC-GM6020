/**
 * ControlTask.cpp
 * ------------------------------------------------------------------
 * GM6020 正弦跟随 + 看门狗保护
 *
 * 结构（从上到下就是执行顺序）：
 *   1. 参数区      —— 所有能调的数字都集中在这里
 *   2. 发送函数    —— 把电压变成 8 字节 CAN 帧发出去
 *   3. 初始化      —— 打开 CAN、发两帧 0、启动 1 kHz 心跳
 *   4. 主任务      —— 每 1 ms 跑一次的全部控制逻辑
 *   5. 接收        —— 收到电机反馈时拆字节
 *   6. 中断回调    —— 把 HAL 的中断接到上面几个函数
 *
 * 数据流（每 1 毫秒循环一次）：
 *   正弦发生器 → 参考值 → 位置环(可选) → 速度环 → 电压 → CAN → 电机
 *        ↑                                                    │
 *        └──────────────── 电机反馈 ←──────────────────────────┘
 * ------------------------------------------------------------------
 */
#include "ControlTask.h"
#include "Gm6020.h"
#include "Pid.h"
#include "main.h"

#include <math.h>

/* hcan1 / htim6 / hiwdg 是 CubeMX 在 main.c 里定义的全局句柄 */
extern "C" {
extern CAN_HandleTypeDef  hcan1;
extern TIM_HandleTypeDef  htim6;
extern IWDG_HandleTypeDef hiwdg;
}

/* ==================================================================
 * 1. 参数区：现场只改这里
 * ================================================================== */
namespace {

/* ---------- 1.1 电机与工作模式 ---------- */
constexpr uint8_t kMotorId = 1;   // 必须和电机外壳上的拨码开关一致
constexpr int     kMode    = 0;   // 0 = 速度跟随正弦；1 = 位置跟随正弦

/* ---------- 1.2 正弦参考 ---------- */
constexpr float kFreqHz = 0.2f;   // 0.2 Hz → 5 秒一个周期（肉眼看得清）
constexpr float kPosAmp = 0.6f;   // 位置幅值 ±0.6 rad ≈ ±34°
constexpr float kVelAmp = 1.5f;   // 速度幅值 ±1.5 rad/s ≈ ±14 rpm

/* ---------- 1.3 限幅与保护 ---------- */
constexpr float kVelRefMax = 3.0f;    // 位置环输出（速度目标）的限幅
constexpr float kVoltMax   = 2000.0f; // 输出电压限幅；满量程 25000 ≈ 24 V
                                      // 2000 ≈ 1.9 V ≈ 空载 26 rpm，第一次上电够安全
constexpr float kVelFdbMax = 35.0f;   // 超速保护阈值 rad/s
constexpr float kTempMax   = 70.0f;   // 过温保护阈值 ℃
constexpr uint16_t kLostMax = 100;    // 掉线保护：连续多少毫秒没反馈就锁死

constexpr float kPi = 3.14159265358979f;
constexpr float kFF = 1.0f;           // 位置模式的速度前馈系数

/* ---------- 1.4 对象 ---------- */
Gm6020 g_motor(kMotorId);

/* 位置环：输入角度误差(rad)，输出速度目标(rad/s)
 * 只给 P，因为速度环已经提供了阻尼，位置环再加 D 容易抖 */
Pid g_pos_pid(5.0f, 0.0f, 0.0f, kVelRefMax, -kVelRefMax);

/* 速度环：输入速度误差(rad/s)，输出电压给定
 * Kp 1200 / Ki 6000 是仿真调出来的：纯 P 跟不上正弦，必须靠 I 顶；
 * 而 Kp 又不能太大，因为转速反馈量化单位是 1 rpm，Kp 太大会被量化噪声放大成抖动 */
Pid g_vel_pid(1200.0f, 6000.0f, 0.0f, kVoltMax, -kVoltMax);

bool g_fault = false;   // 故障锁存：一旦置位只有断电重启才清除

/* ==================================================================
 * 2. 发送函数：把当前电压打成 8 字节发出去
 * ================================================================== */
void sendVoltageFrame(void) {
    CAN_TxHeaderTypeDef hdr = {0};
    uint8_t data[8] = {0};      // 8 个格子先全填 0，占住位置

    hdr.StdId = g_motor.txId(); // ID1~4 → 0x1FF，ID5~7 → 0x2FF
    hdr.IDE   = CAN_ID_STD;     // 标准帧（11 位 ID）
    hdr.RTR   = CAN_RTR_DATA;   // 数据帧
    hdr.DLC   = 8;              // 带 8 字节

    g_motor.encode(data);       // 把电压写进自己那两个字节

    /* STM32 只有 3 个发送邮箱，总线异常时会占满。
       先检查再发，避免 HAL_CAN_AddTxMessage 卡住整个 1 kHz 循环 */
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0) {
        uint32_t mailbox = 0;
        HAL_CAN_AddTxMessage(&hcan1, &hdr, data, &mailbox);
    }
}

}  // namespace

/* ==================================================================
 * 2.5 给调试器观察的全局变量
 * ------------------------------------------------------------------
 * volatile 的作用：告诉编译器"这个变量可能被外部（调试器/中断）读取"，
 * 不许优化掉、不许缓存到寄存器。否则 Release 编译时它们会被整段删掉，
 * 你在 Ozone / gdb 里就看不到任何东西。
 * ================================================================== */
extern "C" {
volatile float    g_pos_ref   = 0.0f;   // 位置给定
volatile float    g_pos_fdb   = 0.0f;   // 位置反馈
volatile float    g_vel_ref   = 0.0f;   // 速度给定
volatile float    g_vel_fdb   = 0.0f;   // 速度反馈
volatile float    g_vel_cmd   = 0.0f;   // 位置环输出（速度指令）
volatile float    g_volt      = 0.0f;   // 最终电压给定
volatile float    g_temp      = 0.0f;   // 电机温度
volatile uint32_t g_loop_cnt  = 0;      // 控制循环跑了多少次（判断 1 kHz 是否正常）
volatile uint32_t g_rx_cnt    = 0;      // 收到多少帧反馈（判断通信是否正常）
volatile uint8_t  g_fault_flag = 0;     // 1 = 保护已触发并锁存
}

/* ==================================================================
 * 3. 上电初始化：只执行一次
 * ================================================================== */
extern "C" void ControlTaskInit(void) {
    /* ---------- 3.1 配置 CAN 过滤器 ---------- */
    CAN_FilterTypeDef filter = {0};
    filter.FilterBank           = 0;                     // 用 0 号过滤器组
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;  // 掩码模式
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = 0x0000;
    filter.FilterIdLow          = 0x0000;
    filter.FilterMaskIdHigh     = 0x0000;   // 掩码全 0 = 不筛选，所有帧都收
    filter.FilterMaskIdLow      = 0x0000;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;           // 收到的帧进 FIFO0
    filter.FilterActivation     = ENABLE;
    filter.SlaveStartFilterBank = 14;       // CAN2 用 14~27，这里给 CAN1 让路

    HAL_CAN_ConfigFilter(&hcan1, &filter);
    HAL_CAN_Start(&hcan1);
    HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);

    /* ---------- 3.2 上电先发两帧 0 电压 ----------
       防止驱动器里残留上一轮的指令，让电机一上电就自己动 */
    g_motor.setVoltage(0);
    sendVoltageFrame();
    HAL_Delay(20);              // 此刻 TIM6 还没启动，用阻塞延时是安全的
    sendVoltageFrame();

    /* ---------- 3.3 启动 1 kHz 心跳 ----------
       这一行是"开机键"：没有它，MainTask 永远不会被调用 */
    HAL_TIM_Base_Start_IT(&htim6);

    /* ---------- 3.4 第一次喂狗 ---------- */
    HAL_IWDG_Refresh(&hiwdg);
}

/* ==================================================================
 * 4. 主任务：TIM6 每 1 ms 调用一次
 * ================================================================== */
extern "C" void MainTask(void) {
    /* ---------- 4.1 时间累加 ----------
       static：只初始化一次，函数返回后值仍保留，下一拍接着累加。
       每拍进 1 ms，所以 +0.001 秒。 */
    static float t = 0.0f;
    t += 0.001f;
    if (t > 1000.0f) t -= 1000.0f;    // 防止跑很久之后浮点精度变差

    const float omega = 2.0f * kPi * kFreqHz;   // 角频率 ω = 2πf

    /* ---------- 4.2 生成正弦参考 ---------- */
    const float pos_ref    = kPosAmp * sinf(omega * t);
    const float pos_vel_ff = kPosAmp * omega * cosf(omega * t);  // 位置参考的导数

    /* 速度模式用 sinf 而不是 cosf：sin(0)=0，上电从 0 速平滑起步，
       用 cosf 的话 t=0 就是最大速度，等于上来给一个阶跃。 */
    const float vel_ref = (kMode == 0) ? (kVelAmp * sinf(omega * t))
                                       : pos_vel_ff;

    /* ---------- 4.3 读反馈 ---------- */
    const float pos_fdb = g_motor.angle();
    const float vel_fdb = g_motor.vel();
    const float temp    = g_motor.temp();

    /* ---------- 4.4 位置环（仅位置模式） ---------- */
    float vel_cmd;
    if (kMode == 0) {
        vel_cmd = vel_ref;                        // 速度模式：直接给速度目标
    } else {
        vel_cmd = g_pos_pid.calc(pos_ref, pos_fdb)  // 角度误差 → 速度目标
                + kFF * pos_vel_ff;                 // 前馈：补掉 PID 天生的滞后
        if (vel_cmd >  kVelRefMax) vel_cmd =  kVelRefMax;
        if (vel_cmd < -kVelRefMax) vel_cmd = -kVelRefMax;
    }

    /* ---------- 4.5 速度环 ---------- */
    float volt = g_vel_pid.calc(vel_cmd, vel_fdb);

    /* ---------- 4.6 三重保护 ----------
       1) 超速   2) 过温   3) 掉线（连续 100 ms 没有新反馈） */
    static uint32_t last_rx = 0;
    static uint16_t lost_ms = 0;

    if (g_rx_cnt == last_rx) {          // 这一拍没收到新帧
        if (lost_ms < 1000) lost_ms++;  // 计数器封顶，避免溢出
    } else {
        last_rx = g_rx_cnt;
        lost_ms = 0;
    }

    if (fabsf(vel_fdb) > kVelFdbMax || temp > kTempMax || lost_ms > kLostMax) {
        g_fault = true;
    }

    if (g_fault) {
        volt = 0.0f;            // 锁死输出
        g_vel_pid.reset();      // 清积分，避免解除后猛冲
        g_pos_pid.reset();
    }

    /* ---------- 4.7 下发 ---------- */
    g_motor.setVoltage(static_cast<int16_t>(volt));
    sendVoltageFrame();

    /* ---------- 4.8 给调试器看的观测量 ----------
       volatile：防止编译器优化掉（否则 Release 下这些变量会被删） */
    g_pos_ref     = pos_ref;
    g_pos_fdb     = pos_fdb;
    g_vel_ref     = (kMode == 0) ? vel_ref : vel_cmd;
    g_vel_fdb     = vel_fdb;
    g_vel_cmd     = vel_cmd;
    g_volt        = volt;
    g_temp        = temp;
    g_fault_flag  = g_fault ? 1 : 0;
    g_loop_cnt = g_loop_cnt + 1;   // 显式写法，避免 volatile 自增的编译警告

    /* ---------- 4.9 喂狗 ----------
       放在最后一行：只有整段逻辑正常跑完才喂。
       如果程序卡死或跑飞，这一行执行不到，0.8 秒后看门狗复位。 */
    HAL_IWDG_Refresh(&hiwdg);
}

/* ==================================================================
 * 5. 收到一帧反馈
 * ================================================================== */
extern "C" void CanFeedbackCallback(uint32_t std_id, const uint8_t *data) {
    if (std_id == g_motor.rxId()) {   // 0x204 + ID，只认自己那台
        g_motor.decode(data);         // 8 字节 → 角度/转速/电流/温度
        g_rx_cnt = g_rx_cnt + 1;      // 掉线保护靠它判断"还在不在收数据"
    }
}

/* ==================================================================
 * 6. HAL 弱函数重写：把中断接到上面的函数
 * ================================================================== */

/* TIM6 更新中断 → 1 kHz 主任务 */
extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM6) {
        MainTask();
    }
}

/* CAN1 收到一帧 → 取出数据 → 交给 CanFeedbackCallback */
extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
    if (hcan->Instance != CAN1) return;

    CAN_RxHeaderTypeDef hdr;
    uint8_t data[8];
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &hdr, data) != HAL_OK) return;

    if (hdr.IDE == CAN_ID_STD) {          // 只处理标准帧
        CanFeedbackCallback(hdr.StdId, data);
    }
}
