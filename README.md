![GitHub](https://img.shields.io/github/license/notrynohigh/BabyOS)![GitHub language count](https://img.shields.io/github/languages/count/notrynohigh/BabyOS)![GitHub code size in bytes](https://img.shields.io/github/languages/code-size/notrynohigh/BabyOS)

![](https://gitee.com/notrynohigh/BabyOS/raw/dev/doc/2.png)

# BabyOS

BabyOS适用于MCU项目，她是一套管理功能模块和外设驱动的框架。

对项目而言，缩短开发周期。项目开发时选择适用的功能模块及驱动。直接进入功能代码编写的阶段。

对开发而言，减少重复工作。调试过的功能模块和驱动代码放入BabyOS中管理，以后项目可以直接使用。

# 1 代码结构

<img src="https://gitee.com/notrynohigh/BabyOS/raw/dev/doc/BabyOS.png" alt="BabyOS" style="zoom: 80%;" />

# 2 使用要求

1. 编译器需要勾选C99
2. 默认情况，编译器需要支持自定义段（必须）以及弱函数（非必须）

Windows下需要安装python（2或3均可）用于配置代码

Linux下需要安装python、python-pip以及make 用于配置和编译代码

![b_config](https://foruda.gitee.com/images/1674379348907249447/3b414522_1789704.gif)

# 3 BabyOS文档

查看设计文档：[https://babyos.cn/doc/](https://babyos.cn/doc/)

推荐：将babyos作为子模块加入到用户工程进行使用。

<img src="https://foruda.gitee.com/images/1706455699810637537/031b0a0f_1789704.gif" style="zoom: 67%;" />

**添加BabyOS代码**

| 路径           | 部分/全部                   |
| -------------- | --------------------------- |
| bos/algorithm  | 全部添加                    |
| bos/core       | 全部添加                    |
| bos/drivers    | **根据需要添加**            |
| bos/hal        | 全部添加                    |
| bos/mcu        | **根据需要添加**            |
| bos/modules    | 全部添加                    |
| bos/thirdparty | **根据需要添加**            |
| bos/utils      | 全部添加                    |
| bos/_config    | *_config目录拷贝至用户目录* |

**添加Include路径**

`bos/`

 `_config/`   如果配置文件拷贝到其他路径了，则添加相应路径即可。

# 4 BabyOS教程

由于代码在不断更新，所以无法避免视频内容与最新代码匹配，如有疑问，请在issues描述问题！

[第一讲介绍BabyOS](https://www.bilibili.com/video/BV1Ff4y1o7bZ?share_source=copy_web)

[第二讲BabyOS的快速体验和进阶体验](https://www.bilibili.com/video/BV1Lg411f7cH?share_source=copy_web)

[第三讲BabyOS的功能概述](https://www.bilibili.com/video/BV1iU4y1q7EJ?share_source=copy_web)

[第四讲BabyOS的功能组件](https://www.bilibili.com/video/BV1JZ4y1Y7S7?share_source=copy_web)

# 5 BabyOS例程

例程仓库中不同分支对应着不同实验：

[https://gitee.com/notrynohigh/BabyOS_Example](https://gitee.com/notrynohigh/BabyOS_Example)

BabyOS私有协议上位机Demo：

[https://gitee.com/notrynohigh/BabyOS_Protocol](https://gitee.com/notrynohigh/BabyOS_Protocol)

# 6 Baby如何成长

BabyOS如果能在项目中发挥大的作用就需要有足够的功能模块以及驱动代码。希望借助广大网友的力量，一起“喂养”她，是她成为MCU开发中不可缺少的一部分。

**码云**（主仓库，开发者提交代码于dev分支，由管理员合并至master分支）：

[https://gitee.com/notrynohigh/BabyOS](https://gitee.com/notrynohigh/BabyOS)

**github**（自动同步）：

[https://github.com/notrynohigh/BabyOS](https://github.com/notrynohigh/BabyOS)

---

**如果您觉得这套开源代码有意义，请给个Star表示支持，谢谢！**

管理员邮箱：notrynohigh@outlook.com

开发小组QQ群&开发动态发布的公众号：

![qq](https://gitee.com/notrynohigh/BabyOS/raw/master/doc/qq.png) ![qrcode](https://gitee.com/notrynohigh/BabyOS/raw/master/doc/qrcode.jpg)

# 更新记录

| 日期    | 新增项                                                       | 备注 |
| ------- | ------------------------------------------------------------ | ---- |
| 2019.12 | 功能模块：FIFO, AT, Nr_micro_shell, Lunar calendar           |      |
| 2020.01 | 功能模块：KV存储                                             |      |
| 2020.02 | 功能模块：Xmodem128, Ymodem, FlexibleButton 驱动：xpt2046    |      |
| 2020.03 | 功能模块：b_log, b_gui, b_menu, b_trace，b_heap 驱动：ssd1289 ili9341 |      |
| 2020.04 | 增加基于华大MCU例子，增加驱动ili9320 oled12864，优化b_mod_kv等功能模块，增加algorithm目录 |      |
| 2020.05 | 优化代码结构，增加：b_mod_timer b_mod_qpn等 ,优化b_mod_xmodem等，增加算法base64 sha1 sort等 |      |
| 2020.06 | 分解为3个仓库管理代码。增加SFUD作为SPIFLASH驱动，增加SD卡驱动，增加FatFS和LittleFS等 |      |
| 2020.07 | 内容未更新，增加两个视频教程用于快速了解BabyOS               |      |
| 2020.08 | BabyOS_Hal仓库增加ST标准库，增加标准库例子。对代码中细节进行优化 |      |
| 2021.01 | 去掉多余的文件，修改section部分的内容。                      |      |
| 2021.02 | 增加vscode+arm-gcc+openocd 编译调试的例子。doc目录增加sections链接文件以及示例makefile等 |      |
| 2021.05 | 支持编译器AC6                                                |      |
| 2021.06 | 大版本更新，对整体代码重新梳理一遍。可使用MCU内部FLASH使用KV存储、完善驱动部分等... |      |
| 2021.07 | 修改gcc环境下链接文件的修改方法                              |      |
| 2021.10 | 修改将分离的两个仓库内容重新加入进来，改变HAL代码的方式以及各个驱动的接口结构。V7.0.0 |      |
| 2021.11 | 针对警告和报错点进行优化, mcu目录下的代码默认用寄存器操作方式 |      |
| 2021.12 | 优化整体结构，增加对应配置工具，方便使用。支持V7.3.0及更新版本的 b_config.h |      |
| 2022.01 | 增加bReinit和bModifyHalIf接口，并修复模拟I2C中BUG            |      |
| 2022.02 | 调整结构，使得依赖关系更加合理。                             |      |
| 2022.03 | 增加cjson第三方代码，并配上内存池。修改b_config.h中宏的命名以及文件夹、文件命名。 |      |
| 2022.04 | 增加WIFI模块驱动，增加获取驱动私有信息的接口，增加QSPI       |      |
| 2022.05 | 重写配置工具、增加IAP功能模块                                |      |
| 2022.06 | 更新设计和使用文档，详细介绍软件模块。增加对矩阵按键的支持   |      |
| 2022.07 | 增加视频教程，在README中展现。修改b_mod_gui，支持多个屏使用ugui |      |
| 2022.08 | 修复按键相关的bug，增加MCU，增加COREMARK, 完善IAP模块        |      |
| 2022.10 | 增加b_mod_state功能模块，`<br>`增加内存中模拟4M SPIFLASH驱动（仅用于调试软件逻辑）`<br>`增加在linux环境下运行babyos的必要代码，用于脱离硬件调试纯软件逻辑功能 |      |
| 2022.11 | 优化驱动框架                                                 |      |
| 2022.12 | 修复V8.0.0的bug ， 将例程更新匹配V8版本                      |      |
| 2023.01 | 将算法模块使用起来，将常用计算类代码放入其中 。改变BabyOS的配置方式，使用Kconfig |      |
| 2023.02 | 完成KV 0.3版本所有预期功能                                   |      |
| 2023.03 | 重写WIFI模块驱动、动态内存。去掉AT模块，增加队列，补充FIFO接口。 |      |
| 2023.04 | 增加b_mod_wifi去对接各个WIFI模块                             |      |
| 2023.05 | 将utc时间从基准时间2000年改为1970年<br>utc增加设置和读取utc时间接口。tick增加溢出计数，提供uint64_t类型的tick值获取接口 |      |
| 2023.06 | bMalloc和bFree可选择是否打印申请者和释放者信息<br>修复队列bug ; b_hal_it 增加中断注册和调用接口 ; 增加io多路转发select功能 |      |
| 2023.07 | core 目录，增加b_task b_timer b_queue b_sem<br>b_util_uart废弃，其功能融入 b_hal_uart 往bos喂串口数据的唯一途径是bHalItInvoke |      |
| 2023.08 | 修复语法问题，增加service层                                  |      |
| 2023.09 | 增加第三方代码arm-2d，优化spi接口和屏驱动                    |      |
| 2023.10 | 修复若干bug。增加mcu.py方便新增mcu，优化b_config.py当bos路径无效时提示重新输入 |      |
| 2023.11 | 修复模拟iic接口；ms延时不依赖tick。添加驱动：icm20948+qmc5883l+qmi8658a<br>增加第三方库qrcode，新建对应的软件模块 |      |
| 2023.12 | 第三方库增加lwip，modules增加 b_mod_netif 可免去用户移植lwip的工作 |      |
| 2024.01 | 增加 b_srv_tcpip 实现获取ntp时间 <br>完善 b_mod_netif接口，实现tcp/udp客户端服务端代码以及ping接口 |      |
| 2024.02 | 重构网络接口部分代码，统一网络操作接口 | |
| 2024.03 | 重写WIFI模组代码适配网络接口,支持瑞萨编译器 | |
| 2024.04 | 增加手势传感器和tinyusb第三方库 | |
| 2024.05 | 完善babyos协议，使用最新的代码更新OTA例子;增加LVGL第三方代码 | |
| 2024.06 | 增加b_mod_ssl 可以请求https接口，HAL层增加看门狗 | |
| 2024.07 | 重写b_mod_fs， 统一文件系统的操作接口 | |
| 2024.08 | 修复modbus的bug，hal层增加dma接口 | |
| 2024.09 | 增加文件传输服务 | |
| 2024.11 | 移植lua, 更新cm_backtrace版本  | |
| 2024.12-2025.03 | 基于BabyOS DIY 桌面电子宠物  | |