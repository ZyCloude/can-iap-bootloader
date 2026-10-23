# -*- coding: utf-8 -*-
"""
把 App.bin 通过串口下发给母机（帧格式: AA 55 + 长度4字节小端 + 数据）

用法:
    python send_fw.py                     # 用下面的默认 PORT
    python send_fw.py COM10               # 指定端口
    python send_fw.py COM10 115200        # 指定端口和波特率

⚠️ 运行前【必须关闭 XCOM 的串口】，否则端口被占用会报错
   （本脚本会自己把母机的打印读回来，不用再切 XCOM）
"""

import os
import struct
import sys
import time
import zlib          # ★ CRC32（标准 IEEE 802.3，和 C 端 Crc32_Calc 一致）

import serial

# ─────────────────── 可改参数 ───────────────────
PORT = "COM10"
BAUD = 115200
BIN  = r"D:\CubeMX\RTOS\BL\Boot loader产品化-APP端\MDK-ARM\App.bin"   # ★ 指向产品化-APP端
# ────────────────────────────────────────────────

HEAD = b"\xAA\x55"


def main():
    # 用法: python send_fw.py [COMx] [波特率] [--corrupt]
    #   --corrupt : 故障注入，故意翻转 1 个字节，用来验证母机的 CRC32 能不能拦住
    _args  = [a for a in sys.argv[1:] if not a.startswith("--")]
    _flags = set(a for a in sys.argv[1:] if a.startswith("--"))

    port    = _args[0] if len(_args) > 0 else PORT
    baud    = int(_args[1]) if len(_args) > 1 else BAUD
    corrupt = "--corrupt" in _flags

    if not os.path.isfile(BIN):
        print("[错误] 找不到固件: %s" % BIN)
        return 1

    data = open(BIN, "rb").read()

    # ★★★ 先用【原始数据】算 CRC32
    #   故障注入时只改要发送的数据、CRC 仍保留原始值
    #   → 母机算出的本地 CRC ≠ 收到的 CRC → 才能拦住
    crc = zlib.crc32(data) & 0xFFFFFFFF

    if corrupt:
        # ★★★ 故障注入：翻转 1 个字节，模拟"串口传输出错"
        #     长度不变、只有内容变了 → 母机的整包 CRC32 应该能拦住
        _pos = len(data) // 2
        _buf = bytearray(data)
        _buf[_pos] ^= 0xFF
        data = bytes(_buf)
        print("★★★ 故障注入: 已翻转第 %d 字节 (^0xFF)，用于验证 CRC32" % _pos)

    print("固件 : %s" % BIN)
    print("大小 : %d 字节" % len(data))
    print("端口 : %s @ %d" % (port, baud))
    print("-" * 46)

    try:
        ser = serial.Serial(port, baud, timeout=0.2, write_timeout=10.0)
    except serial.SerialException as e:
        print("[错误] 打开串口失败: %s" % e)
        print("       ★ 是不是 XCOM 还占着这个口？先关掉 XCOM 的串口")
        return 1

    with ser:
        ser.reset_input_buffer()          # 丢掉上电时的残留打印

        # ─── 发送 ───
        # ★ 帧格式: [0xAA][0x55][长度 4B][数据 N][CRC32 4B]
        #   crc 已在上面按【原始数据】算好；故障注入时它保持不变，
        #   这样母机算出的本地 CRC 才会和收到的对不上
        frame = HEAD + struct.pack("<I", len(data)) + data + struct.pack("<I", crc)
        print("CRC32: %08X" % crc)
        print("发送 %d 字节（6B 帧头+长度 + %dB 数据 + 4B CRC32）..." % (len(frame), len(data)))
        t0 = time.time()

        # ★ 分段写（每次 4KB）：
        #   一次写 28KB 时，若母机暂时没在收（比如它卡在互斥量上），
        #   Windows 的串口发送缓冲会满 → 抛 Write timeout，整块失败。
        #   分段后即使某段慢，也不会整块丢；而且日志能看出卡在哪一段。
        CHUNK = 4096
        for off in range(0, len(frame), CHUNK):
            ser.write(frame[off:off + CHUNK])
        ser.flush()
        print("发送完成，耗时 %.2f 秒" % (time.time() - t0))
        print("-" * 46)
        print("母机回复:")

        # ─── 读回母机打印（最多 10 秒）───
        deadline = time.time() + 10.0
        got = False
        while time.time() < deadline:
            try:
                line = ser.readline()
            except (serial.SerialException, OSError) as e:
                print("  ⚠️ 串口读取中断: %s" % e)
                print("  ★ 常见原因:")
                print("     · XCOM 同时打开了这个端口")
                print("     · USB 转串口(CH340) 掉线 —— 板子复位/断电/USB 线松动")
                print("     · 母机程序崩溃后自动重启")
                break
            if line:
                got = True
                print("  " + line.decode("gbk", errors="replace").rstrip())
        if not got:
            print("  (10 秒内没收到打印)")
            print("  ★ 检查: 母机是否已运行、波特率是否一致、COM 口是否选对")

    return 0


if __name__ == "__main__":
    sys.exit(main())
