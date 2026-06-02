# Signal Processor

基于 STM32G474VET6 的简易信号源与数字示波器实验项目。固件负责 DAC 波形输出、ADC 采样和 UART 数据传输；上位机使用浏览器 Web Serial API 完成串口连接、参数控制和波形显示。

> 当前版本使用 `PA5 / DAC1_OUT2` 作为信号源输出，使用 `PA1 / ADC1_IN2` 作为示波器输入。

## 功能

- 支持正弦波、方波、三角波、锯齿波输出
- 支持信号源频率和幅度设置
- 支持 ADC 采样波形实时显示
- 支持 Vpp、Vmax、Vmin、Vavg 和频率估算
- Web 上位机通过 Chrome/Edge Web Serial API 连接 USB-TTL 串口
- 串口通信采用二进制帧协议，默认波特率 `2000000`

## 硬件连接

| STM32 引脚 | 功能 | 连接 |
| --- | --- | --- |
| PA5 | DAC1_OUT2，信号源输出 | 回接到 PA1 测试 |
| PA1 | ADC1_IN2，示波器输入 | 接 PA5 或待测信号 |
| PC4 | USART1_TX | 接 USB-TTL RX |
| PC5 | USART1_RX | 接 USB-TTL TX |
| GND | 共地 | 接 USB-TTL GND |

注意：

- USB-TTL 的 `3.3V` 一般不需要接到开发板，只接 `GND/RX/TX`。
- 串口 TX/RX 要交叉连接：`PC4 -> USB-TTL RX`，`PC5 -> USB-TTL TX`。
- 如果测试回环波形，使用 `PA5 -> PA1`，不要再使用旧版的 `PA4 -> PA0`。

## CubeIDE 配置要点

当前工程的 `.ioc` 关键配置如下：

- MCU：`STM32G474VET6`
- 系统时钟：`170 MHz`
- ADC：`ADC1_IN2 / PA1`
- ADC 触发：`TIM3 TRGO`
- ADC DMA：`DMA1_Channel2`，Circular，Half Word
- DAC：`DAC1_OUT2 / PA5`
- DAC Trigger：`None`
- TIM3：用于 ADC 采样触发
- TIM6：用于信号源 DAC 更新节拍
- USART1：`PC4 TX`，`PC5 RX`，`2000000 baud`

如果重新在 CubeIDE 里生成代码，请重点检查：

- `PA1` 是否仍为 `ADC1_IN2`
- `PA5` 是否仍为 `DAC1_OUT2`
- ADC1 是否仍开启 DMA
- ADC1 是否仍由 `TIM3 TRGO` 触发
- USART1 波特率是否仍为 `2000000`

## 项目结构

```text
Signal_processor/
├─ Core/
│  ├─ Inc/
│  │  ├─ signal_generator.h
│  │  ├─ oscilloscope.h
│  │  └─ uart_protocol.h
│  └─ Src/
│     ├─ main.c
│     ├─ signal_generator.c
│     ├─ oscilloscope.c
│     └─ uart_protocol.c
├─ Drivers/
├─ UpperComputer/
│  ├─ index.html
│  ├─ style.css
│  └─ app.js
├─ Simulator/
│  ├─ simulator.py
│  └─ requirements.txt
├─ Signal_processor.ioc
└─ README.md
```

## 固件使用

1. 使用 STM32CubeIDE 打开本工程。
2. 确认 `.ioc` 中引脚和外设配置无红叉。
3. Build 工程。
4. 通过 ST-Link 下载固件到开发板。
5. 按照硬件连接表连接 USB-TTL 与测试回路线。

## Web 上位机使用

可以使用本地 HTTP 服务打开网页：

```powershell
cd UpperComputer
python -m http.server 8000
```

然后在 Chrome 或 Edge 中打开：

```text
http://127.0.0.1:8000/
```

操作步骤：

1. 选择波特率 `2000000`。
2. 点击连接串口。
3. 选择对应的 USB-TTL COM 口。
4. 开启示波器。
5. 开启信号源并设置波形、频率和幅度。

建议测试参数：

- 波形：方波或正弦波
- 频率：`1000 Hz`
- 幅度：`3300 mV`
- 示波器采样率：`20 kSPS` 或 `50 kSPS`
- 时基：`1 ms/div` 到 `100 ms/div`

## 通信协议

串口帧格式：

```text
| Header 2B | CMD 1B | LEN 2B | DATA N bytes | XOR 1B |
| AA 55     | CMD    | LEN    | DATA         | XOR    |
```

校验方式：

```text
XOR = CMD ^ LEN_H ^ LEN_L ^ DATA[0] ^ ... ^ DATA[N-1]
```

主要命令：

| CMD | 方向 | 说明 |
| --- | --- | --- |
| `0x01` | PC -> MCU | 设置波形 |
| `0x02` | PC -> MCU | 设置频率 |
| `0x03` | PC -> MCU | 设置幅度 |
| `0x04` | PC -> MCU | 启停信号源 |
| `0x10` | PC -> MCU | 设置示波器采样率 |
| `0x11` | PC -> MCU | 启停示波器 |
| `0x80` | MCU -> PC | 波形数据 |
| `0x81` | MCU -> PC | 参数 ACK |

## 当前限制

- 本项目是实验性质的简易示波器与信号源，不适合作为高精度测量设备。
- 当前信号源固件使用 TIM6 中断逐点写 DAC，信号源频率上限限制为 `5000 Hz`，优先保证低频波形可观察。
- Web Serial API 需要 Chrome/Edge 等支持 Web Serial 的浏览器。
- USB-TTL 模块需要支持较高波特率，默认使用 `2000000`。

## 许可证

MIT License
