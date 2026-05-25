#!/usr/bin/env python3
"""
STM32 Simple USB Monitor - 简化版
专为STM32 USB CDC调试设计的简化监控工具，解决核心痛点。

核心痛点解决方案：
1. DTR/RTS问题：显式设置 ser.dtr = True 和 ser.rts = True
2. 自动重连：STM32复位时自动恢复连接
3. 稳健解码：处理乱码不崩溃

使用方法：
  python stm32_simple_monitor.py COM10
  python stm32_simple_monitor.py COM10 log.txt
"""

import sys
import time
import serial
from datetime import datetime

def main():
    # 检查参数
    if len(sys.argv) < 2:
        print("用法:")
        print("  python stm32_simple_monitor.py <COM端口> [日志文件]")
        print()
        print("示例:")
        print("  python stm32_simple_monitor.py COM10")
        print("  python stm32_simple_monitor.py COM10 log.txt")
        print()
        print("提示:")
        print("  1. 确保STM32已连接并正确供电")
        print("  2. 程序会自动重连，支持STM32复位")
        print("  3. 按 Ctrl+C 退出程序")
        return

    port = sys.argv[1]
    log_file = sys.argv[2] if len(sys.argv) > 2 else None

    print(f"STM32 Simple USB Monitor")
    print(f"目标端口: {port}")
    print(f"开始时间: {datetime.now().strftime('%H:%M:%S')}")
    if log_file:
        print(f"日志文件: {log_file}")
    print("-" * 40)
    print("按 Ctrl+C 退出程序")
    print("-" * 40)

    # 打开日志文件（如果指定）
    log_handle = None
    if log_file:
        try:
            log_handle = open(log_file, 'a', encoding='utf-8')
            print(f"✓ 日志文件已打开: {log_file} (追加模式)")
        except Exception as e:
            print(f"✗ 打开日志文件失败: {e}")
            log_file = None

    reconnect_count = 0
    total_bytes = 0

    try:
        while True:
            ser = None
            try:
                # 尝试连接串口
                print(f"\n[{datetime.now().strftime('%H:%M:%S')}] 正在连接 {port}...")

                ser = serial.Serial(
                    port=port,
                    baudrate=115200,
                    timeout=1,
                    rtscts=False,
                    dsrdtr=False
                )

                # 🔥 关键：设置DTR和RTS为True
                ser.dtr = True
                ser.rts = True
                time.sleep(0.05)

                # 可选：模拟串口调试助手的DTR变化
                ser.dtr = False
                time.sleep(0.05)
                ser.dtr = True
                time.sleep(0.05)

                ser.reset_input_buffer()
                ser.reset_output_buffer()

                reconnect_count += 1
                print(f"[{datetime.now().strftime('%H:%M:%S')}] ✓ 连接成功 (重连次数: {reconnect_count})")

                buffer = ""
                connection_bytes = 0

                # 数据读取循环
                while ser.is_open:
                    try:
                        if ser.in_waiting > 0:
                            data = ser.read(ser.in_waiting)
                            total_bytes += len(data)
                            connection_bytes += len(data)

                            # 稳健解码
                            try:
                                text = data.decode('utf-8', errors='replace')
                            except:
                                text = data.decode('latin-1', errors='replace')

                            buffer += text

                            # 按行输出
                            while '\n' in buffer:
                                line, buffer = buffer.split('\n', 1)
                                line = line.rstrip('\r')
                                if line:
                                    timestamp = datetime.now().strftime('%H:%M:%S')
                                    output = f"[{timestamp}] {line}"
                                    print(output)

                                    if log_handle:
                                        log_handle.write(output + '\n')
                                        log_handle.flush()

                        time.sleep(0.001)

                    except (serial.SerialException, OSError):
                        # 串口异常，跳出内层循环
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] ⚠ 连接断开")
                        if connection_bytes > 0:
                            print(f"[{datetime.now().strftime('%H:%M:%S')}] 📊 本次接收: {connection_bytes} 字节")
                        break

            except serial.SerialException as e:
                print(f"[{datetime.now().strftime('%H:%M:%S')}] ✗ 连接失败: {e}")
                time.sleep(2)  # 等待2秒后重试
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"[{datetime.now().strftime('%H:%M:%S')}] ✗ 意外错误: {e}")
                time.sleep(2)
            finally:
                if ser and ser.is_open:
                    try:
                        ser.close()
                    except:
                        pass

    except KeyboardInterrupt:
        print(f"\n[{datetime.now().strftime('%H:%M:%S')}] 程序被用户中断")

    finally:
        # 清理资源
        if log_handle:
            try:
                log_handle.close()
                print(f"[{datetime.now().strftime('%H:%M:%S')}] ✓ 日志已保存: {log_file}")
            except:
                pass

        print(f"\n[{datetime.now().strftime('%H:%M:%S')}] 运行统计:")
        print(f"  总重连次数: {reconnect_count}")
        print(f"  总接收字节: {total_bytes}")
        print(f"  结束时间: {datetime.now().strftime('%H:%M:%S')}")


if __name__ == '__main__':
    main()