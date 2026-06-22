#!/usr/bin/env python3
"""
X210 串口文件传输脚本
通过 base64 编码传输文件
"""
import serial
import time
import sys
import base64
import os

SERIAL_PORT = '/dev/ttyUSB0'
BAUD_RATE = 115200

def connect():
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=10)
    time.sleep(1)
    ser.reset_input_buffer()
    return ser

def send_command(ser, cmd, wait=1):
    """发送命令并返回输出"""
    ser.reset_input_buffer()
    ser.write((cmd + '\n').encode())
    time.sleep(wait)
    output = b''
    while ser.in_waiting > 0:
        output += ser.read(ser.in_waiting)
    return output.decode('utf-8', errors='ignore')

def transfer_file_to_pc(ser, remote_path, local_path):
    """从X210传输文件到PC"""
    print(f"传输文件: {remote_path} -> {local_path}")

    # 在X210上执行base64编码
    print("在X210上执行base64编码...")
    send_command(ser, f"cat {remote_path} | base64 > /tmp/audio_b64.txt", wait=2)
    
    # 获取文件大小
    response = send_command(ser, "wc -c /tmp/audio_b64.txt", wait=1)
    print(f"编码后大小: {response.strip()}")

    # 分块读取并传输
    print("开始读取base64数据...")
    CHUNK = 4096
    all_data = []
    
    for i in range(100):  # 最多100次读取
        cmd = f"dd if=/tmp/audio_b64.txt bs={CHUNK} skip={i} count=1 2>/dev/null"
        response = send_command(ser, cmd, wait=1)
        # 清理提示符
        clean = []
        for line in response.split('\n'):
            if not line.startswith('[') and 'root' not in line and 'dd if' not in line:
                clean.append(line)
        data = '\n'.join(clean).strip()
        if not data or len(data) < 10:
            break
        all_data.append(data)
        print(f"  块 {i+1}: {len(data)} 字节")
    
    full_b64 = ''.join(all_data)
    print(f"总共接收 {len(full_b64)} 字节")

    # 解码
    try:
        audio_data = base64.b64decode(full_b64)
        with open(local_path, 'wb') as f:
            f.write(audio_data)
        print(f"成功! 保存到: {local_path}")
        print(f"文件大小: {os.path.getsize(local_path)} 字节")
    except Exception as e:
        print(f"解码失败: {e}")
        # 保存原始数据
        with open(local_path + '.b64', 'w') as f:
            f.write(full_b64)
        print(f"已保存到: {local_path}.b64")

def main():
    if len(sys.argv) < 3:
        print("用法: python3 serial_transfer.py <remote_file> <local_file>")
        sys.exit(1)

    remote_path = sys.argv[1]
    local_path = sys.argv[2]

    print(f"连接到 {SERIAL_PORT}...")
    ser = connect()
    print("连接成功!")

    transfer_file_to_pc(ser, remote_path, local_path)
    ser.close()

if __name__ == '__main__':
    main()
