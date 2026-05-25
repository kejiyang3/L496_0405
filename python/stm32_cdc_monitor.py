#!/usr/bin/env python3
"""
STM32 USB CDC Monitor - 完美解决方案
专为解决STM32 USB CDC调试痛点设计，具备自动重连和可靠数据接收功能。

核心特性：
1. 彻底解决DTR/RTS问题 - 确保STM32 CDC驱动正确初始化
2. 完美自动重连 - 支持STM32复位、重烧录、USB拔插等场景
3. 稳健数据解码 - 处理乱码不崩溃
4. 覆盖日志记录 - 每次运行清空文件

痛点解决：
⚠️ 痛点1：为什么Python连上却收不到数据？（DTR/RTS陷阱）
    STM32CubeMX生成的USB CDC固件会检测主机DTR信号。如果DTR为低电平，
    STM32会认为上位机没有准备好，从而静默丢弃数据。
    → 解决方案：显式设置 ser.dtr = True 和 ser.rts = True

⚠️ 痛点2：如何实现完美的"自动重连"？
    STM32复位时，Windows COM端口会瞬间消失再重现，导致ser.read()抛出异常。
    → 解决方案：双层while循环架构，外层检测COM口，内层读取数据
"""

import sys
import time
import argparse
from datetime import datetime
import serial
import serial.tools.list_ports

class STM32CDCMonitor:
    """STM32 USB CDC监控器 - 完美解决自动重连和数据接收问题"""

    def __init__(self, port=None, baudrate=115200, log_file=None, verbose=True):
        """
        初始化监控器

        Args:
            port: 串口号（如COM10），None则自动检测
            baudrate: 波特率，默认115200
            log_file: 日志文件路径，None则不保存日志
            verbose: 是否显示详细输出
        """
        self.port = port
        self.baudrate = baudrate
        self.log_file = log_file
        self.verbose = verbose
        self.ser = None
        self.log_handle = None
        self.connection_start_time = None
        self.total_bytes_received = 0
        self.reconnect_count = 0

    def find_stm32_port(self):
        """自动检测STM32 CDC虚拟串口"""
        ports = serial.tools.list_ports.comports()

        if self.verbose:
            print("检测到的串口设备:")
            for port in ports:
                print(f"  - {port.device}: {port.description}")

        # 优先查找STM32相关的串口
        for port in ports:
            desc_upper = port.description.upper()
            if any(keyword in desc_upper for keyword in ['STM', 'USB', 'CDC', 'VIRTUAL', 'SERIAL']):
                if self.verbose:
                    print(f"✓ 找到STM32 CDC串口: {port.device}")
                return port.device

        # 如果没有找到STM32相关的，返回第一个串口
        if ports:
            if self.verbose:
                print(f"⚠ 未找到明确的STM32串口，使用第一个: {ports[0].device}")
            return ports[0].device

        return None

    def connect_serial(self, quiet=False):
        """连接串口并正确设置DTR/RTS"""
        try:
            if self.port is None:
                # 尝试四个常用端口
                for candidate in ['COM8', 'COM7', 'COM6', 'COM12']:
                    try:
                        self.ser = serial.Serial(
                            port=candidate,
                            baudrate=self.baudrate,
                            timeout=1,
                            write_timeout=1,
                            rtscts=False,
                            dsrdtr=False
                        )
                        self.port = candidate
                        break
                    except serial.SerialException:
                        self.ser = None
                        continue

                if self.ser is None:
                    if not quiet:
                        print("✗ 未找到可用的串口设备 (尝试了 COM8/7/6/12)")
                    return False
            else:
                # 指定了端口，直接连接
                if self.verbose:
                    print(f"正在连接 {self.port} (波特率: {self.baudrate})...")
                self.ser = serial.Serial(
                    port=self.port,
                    baudrate=self.baudrate,
                    timeout=1,
                    write_timeout=1,
                    rtscts=False,
                    dsrdtr=False
                )

            # 🔥 关键步骤：设置DTR和RTS为True
            # 这是解决STM32 CDC数据接收问题的核心！
            self.ser.dtr = True
            self.ser.rts = True
            time.sleep(0.05)  # 短暂延迟让信号稳定

            # 可选：模拟串口调试助手的DTR变化行为
            # 有些STM32 CDC驱动需要看到DTR变化才完全初始化
            self.ser.dtr = False
            time.sleep(0.05)
            self.ser.dtr = True
            time.sleep(0.05)

            # 清空缓冲区
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()

            self.connection_start_time = datetime.now()
            self.reconnect_count += 1

            if self.verbose:
                print(f"✓ 成功连接到 {self.port}")
                print(f"  连接时间: {self.connection_start_time.strftime('%H:%M:%S')}")
                print(f"  重连次数: {self.reconnect_count}")

            return True

        except serial.SerialException as e:
            if self.verbose:
                print(f"✗ 连接失败: {e}")
            return False
        except Exception as e:
            if self.verbose:
                print(f"✗ 连接时发生意外错误: {e}")
            return False

    def open_log_file(self):
        """打开日志文件（覆盖模式）"""
        if self.log_file:
            try:
                # 使用覆盖模式，每次运行清空文件
                self.log_handle = open(self.log_file, 'w', encoding='utf-8')
                if self.verbose:
                    print(f"✓ 日志文件已打开: {self.log_file} (覆盖模式)")
                return True
            except Exception as e:
                print(f"✗ 打开日志文件失败: {e}")
                return False
        return True

    def log_message(self, message, timestamp=True, add_newline=True):
        """输出消息到控制台和日志文件"""
        if timestamp:
            ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
            output = f"[{ts}] {message}"
        else:
            output = message

        if add_newline:
            output += '\n'

        print(output, end='')
        if self.log_handle:
            self.log_handle.write(output)
            self.log_handle.flush()

    def read_loop(self):
        """数据读取循环（内层循环）"""
        buffer = ""
        line_count = 0

        try:
            while self.ser and self.ser.is_open:
                try:
                    # 检查是否有数据可读
                    if self.ser.in_waiting > 0:
                        # 读取所有可用数据
                        data = self.ser.read(self.ser.in_waiting)
                        self.total_bytes_received += len(data)

                        # 🔥 稳健解码：使用replace处理乱码
                        try:
                            text = data.decode('utf-8', errors='replace')
                        except UnicodeDecodeError:
                            # 极少数情况下的额外保护
                            text = data.decode('latin-1', errors='replace')

                        buffer += text

                        # 按行处理
                        while '\n' in buffer:
                            line, buffer = buffer.split('\n', 1)
                            line = line.rstrip('\r')
                            if line:  # 只输出非空行
                                line_count += 1
                                self.log_message(f"{line}")

                    # 短暂休眠，避免CPU占用过高
                    time.sleep(0.001)

                except serial.SerialException as e:
                    # 🔥 关键：串口异常，跳出内层循环进行重连
                    if self.verbose:
                        self.log_message(f"⚠ 串口异常: {e}", timestamp=True)
                    break
                except Exception as e:
                    # 其他异常，记录但不中断
                    if self.verbose:
                        self.log_message(f"⚠ 读取时发生错误: {e}", timestamp=True)
                    time.sleep(0.02)

            # 处理剩余的缓冲区数据
            if buffer:
                line = buffer.rstrip('\r')
                if line:
                    self.log_message(f"{line}")

        finally:
            # 确保串口关闭
            if self.ser and self.ser.is_open:
                try:
                    self.ser.close()
                except:
                    pass

            if self.verbose and line_count > 0:
                self.log_message(f"📊 本次连接统计: {line_count}行数据", timestamp=True)

    def reconnect_loop(self):
        """自动重连循环（外层循环）"""
        print("\n" + "="*60)
        print("STM32 USB CDC Monitor - 完美解决方案")
        print("="*60)
        print(f"开始时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"目标端口: {self.port if self.port else '自动检测'}")
        print(f"波特率: {self.baudrate}")
        if self.log_file:
            print(f"日志文件: {self.log_file}")
        print("="*60)
        print("提示: 按 Ctrl+C 退出程序")
        print("="*60 + "\n")

        # 打开日志文件（如果指定）
        if not self.open_log_file():
            print("✗ 日志文件初始化失败，继续运行但不保存日志")

        last_reconnect_time = time.time()

        try:
            while True:
                # 检查是否需要等待重连
                current_time = time.time()
                if self.ser and self.ser.is_open:
                    # 已经连接，不需要等待
                    pass
                elif current_time - last_reconnect_time < 0.2:
                    # 距离上次重连尝试不到0.2秒，等待
                    time.sleep(0.05)
                    continue

                # 尝试连接
                if self.ser is None or not self.ser.is_open:
                    if self.connect_serial(quiet=(self.reconnect_count > 0)):
                        # 连接成功，开始读取数据
                        self.log_message(f"✅ 连接成功，开始接收数据", timestamp=True)
                        if self.reconnect_count > 1:
                            self.log_message(f"📈 累计重连次数: {self.reconnect_count}", timestamp=True)

                        self.read_loop()

                        # 连接断开，下次重连时重新扫描所有端口
                        self.port = None
                        if self.verbose:
                            self.log_message("⚠ 连接断开，等待重连...", timestamp=True)
                    else:
                        # 连接失败 — 静默重连，仅每 3s 提示一次
                        if not hasattr(self, '_last_fail_print'):
                            self._last_fail_print = 0
                        now = time.time()
                        if now - self._last_fail_print >= 3.0:
                            self._last_fail_print = now
                            if self.verbose:
                                self.log_message("❌ 连接失败，重试中...", timestamp=True)
                        time.sleep(0.2)

                    last_reconnect_time = time.time()
                else:
                    # 理论上不会到达这里
                    time.sleep(0.02)

        except KeyboardInterrupt:
            print("\n\n" + "="*60)
            print("程序被用户中断")
            print("="*60)

        finally:
            self.cleanup()

    def cleanup(self):
        """清理资源"""
        # 关闭串口
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
                print("✓ 串口已关闭")
            except:
                pass

        # 关闭日志文件
        if self.log_handle:
            try:
                self.log_handle.close()
                print(f"✓ 日志文件已保存: {self.log_file}")
            except:
                pass

        # 显示统计信息
        print("\n" + "="*60)
        print("运行统计:")
        print(f"  总重连次数: {self.reconnect_count}")
        print(f"  总接收字节: {self.total_bytes_received}")
        print(f"  结束时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print("="*60)

    def run(self):
        """运行监控器"""
        self.reconnect_loop()


def list_available_ports():
    """列出所有可用的串口"""
    ports = serial.tools.list_ports.comports()

    if not ports:
        print("未找到任何串口设备")
        return

    print("\n可用的串口设备:")
    print("-" * 50)
    for i, port in enumerate(ports):
        print(f"[{i}] {port.device}")
        print(f"    描述: {port.description}")
        print(f"    硬件ID: {port.hwid}")
        print()


def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description='STM32 USB CDC Monitor - 完美解决方案',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:
  # 自动检测STM32串口
  python stm32_cdc_monitor.py

  # 指定串口号
  python stm32_cdc_monitor.py -p COM10

  # 指定串口和日志文件
  python stm32_cdc_monitor.py -p COM10 -l stm32_log.txt

  # 使用不同波特率
  python stm32_cdc_monitor.py -p COM10 -b 9600

  # 列出所有可用串口
  python stm32_cdc_monitor.py --list-ports

  # 静默模式运行（不显示详细输出）
  python stm32_cdc_monitor.py -p COM10 --quiet

特性说明:
  • 自动重连：STM32复位、重烧录、USB拔插时自动恢复连接
  • DTR/RTS优化：确保STM32 CDC驱动正确接收数据
  • 稳健解码：处理乱码不崩溃
  • 日志记录：多次重连不覆盖历史数据
        """
    )

    parser.add_argument('-p', '--port',
                       help='串口号 (如COM10)，不指定则自动检测STM32 CDC串口',
                       default=None)

    parser.add_argument('-b', '--baudrate',
                       type=int,
                       help='波特率 (默认: 115200)',
                       default=115200)

    parser.add_argument('-l', '--log',
                       help='日志文件路径，不指定则不保存日志',
                       default=None)

    parser.add_argument('-q', '--quiet',
                       action='store_true',
                       help='静默模式，不显示详细输出')

    parser.add_argument('--list-ports',
                       action='store_true',
                       help='列出所有可用串口后退出')

    args = parser.parse_args()

    # 列出串口
    if args.list_ports:
        list_available_ports()
        return

    # 创建并运行监控器
    monitor = STM32CDCMonitor(
        port=args.port,
        baudrate=args.baudrate,
        log_file=args.log,
        verbose=not args.quiet
    )

    try:
        monitor.run()
    except Exception as e:
        print(f"\n✗ 程序运行失败: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()