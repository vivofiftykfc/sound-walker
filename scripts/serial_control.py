#!/usr/bin/env python3
"""
X210 串口自动化控制脚本
用法: python3 serial_control.py <command>
"""
import serial
import time
import sys
import os

# 串口配置
SERIAL_PORT = '/dev/ttyUSB0'
BAUD_RATE = 115200
TIMEOUT = 3

def connect():
    """连接串口"""
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=TIMEOUT)
        time.sleep(1)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        return ser
    except serial.SerialException as e:
        print(f"无法打开串口 {SERIAL_PORT}: {e}")
        print("请检查:")
        print("  1. 串口线是否连接")
        print("  2. 串口权限: sudo usermod -a -G dialout $USER")
        print("  3. ttyUSB0 是否正确")
        sys.exit(1)

def send_command(ser, cmd, wait=1):
    """发送命令并返回输出"""
    ser.reset_input_buffer()
    ser.write((cmd + '\n').encode())
    time.sleep(wait)
    output = b''
    while ser.in_waiting > 0:
        output += ser.read(ser.in_waiting)
    return output.decode('utf-8', errors='ignore')

def run_batch(commands):
    """批量执行命令"""
    ser = connect()
    print(f"连接到 {SERIAL_PORT}\n")

    results = {}
    for name, cmd, wait in commands:
        print(f"执行: {cmd}")
        output = send_command(ser, cmd, wait)
        results[name] = output
        print(output)
        print("-" * 40)

    ser.close()
    return results

# 需要执行的命令列表 (名称, 命令, 等待时间)
COMMANDS = [
    ("CPU信息", "cat /proc/cpuinfo | head -10", 1),
    ("内核版本", "cat /proc/version", 1),
    ("存储情况", "df -h", 1),
    ("内存情况", "free -h", 1),
    ("音频设备", "ls /dev/snd/ 2>/dev/null || echo 'no sound'", 1),
    ("Python", "which python3 || which python || echo 'no python'", 1),
    ("GCC版本", "arm-none-linux-gnueabi-gcc -v 2>&1 | tail -3", 2),
    ("GCC路径", "which arm-none-linux-gnueabi-gcc || echo 'not in PATH'", 1),
]

if __name__ == '__main__':
    if len(sys.argv) > 1:
        cmd = ' '.join(sys.argv[1:])
        ser = connect()
        print(f"> {cmd}")
        output = send_command(ser, cmd, 2)
        print(output)
        ser.close()
    else:
        results = run_batch(COMMANDS)
