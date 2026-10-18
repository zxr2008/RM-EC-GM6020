r"""
plot_vars.py —— 从运行中的单片机里读出变量并画曲线

原理：调试器（DAPLink）通过 SWD 直接读 RAM 里的变量，
      不打断程序运行，所以电机还在转的同时就能采数据。

用法（在 D:\第一部分考核 目录下）：
    python tools\plot_vars.py                                   # 默认看速度环
    python tools\plot_vars.py --vars g_vel_ref g_vel_fdb --dur 6
    python tools\plot_vars.py --vars g_pos_ref g_pos_fdb        # 看位置环
    python tools\plot_vars.py --vars g_volt                     # 看电压
    python tools\plot_vars.py --list                            # 列出可选变量

输出：当前目录下 curve_YYYYmmdd_HHMMSS.png + 同名的 .csv
"""

import argparse
import csv
import datetime
import os
import struct
import subprocess
import sys
import time

# ---------------- 可改的默认值 ----------------
NM_EXE = r"D:\tools\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin\arm-none-eabi-nm.exe"
DEFAULT_ELF = r"D:\第一部分考核\build\C_project.elf"
DEFAULT_TARGET = "stm32f407ig"     # C 板；F103 请用 air32f103xb
DEFAULT_VARS = ["g_vel_ref", "g_vel_fdb"]
DEFAULT_DUR = 5.0                   # 采样多少秒
DEFAULT_HZ = 200.0                  # 每秒采多少个点


def fail(msg):
    print("[错误] " + msg)
    sys.exit(1)


def read_symbols(elf_path):
    """用 nm 把 elf 里的全局变量地址读出来，返回 {变量名: 地址}"""
    if not os.path.exists(elf_path):
        fail("找不到固件: " + elf_path + "\n       先运行 build.bat 编译")
    if not os.path.exists(NM_EXE):
        fail("找不到 arm-none-eabi-nm: " + NM_EXE)

    out = subprocess.run([NM_EXE, elf_path], capture_output=True, text=True)
    if out.returncode != 0:
        fail("nm 执行失败: " + out.stderr.strip())

    table = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        addr, kind, name = parts
        if kind in ("B", "b", "D", "d", "G", "g"):     # 只关心数据段/未初始化段
            try:
                table[name] = int(addr, 16)
            except ValueError:
                continue
    return table


def read_float(target, addr):
    """读 4 个字节，按 IEEE754 单精度解释成 float"""
    raw = target.read32(addr)
    return struct.unpack("<f", struct.pack("<I", raw))[0]


def main():
    ap = argparse.ArgumentParser(description="读取 STM32 变量并画曲线")
    ap.add_argument("--vars", nargs="+", default=DEFAULT_VARS, help="要采的变量名")
    ap.add_argument("--dur", type=float, default=DEFAULT_DUR, help="采样时长（秒）")
    ap.add_argument("--hz", type=float, default=DEFAULT_HZ, help="采样频率（Hz）")
    ap.add_argument("--elf", default=DEFAULT_ELF, help="固件路径")
    ap.add_argument("--target", default=DEFAULT_TARGET, help="芯片型号")
    ap.add_argument("--list", action="store_true", help="只列出固件里的变量")
    ap.add_argument("--demo", action="store_true", help="用仿真数据画一张示例图，不需要硬件")
    args = ap.parse_args()

    symbols = read_symbols(args.elf)

    if args.list or len(symbols) == 0:
        names = sorted(n for n in symbols if n.startswith("g_"))
        print("固件里的 g_ 变量：")
        for n in names:
            print("   %-16s 0x%08X" % (n, symbols[n]))
        return

    missing = [] if args.demo else [v for v in args.vars if v not in symbols]
    if missing:
        fail("固件里没有这些变量: " + ", ".join(missing) + "\n       用 --list 看看有哪些")

    if args.demo:
        print("[demo] 生成仿真数据（不需要硬件，用来看图长什么样）", flush=True)
        import math
        import random
        n = max(1, int(args.dur * args.hz))
        period = 1.0 / args.hz
        rows = []
        for i in range(n):
            t = i * period
            vals = []
            for k, _ in enumerate(args.vars):
                amp = 1.5 / (k + 1)
                vals.append(amp * math.sin(2 * math.pi * 0.2 * t + k * 0.4)
                            + random.gauss(0.0, 0.02))
            rows.append([t] + vals)
    else:
        try:
            from pyocd.core.helpers import ConnectHelper
        except ImportError:
            fail("pyocd 没装。运行: pip install pyocd")

        print("[1/3] 连接调试器 ...", flush=True)
        session = ConnectHelper.session_with_chosen_probe(
            blocking=False,          # 没插调试器就立刻返回 None，不要傻等
            options={
                "target_override": args.target,
                "connect_mode": "attach",     # attach = 不打断正在运行的程序
                "frequency": 1000000,
            }
        )
        if session is None:
            fail("没找到调试器。检查 DAPLink 是否插好、SWD 三根线是否接上")

        session.open()
        target = session.target
        print("      目标: %s" % target.part_number, flush=True)

        n = max(1, int(args.dur * args.hz))
        period = 1.0 / args.hz
        rows = []
        print("[2/3] 采样 %.1f 秒 @ %.0f Hz（%d 个点）..." % (args.dur, args.hz, n), flush=True)
        t0 = time.perf_counter()
        try:
            for i in range(n):
                now = time.perf_counter() - t0
                vals = []
                for v in args.vars:
                    try:
                        vals.append(read_float(target, symbols[v]))
                    except Exception:
                        vals.append(float("nan"))
                rows.append([now] + vals)
                # 按目标周期节流
                sleep = (i + 1) * period - (time.perf_counter() - t0)
                if sleep > 0:
                    time.sleep(sleep)
        finally:
            session.close()

    # 统计
    print("[3/3] 统计：")
    for k, v in enumerate(args.vars):
        col = [r[k + 1] for r in rows if r[k + 1] == r[k + 1]]   # 过滤 nan
        if not col:
            print("   %-14s 全是无效值" % v)
            continue
        print("   %-14s min=%.3f  max=%.3f  mean=%.3f" %
              (v, min(col), max(col), sum(col) / len(col)))

    # 存 CSV
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = "curve_%s.csv" % stamp
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["t"] + args.vars)
        w.writerows(rows)
    print("   数据已存: " + os.path.abspath(csv_path))

    # 画图
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib import font_manager
    except ImportError:
        fail("matplotlib 没装。运行: pip install matplotlib")

    # 让中文标题不乱码
    for name in ("Microsoft YaHei", "SimHei", "SimSun"):
        try:
            font_manager.findfont(name, fallback_to_default=False)
            plt.rcParams["font.sans-serif"] = [name]
            break
        except Exception:
            continue
    plt.rcParams["axes.unicode_minus"] = False

    t = [r[0] for r in rows]
    fig, ax = plt.subplots(figsize=(11, 5))
    for k, v in enumerate(args.vars):
        ax.plot(t, [r[k + 1] for r in rows], linewidth=1.4, label=v)
    ax.set_xlabel("时间 (秒)")
    ax.set_ylabel("数值")
    ax.set_title("STM32 变量曲线  " + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()

    png_path = "curve_%s.png" % stamp
    fig.savefig(png_path, dpi=130)
    print("   曲线已存: " + os.path.abspath(png_path))

    # Windows 上直接弹出来看
    try:
        os.startfile(png_path)
    except Exception:
        pass


if __name__ == "__main__":
    main()
