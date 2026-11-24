# 大众车机天宝187A Hack笔记 (mengxp, FreeBuf 2017-03-16)

Source: https://web.archive.org/web/20180215224803/http://www.freebuf.com/geek/129160.html

*本文原创作者：mengxp，属Freebuf原创奖励计划，未经许可禁止转载

0×00前言

自从去年买了车，对汽车电子系统的兴趣就上来了。这不，前一阵子逛汽车论坛，发现了有网友将老版本的天宝车机被刷上了2017新帕萨特车机的系统，支持超级蓝牙和苹果CarPlay，百度CarLife等功能。

没错，这台车机就是最近很火的天宝187A，据说被刷上去的新系统是属于天宝187B，而天宝187B是为在当时并未上市的2017款新帕萨特配备的。

我瞧了一眼我车上的车机型号，和天宝187A零件型号一样，都是6RD035187A，但是我这台却是德赛的。一样的型号，却有两个制造商，这很有意思，就好比有一汽大众和上汽大众一样…

在网上查了一些相关资料，得知德赛187A运行WINCE系统，没有CarPlay功能和百度Life。天宝187A运行Linux系统，也没有CarPlay功能和百度Life。但是天宝187A可以刷187B的系统，天宝187A和187B的区别仅仅是DRAM内存大小和面板按键不一样。187A是256MB 内存，而187B是512MB内存。

这就产生了一个问题，有些网友刷了系统后启动时死机，但是换了512MB内存后就正常了。由于本人对硬件也有研究，因此决定买一台天宝187A来研究一下，至于德赛……先好好地在车上服役，暂时不去虐他了…

关于天宝这个公司，也许是我才接触汽车不久，之前没有听说过。德赛倒是听说过，专门做汽车导航机器的，也给大众代工车机。江苏天宝汽车电子有限公司([http://www.toppower.com](http://www.toppower.com/))，公司很低调，网站很简陋…看一下职位招聘，都招什么工程师，嗯，CAE工程师、项目工程师、电子工程师、制造工程师，就是没有软件系统工程师，那这机器软件哪来的？大众给的？外包的？此处留下疑问……

0×01硬件

接下来我准备详细分析天宝187A的硬件系统。我从咸鱼平台上买了一台没有刷过系统的天宝187A。拆机后看到了主板。

主板正面有很多芯片，首先看电源部分。电源由汽车蓄电池直接供电，汽车启动后就是发电机供电，因此输入电压应是12-14V大概这个范围。

首先电源经过了一个STPS41H100CGY，这是一个肖特基二极管，整流作用，不关心。然后有一些大电容，大电感，背面有一个SOT23-5封装的IC，难道是DCDC？由于这部分电路靠近功放IC，不想继续追究他，无非就是一些给功放IC供电的滤波电路或者开关电路。毕竟车机接的是蓄电池，功放IC是不可能一直工作的。

接下来会看到很多电源IC，小电感，还有很多1206封装的电容，这肯定是给CPU或其他芯片供电的部分。首先负责12V转5V供电的有两个芯片。TI德州仪器SSOP-10封装的TPS54260-Q1，IC丝印是5426Q。MAXIM美信QFN-28封装的MAX16984，IC丝印是16984RA。

TPS54260是一颗宽电压输入(3.5V-60V)，2.5A电流的DCDC降压IC，在这里设计12V输入，5V输出，由于电感体积很大，大过其他几个电感，应该是主要供电。美信的MAX16984是设计给USB供电的，电流是2.5A，很高啊，看来是可以满足iPad2.1A充电的，设计的比较有良心……

往上看还有3颗电源IC，分别是两颗德州仪器QFN-16封装的TPS54388Q1，丝印5438Q，一颗安森美DFN-10封装的NCV896530，丝印是完整型号。

TPS54388-Q1，2.95V-6V输入，3A输出。NCV896530，2.7V-5.5V输入，两路1A输出。其中一颗TPS54388-Q1负责5V转1.5V给DDR3内存供电，另一颗TPS54388-Q1负责5V转1.375V给CPU核心供电。NCV896530负责5V转1.8V和3.3V，应该是IO供电和外设供电。

靠近大插座附近有一个CAN PHY芯片，附近还有一颗电源芯片，丝印是BLW N EO，查不到具体型号，但是他附近没有电感，应该是一颗LDO降压芯片，测量结果是12V转3.3V，是给旁边这颗MCU供电的，这样大的压差使用LDO供电非常不明智…唯一的解释就是这个MCU功耗非常低，使用LDO设计上合理，成本上也合理…

分析了这么多硬件电源部分，感觉好无聊。其实在我看来分析一个系统是否成功，首先就要看他的供电设计，这部分是核心，供电设计的好不好，关乎整个系统工作是否稳定。另外，我没有看到高级电源管理IC，此处也是一个疑问，不过后面的分析可以解答这个问题。

接下来登场的是系统主要芯片。CPU是飞思卡尔（已被NXP恩智浦收购）公司的i.MX6DL，型号是MCIMX6S6AVM08AC。双核Cortex-A9，最高1G工作频率，内置2D/3D图形处理单元，具有1080p图像处理能力，支持丰富外设。看起来很强大的样子…

DRAM是南亚的NT5CB128M16FP-D1H，16位，256MB，DDR3，1.5V。

闪存使用的是Spansion的ML02G100BHA00，256Mx8 NANDFLASH，3.3V。

还有一颗QFP-100封装的芯片R7F7010253，直接搜索这个型号其实只能得知他是一颗瑞萨设计的芯片，其实他有个代号是RH850，搜这个代号能找到更多的资料，还有开发工具，但是就是找不到datasheet。

瑞萨这家公司主要设计生产MCU微控制器，汽车领域是其主要业务。

经过后面结合软件部分的一番分析得知，这个MCU的作用其实是CAN通信、硬件身份信息（零件号，序列号）、电源管理。没错，电源管理，因为汽车通信网是CAN总线，因此这款机器的电源其实也是受CAN总线管理的。

由于缺乏这颗MCU更详细的datasheet资料，不能找到具体哪些引脚对应哪些功能，不过后来我还是找到了控制每个电源IC使能引脚(EN)，以及CPU复位引脚(POR_B)所对应的引脚，这些后面结合软件部分再说。

主板背面还有2个芯片，一个是ANALOG亚德诺的ADV7182，负责倒车影像摄像头信号转换（AV转数字信号）。还有一个丝印是2313，查不到具体型号，看旁边的测试点，和I2C总线相关，作用不明…

硬件就先讲到这，大家是不是以为我是一个硬件工程师？其实我只是个业余的…硬件不是我的本职工作，不过我给公司客串过硬件设计，做过一个小产品，从硬件电路设计，到PCB Layout，从MCU固件，到产测软件，都被我包了，公司小，又是做软件的，请个硬件工程师没必要，就让我上了…还好这个项目比较小，不然我真要累死…

了解了天宝187A的硬件组成，就方便了接下来开展Hack行动！

0×02调试串口

Hack一台硬件和软件公开资料并不多的主板，最好的入口点就是寻找他的调试接口。很多产品的主板在设计时都会将TTL串口，JTAG这种关键的接口在主板上以插针或测试点(TestPad)的方式引出来，这种方式很普遍，比如说常用的路由器，大部分路由器都引出了TTL串口，少部分还引出了JTAG调试接口；3.5寸/2.5寸硬盘主板背面是不是有2排测试点？没错，这也是串口和JTAG。

其实不局限于常见的这些产品，绝大多数产品，都会引出TTL或JTAG，这主要是为了方便生产和测试，一颗CPU或MCU芯片、或者FLASH芯片，在工厂SMT贴在主板上后，里面是没有程序的，程序怎么烧进去？这就需要TTL或JTAG接口来实现了，有些带USB接口的还可以从USB引导烧入。

串口和JTAG是生产测试所必须的接口，我想应该很少有人会把烧程序这个阶段放在SMT工艺前面，再说还有测试环节怎么办，盲测？不可能嘛……比如下面这张是从网上找到的硬盘主板和很常见的路由主板，上面都有TTL接口或JTAG接口。

有些人没做过硬件开发，我在这里简单解释下什么是TTL和JTAG。

TTL串口他就是一个串口，TTL电平，基本都是3线，TX，RX，GND，相比RS232来说，控制引脚、状态引脚基本都是不使用的。这其实简化了串口的使用难度，方便了开发，一般开发人员仅使用一个USB转串口的小板就能完成与目标板的通讯。

串口主要用途就是打印日志，方便开发人员确定程序状态，或对目标板进行控制。例如可以查看引导程序日志，打断引导，执行烧写固件指令，上传临时程序到RAM等等。另外使用串口还可以登录Linux系统的终端shell进行操作等等。

JTAG接口是CPU调试接口，通常由TDITDO TCK TMS这4个主要信号组成，一些系统还有TRST信号，用于复位JTAG状态机，还有nSRST用于复位CPU等等。使用这个接口需要与CPU搭配使用的硬件调试工具，每个CPU厂商所使用的工具是有一些区别的。

JTAG接口用途广泛，他可以在CPU上电后就打断CPU的运行，并直接操作CPU寄存器。如果开发Bootloader，或者相关底层的工作，也需要用到JTAG。

在生产测试环节，JTAG还可以测试CPU是否正常工作，读写DRAM，判断DRAM是否正常工作，还可以将程序烧写入Flash。

由于JTAG调试接口过于强大，大部分情况下为了避免自己的产品被山寨，制造商都会在产测结束后禁用掉这个JTAG接口，方法是烧掉CPU里面的熔丝，这个接口就不起作用了。

接下来开始在主板上找TTL和JTAG，JTAG很好找，就在主板背面，有几个较大的测试点，标明了TDI TDO TCK TMS等字样，这些应该就是CPU的JTAG接口了。

但是JTAG熔丝很可能被烧掉了，JTAG接口估计是废的。主要还是要找到调试串口。虽然有找到一些标注了TXD RXD丝印的测试点，但是很遗憾，经过连接测试后，这些并不是CPU的TTL调试串口，系统上电后根本没有任何字符串输出。

难道这个主板在设计时没有引出TTL串口吗？这怎么可能，太愚蠢了吧…

由于这个主板测试点非常多，密密麻麻很多小测试点，根本没有丝印标注，难道TTL串口就隐藏在这些没有丝印标注的小测试点吗？在这么多测试点里找到串口TX和RX信号，这看起来像是不可能完成的任务！

主板正面有两个没有焊接排线插座的焊盘，这两个排线插座都是18pin的，其中一个走线靠近瑞萨MCU，有可能是瑞萨MCU的产测接口。另一个排线插座顶层走线既不靠近CPU也不靠近MCU，但这并不意味着他没有在内层电路与CPU相连，这很有可能主CPU的产测接口。

但是很遗憾当时我并没有意识到这一点，我已经被密密麻麻的小测试点逼得抓狂了。接下来我甚至做了一系列现在想想觉得很愚蠢的事情，在此分享出来吧。

我在咸鱼上联系一位网友花废品价买了他一个报废的主板，为的就是拆CPU，寻找TXD，RXD的走线。我把主板上了热风枪拆焊台，把CPU吹了下来。

寻找NXP的SDK开发资料，找到了一个参考板的电路图，得知调试串口极有可能是UART4，TXD是Pin_W5，RXD是Pin_V6。我根据Datasheet上的引脚图去找W5和V6，然后通过BGA过孔找到背面的过孔焊盘，把焊盘的阻焊漆刮了，飞线，连接USB串口，上电发现并没有任何信息。

难道厂商的UBOOT没有启用串口？或者配置了额外的引脚作为调试串口？因为找不到正确的调试串口Pin，我的研究一度陷入了停滞。

接下来，我从网上找来网友分享的187A升级包，里面有Uboot镜像文件，我上了IDA，对比SDK中的Uboot源代码，找到了配置调试串口的数据。

```
ROM:178013AC dword_178013AC DCD 0x62C244 ; DATA XREF: sub_17801278+C8
ROM:178013B0 dword_178013B0 DCD 0x10000100 ; DATA XREF: sub_17801278+D4
ROM:178013BC dword_178013BC DCD 0x14640258 ; DATA XREF: sub_17801278+ECr
ROM:178013C0 dword_178013C0 DCD 0x3716348 ; DATA XREF: sub_17801278+F4r

```

通过分析uboot头文件mx6dl_pins.h中的宏定义，得出这就是头文件中定义的

```
MX6_PAD_DECL(KEY_COL0__KEY_COL0, 0x062C, 0x0244, 3, 0x0000, 0, 0)
MX6_PAD_DECL(KEY_ROW0__UART4_RX_DATA, 0x0640, 0x0258, 4, 0x0914, 3, 0)

```

对比datasheet，确定了串口4使用的确实是W5和V6。这就奇怪了，难道调试串口不是串口4？后来我又定位到了puts函数，然后进入putchar函数，发现写入的确实是串口4的寄存器……

现在已经可以从代码上确定，串口4就是调试串口，使用的引脚也确实是CPU的W5和V6，那为什么我在串口上看不到任何信息？

此刻我已经被密密麻麻的测试点、BGA过孔、没有任何符号的UbootIDA反汇编，折磨的想要放弃了。

过了几天，我觉得不服，钱不能白花，决定再战。我检查了Datasheet是否是这个型号CPU的Datasheet，仔细研究了BGA焊盘，过孔，走线，引脚图……等下，我看到datasheet引脚图上面写的是Bottom View！HOLYCRAP！我把这个图当作Top View来看了！

怪不得，我之前找的引脚是错！的！我把图垂直翻转，然后右转，右转，对齐了PinA1，找到W5V6的过孔，刮背面过孔焊盘，飞线，上电……串口终于找到了！此刻成功激动的心情简直无以言表……

后来我意识到那个18pin的排线插座极有可能引出了UART4，用表测了下，发现果然如此，那我之前的一系列行为岂不是很愚蠢……不过我又研究了这个18pin，发现他还引出了JTAG，USB Host。

接下来的研究，其实并不是按我文章的章节顺序展开的，我的研究流程比较混乱，因为涉及的内容繁杂，相互纠缠。因此本文接下来的内容并不是严格按时间顺序展开的……

0×03 UBoot Hack

他的这个UBoot并不是SDK里面的Uboot，毕竟硬件设计上与参考板设计不同，Uboot代码有所改动是理所应当的。但这个Uboot不能被打断引导，令我很头疼。万一我折腾折腾着，把Kernel搞死了，这块主板就变成了一块砖。由于我没有NAND编程器，没办法对Flash烧写恢复，一旦成砖我还怎么研究下去？

我需要准备一个PlanB……

首先我要想办法打断Uboot的引导，进入uboot提示符，然后使用uboot指令，用命令传输数据，就有可能对砖机进行修复。

我首先查看了他的Flash布局：cat/proc/mtd

```
I have no name!@(none) /$ cat /proc/mtd
dev: size erasesize name
mtd0: 00400000 00020000 "bootloader"
mtd1: 00c00000 00020000 "nand.kernel"
mtd2: 0d000000 00020000 "nand.rootfs"
mtd3: 00a00000 00020000 "pss1"
mtd4: 00a00000 00020000 "pss2"
mtd5: 00c00000 00020000 "logo"

```

可以发现并没有uboot环境变量分区，那么环境变量可能是固定写死在uboot固件里面的，WinHex确实找到了这部分区域。

有一个变量叫bootdelay，值为0。

怪不得不能被打断引导，我把这个值改为3，想着把他刷进去试一下看看。接下来怎么把uboot刷进去又成了一个问题。

最简单暴力的办法是用dd命令直接从/dev/mtd0或者/dev/mtdblock0写进去，那万一写成砖怎么办？这个方法太危险，NAND Flash的操作与SPIFlash可能不同，因为NAND是有OOB区域的，里面有ECC校验，我不能冒险。

接下来我分析了这个系统的更新程序，/opt/sysupdate。这个程序虽然去掉了一些符号表，私有函数在IDA里面看来都是sub_XXXX，但是由于他使用了一套日志接口，log_optional，里面打印了源文件名，函数名，行号等信息，一些函数可能被编译器整合优化成一个函数，但还是可以猜出绝大多数函数的功能。

经过一番分析，确定了他更新uboot使用了外部命令kobs-nginit -v ./uboot.bin。这个命令可能来源于NXP i.MX6 SDK，不管了，先把uboot复制到优盘，插入车机，输命令更新uboot。完成重启，发现bootdelay没生效，并没有出现等待用户3秒输入打断引导的环节……

我X，厂商竟然把bootdelay这部分代码注释掉了吗？这不科学啊！

不过我又想到了一个办法，把bootcmd这个变量擦掉，这样uboot没有了启动命令，自然就无法引导了吧。于是上WinHex，找到bootcmd=，修改成aootcmd=。刷机，重启，发现确实中断了引导。

但是这种办法带来的后果就是，每次启动，都需要手动输入命令才能引导系统，不是什么大问题，准备记事本每次复制粘贴启动命令就可以了…

```
nand read0x10007FC0 0x400000 0x300000; bootm0x10007FC0

```

现在可以在uboot提示符下操作了，help查看帮助，看看支持哪些命令，没有网络相关的，因为这主板上没有导出以太网IC……那用什么命令恢复变砖的主板呢。

```
MX6SOLO STDPLUS U-Boot> help
? - alias for 'help'
autoscr - DEPRECATED - use "source" command instead
base - print or set address offset
bdinfo - print Board Info structure
bld_his -

boot - boot default, i.e., run 'bootcmd'
bootd - boot default, i.e., run 'bootcmd'
bootm - boot application image from memory
clk - Clock sub system
cmp - memory compare
coninfo - print console devices and information
cp - memory copy
crc32 - checksum calculation
destroyenv- destroy enviroment variables stored in medium
echo - echo args to console
erase - erase FLASH memory
flinfo - print FLASH memory information
go - start application at address 'addr'
help - print online help
i2c - I2C sub-system
iminfo - print header information for application image
imxotp - One-Time Programable sub-system
imxtract- extract a part of a multi-image
itest - return true/false on integer compare
loadb - load binary file over serial line (kermit mode)
loads - load S-Record file over serial line
loady - load binary file over serial line (ymodem mode)
loop - infinite loop on address range
md - memory display
mm - memory modify (auto-incrementing address)
mtest - simple RAM read/write test
mw - memory write (fill)
nand - NAND sub-system
nboot - boot from NAND device
nm - memory modify (constant address)
printenv- print environment variables
protect - enable or disable FLASH write protection
regul - Regulator sub system
reset - Perform RESET of the CPU
run - run commands in an environment variable
saveenv - save environment variables to persistent storage
saveenv_wjs- print saveenv info
sdma - memory transfer using the i.MX SDMA Engine 有一个sdma命令，好像是从sd卡传输数据？

找了下NXP官方uboot，并没有这个命令，网上也找不到这个命令的详细用法，但是我随便敲了下，想要测试下这个命令看看出现什么结果，却发现这个命令是无效的。因为给出的提示是这样的

```
MX6SOLO STDPLUS U-Boot> sdma 1
sdma - memory transfer using the i.MX SDMA Engine

MX6SOLO STDPLUS U-Boot> sdma 1 1
sdma - memory transfer using the i.MX SDMA Engine

MX6SOLO STDPLUS U-Boot> sdma 1 1 1
SDMA initialization failed.
MX6SOLO STDPLUS U-Boot> sdma 1 1 1 1
sdma - memory transfer using the i.MX SDMA Engine

MX6SOLO STDPLUS U-Boot> sdma 1 1 1 1 1
sdma - memory transfer using the i.MX SDMA Engine

```

可以看到这个命令应该是3个参数的命令，但是这一行SDMA initialization failed表示命令失败了。看来没戏，按照之前对开发者的理解，他一定是在uboot发行版本中，把这个功能阉割掉了，或者是根本没实现。

那还有哪些命令可以从外部传输数据呢？

```
loadb - load binary file over serial line (kermit mode)
loady - load binary file over serial line (ymodem mode)

```

从串口传输数据，协议是kermit和ymodem，这俩我很熟悉，以前救路由砖的时候用过，WinXP的超级终端就支持这种文件传输协议。其中一个比另一个传输速率稍快一些。但是快能快到哪？理想状态下115200/8 = 14.4KB/s，实际上达到10KB/s就烧高香了。

虽然速度奇慢，这毕竟是一个救砖的PlanB，在缺少NAND编程器的情况下，也许这两条命令就是最后的救命稻草了。或许我可以从固件升级包提取文件，制作一个最简的rootfs，加上内核，估计也就5MB左右……先通过这个内核引导，然后有了USB支持，就可以用USB传输了。

接下来我还思考了下，通常情况下，制造商如何烧入镜像。我查询了NXP的开发资料，可以配置CPU的BOOTMODE，让CPU从USB引导，然后使用一个叫mfgtools的工具，传输bootstrap镜像到RAM，然后再通过USB或以太网传输文件，写入Flash。这个bootstrap镜像其实就是我刚才所想到的，kernel和精简的rootfs。

这种方法的关键在于如何将CPU配置从USB引导，这其实也是一个问题，因为这个配置是可以被制造商通过烧eFuse区域写死的（或者说OTP区域，One Time Programmable），也就是说，这个USB引导，在工厂生产阶段能用，用完之后把这个配置区域写死，就没人能改变CPU的引导方式了。

在uboot提示符使用md命令查看看了eFuse区域寄存器，确实已经被写死了。除非买一片新的CPU换上去，但是新CPU没有eFuse配置，需要从外部引脚配置BOOTMODE，电路改动难度非常大，几乎不可能完成。所以通过改变BOOTMODE，从USB引导，利用mfgtools烧固件救砖这个思路，没戏了。

我想，我已经有了一个PlanB，就是有提示符的uboot，就算搞成砖，也有办法修复，但是事情事实上远没有这么简单……我发现UBOOT中断引导后，系统每隔60秒就重启。那如果我用串口猫协议传输文件，60秒能传输多少数据！况且还有后面的kernel引导！60秒什么都做不了！

这种情况一定是有看门狗。

看门狗是什么？看门狗是一种预防机制，可以防止CPU或程序异常，导致产品失去正常功能。原理其实很简单，做一个定时器，定时器溢出了，就给CPU发送复位信号。为了防止定时器溢出，就必须有一个程序负责清空定时器，这个环节就叫做“喂狗”。如果因为CPU或程序异常因素，导致这段喂狗程序无法正常运行，那么CPU就会被看门狗无情的复位！

一开始，我觉得可能就是CPU内部集成的看门狗吧，反汇编uboot，把放狗的代码屏蔽掉就好了……用IDA确实找到了一处写看门狗寄存器的代码：

```
ROM:178037F0 sub_178037F0 ; CODE XREF: sub_17802044+14
ROM:178037F0 ; sub_1780229C+1C
ROM:178037F0 LDR R3, =0x20BC000 寄存器0x20BC000在datasheet上就是 WatchdogControl Register (WDOG1_WCR)（这CPU有两个狗），uboot写了狗1的寄存器，写入的4恰好是WDE(Watchdog Enable)，没错，跑不了，这个函数绝对叫EnableWatchdog！

可惜当我HACK掉这个函数，把第一句改为BX LR，让其直接返回。实验结果是狗并没有被关闭！后来研究代码发现这段代码根本就没有在启动流程被调用……这段代码的作用其实是ResetCPU(uboot reset命令)。

0×04看门狗

没想到费劲研究出的uboot救砖方案，竟被一只疯狗给破坏了！既然不是CPU内部的狗，那就一定是外部的狗！

这回没错，跑不了，肯定是瑞萨那个MCU干的！

首先我想弄清楚他是怎么复位CPU的。这个CPU没有RST引脚，只有一个POR_B引脚，其实就是个低电平的PowerOn Reset，POR引脚通常不是设计用来复位CPU的。系统在刚开始上电时，供电电路电压还不稳定，CPU不能正常启动。这一般就需要由供电芯片的PG(Power Good)引脚输出，来通知CPU的POR引脚。大致可以理解为是电源IC给CPU说：“大哥，我的电压稳定了，你可以正常工作了！”不过，一旦电压失稳，PG电平改变，CPU一样是会复位的！

我自信满满的认为是瑞萨的MCU通过拉低POR_B引脚来复位CPU的，于是用表开始找，找了小半天，竟然没找到，这个POR_B没有与MCU直接相连。

在这里分享个寻线小窍门，数字表调整到欧姆档，SELECT启用短路告警，这样一旦探测到短路的位置，表的蜂鸣器就会哔~~，于是寻找的方法就变成了，一个表笔按在POR_B的测试点上，一个表笔在MCU引脚上划来划去。

可惜没找到！难道不是通过拉低POR_B来实现复位的？

后来我发现，在复位的时候，机器的按钮背光灯，短暂的熄灭了零点几秒，难道复位的方法是关掉整机电源然后再打开！没错，这个MCU一定兼职了电源管理芯片的功能！

于是我开始找那几个电源IC的Datasheet，找到他们的EN使能引脚，然后找这些引脚与MCU的哪些引脚直接相连。

可惜又没找到！这不科学！难道不是直接相连，难道中间串了MOS管或三极管什么的？这没必要啊！难道中间串了电阻，电阻拉高，MCU开漏输出？查了下，使能引脚是高电平有效，那电阻是拉低，MCU输出高电平呗！在MCU附近寻找，果然发现了一些布局类似的电阻，然后你猜怎么着，我真的找到了与所有电源IC的EN引脚相连的MCU引脚！

现在事情清晰了，整套系统的供电都受MCU控制，MCU内部还实现了一只看门狗，来复位整套系统，并且复位的方式是控制所有供电IC的EN引脚。

我想，可以直接把这些供电IC的EN引脚全都改一下，全都拉高，不过这很麻烦。CPU的上电其实是有时序要求的，哪个电压先来哪个电压后来，都是有讲究的。除非是在CPU上电之后再去HACK EN引脚。不过这么一堆EN引脚，贸然去改动硬件来bypass看门狗，说不定还有其他什么我没发现的坑，有一定失败风险，直接在硬件上做改动，难度较高。

除非搞到喂狗指令，不然这个狗是很难绕过去了，我决定先放弃寻找关狗的方案。

0×05软件系统

接下来我主要研究的内容就是分析整套软件。分析其软件构成，软件包，库，驱动，内核模块。

看看挂载点

```
I have no name!@(none) ~$ df -k
Filesystem 1K-blocks Used Available Use% Mounted on
ubi0:rootfs 182808 155908 22064 88% /
mdev 86372 0 86372 0% /dev
none 5120 12 5108 0% /tmp
shm 86372 8 86364 0% /dev/shm
rwfs 512 236 276 46% /mnt/rwfs
rwfs 512 236 276 46% /var
ubi1:pss1 5960 64 5556 1% /pss1
ubi2:pss2 5960 32 5588 1% /pss2

```

Ubi0:rootfs是根文件系统，他的文件系统是ubifs，这是一套专门为NAND Flash设计的文件系统，具有坏块管理功能。

pss1和pss2里面文件不多，结构一样，估计2是1的备份。

```
I have no name!@(none) ~$ ls -li /pss1
total 52
65 -rwx------ 1 root root 33 Jan 1 00:00 DIAG_PSS_CODING.pss
66 -rwx------ 1 root root 33 Jan 1 00:00 FEATURE_MANAGER_PSS_BLK.pss
67 -rwx------ 1 root root 9 Jan 1 00:00 HMI_BT_A2DP_ONOFF_STATUS_PSS_BLK.pss
68 -rwx------ 1 root root 9 Jan 1 00:00 HMI_BT_ONOFF_STATUS_PSS_BLK.pss
69 -rwx------ 1 root root 9 Jan 1 00:00 HMI_BT_VISIBLE_ONOFF_STATUS_PSS_BLK.pss
119 -rwx------ 1 root root 9 Jan 1 00:00 HMI_EXLAP_SERVER_ONOFF_PSS_BLK.pss
150 -rwx------ 1 root root 2298 Jan 1 00:01 HMI_SYSTEM_APP_CONNECTION_PLIST_PSS_BLK.pss
118 -rwx------ 1 root root 10 Jan 1 00:00 HMI_SYSTEM_SAFECODE_ASSEMBLY_UNLOCK_PSS_BLK.pss
117 -rwx------ 1 root root 9 Jan 1 00:00 HMI_SYSTEM_SAFECODE_REMAINING_ATTEMPT_PSS_BLK.pss
70 -rwx------ 1 root root 28 Jan 1 00:00 HMI_SYSTEM_SETUP_PSS_NEWBLK.pss
71 -rwx------ 1 root root 78 Jan 1 00:00 TM_PSS_BT_FRIENDLY_NAME_PSS_BLK.pss
75 drwx--x--x 2 messageb messageb 1408 Jan 1 00:00 config
90 -rwxr-xr-x 1 messageb messageb 22 Nov 23 2016 hmi_autotest.ini
146 -rw-r--r-- 1 root root 393 Jan 1 00:00 stdplus.ini
I have no name!@(none) ~$ ls -li /pss2
total 52
65 -rwx------ 1 root root 33 Jan 1 00:00 DIAG_PSS_CODING.pss
66 -rwx------ 1 root root 33 Jan 1 00:00 FEATURE_MANAGER_PSS_BLK.pss
67 -rwx------ 1 root root 9 Jan 1 00:00 HMI_BT_A2DP_ONOFF_STATUS_PSS_BLK.pss
68 -rwx------ 1 root root 9 Jan 1 00:00 HMI_BT_ONOFF_STATUS_PSS_BLK.pss
69 -rwx------ 1 root root 9 Jan 1 00:00 HMI_BT_VISIBLE_ONOFF_STATUS_PSS_BLK.pss
70 -rwx------ 1 root root 9 Jan 1 00:00 HMI_EXLAP_SERVER_ONOFF_PSS_BLK.pss
71 -rwx------ 1 root root 2298 Jan 1 00:00 HMI_SYSTEM_APP_CONNECTION_PLIST_PSS_BLK.pss
72 -rwx------ 1 root root 10 Jan 1 00:00 HMI_SYSTEM_SAFECODE_ASSEMBLY_UNLOCK_PSS_BLK.pss
73 -rwx------ 1 root root 9 Jan 1 00:00 HMI_SYSTEM_SAFECODE_REMAINING_ATTEMPT_PSS_BLK.pss
74 -rwx------ 1 root root 28 Jan 1 00:00 HMI_SYSTEM_SETUP_PSS_NEWBLK.pss
75 -rwx------ 1 root root 78 Jan 1 00:00 TM_PSS_BT_FRIENDLY_NAME_PSS_BLK.pss
78 drwxr-xr-x 2 root root 304 Jan 1 00:00 app_data
76 -rwxr-xr-x 1 root root 22 Jan 1 00:00 hmi_autotest.ini
77 -rw-r--r-- 1 root root 393 Jan 1 00:00 stdplus.ini
I have no name!@(none) ~$

```

看文件名猜测应该是与软件配置相关的一些东西。

再看看/dev目录，都有什么设备。

```
I have no name!@(none) ~$ ls /dev
apm_bios loop7 mxc_vpu ttymxc3
console mem mxs_viim ttymxc4
cpu_dma_latency mtd0 network_latency ttyp0
crypto mtd0ro network_throughput ttyp1
event0 mtd1 null tuner_knob
event1 mtd1ro pts ubi0
fb mtd2 ptyp0 ubi0_0
fb0 mtd2ro ptyp1 ubi1
fb1 mtd3 ram0 ubi1_0
full mtd3ro ram1 ubi2
galcore mtd4 ram2 ubi2_0
i2c-0 mtd4ro ram3 ubi_ctrl
i2c-2 mtd5 random uinput
i2c-3 mtd5ro sda urandom
kmem mtdblock0 sda1 usbdev1.1
kmsg mtdblock1 shm usbdev1.2
loop0 mtdblock2 snd v850srq
loop1 mtdblock3 spidev0.0 video
loop2 mtdblock4 tracebuf video0
loop3 mtdblock5 tty video1
loop4 mxc_asrc ttymxc0 video16
loop5 mxc_ipu ttymxc1 video17
loop6 mxc_mem ttymxc2 zero

```

fb(framebuffer)是显存。

event0和event1可能是键盘和触摸板。

3个i2c接口，用途不明，可能是配置功放芯片，摄像头芯片或者触摸板，收音机等用途。

一个spi接口，通过分析/opt/libHAL.so发现是与MCU通信用的。

tuner_knob，可能与前面板两个旋钮相关。

v850srq，这个看名称就知道肯定也和MCU相关，因为那个瑞萨的MCU代号是RH850，而这个MCU的指令集就是NECV850，IDA恰好支持这个指令集。

video设备可能就是倒车摄像头芯片ADV7182的接口。

再看看etc/rc.d目录，有一些系统引导脚本，文件不多，很好分析。

```
I have no name!@(none) ~$ ls /etc/rc.d/
init.d rc.local rc.serial rcS rc_gpu.S
rc.conf rc.modules rc.ts rc_mxc.S
I have no name!@(none) ~$ ls /etc/rc.d/init.d
boa dropbear mdev portmap udev
depmod filesystems mdev_bak settime
devfsd hostname modules smb
dhcp hotplug mount-proc-sys sshd
dhcpd inetd network syslog
I have no name!@(none) ~$

```

我可以修改其中的rcS脚本，在里面插入一个read-t 指令，这样我就可以打断Linux系统的引导，来制造一个failsafe模式。这个模式有什么用呢？其实就是为了防止后面的修改出错，导致系统直接崩溃掉了。有这个failsafe模式，我就可以回退修改。例如还没进入终端系统就崩溃了，我只能在uboot模式恢复系统了，会很麻烦的。

但是暂时这个修改没有用，因为需要喂狗程序，这个程序在哪，是哪个程序，我都不能确定……先算是一个思路吧。

看看/opt目录

```
I have no name!@(none) ~$ ls /opt
AndroidAuto g_ether.ko mfid
aoa_adapter g_file_storage.ko net_windows.sh
asix.ko g_hid.ko nw_vmf_ctrl.dat
audio_proc g_iap_ncm_audio.ko nw_vmf_trace.dat
bt g_iap_ncm_eap.ko pss
camera gresfiles pwr_agent
carlife_daemon hal_init ringtone
carplay_App hmi sc_db.db
carplay_testmode.sh hmi.bak screenshot
carplayd hmi_autotest.ini sourceme.sh
cdc_ncm.ko i2cdetect start_carlife.sh
clear-cache.sh iap2d start_carplay.sh
config iperf start_mdnsd.sh
connect_mgr launch.sh stdplus.ini
data launch_aa.sh sysupdate
diag lib test.sh
diag_eol libcidb_sdk.so tools
eol_test.wav md_adapter usbnet.ko
exlap mdnsd vgw
feature_config.ini media vmf
ftpdup2.sh memtool

```

这里面都是车机的应用程序，有库，配置，图片字体资源文件，各种脚本……

先看看正在跑的都有哪些程序吧。ps -ef

```
I have no name!@(none) ~$ ps -ef
PID USER TIME COMMAND
1 root 0:00 init
2 root 0:00 [kthreadd]
3 root 0:00 [ksoftirqd/0]
4 root 0:00 [kworker/0:0]
5 root 0:00 [kworker/u:0]
6 root 0:00 [migration/0]
7 root 0:00 [khelper]
8 root 0:00 [kworker/u:1]
235 root 0:00 [sync_supers]
237 root 0:00 [bdi-default]
239 root 0:00 [kblockd]
271 root 0:00 [imx6q-ecspi.0]
274 root 0:00 [imx6q-ecspi.1]
281 root 0:00 [khubd]
298 root 0:00 [ipu1_task]
299 root 0:00 [ipu1_task]
327 root 0:00 [usb_wakeup thre]
329 root 0:00 [otg_switch]
341 root 0:00 [kswapd0]
402 root 0:00 [fsnotify_mark]
411 root 0:00 [crypto]
467 root 0:00 [kapmd]
507 root 0:00 [mtdblock0]
512 root 0:00 [mtdblock1]
517 root 0:00 [mtdblock2]
522 root 0:00 [mtdblock3]
527 root 0:00 [mtdblock4]
532 root 0:00 [mtdblock5]
540 root 0:00 [ubi_bgt0d]
572 root 0:00 [vpu_wq]
577 root 0:00 [galcore workque]
578 root 0:00 [kworker/0:1]
579 root 0:00 [galcore daemon ]
580 root 0:00 [galcore daemon ]
646 root 0:00 [scsi_eh_0]
647 root 0:00 [usb-storage]
723 root 0:00 [kworker/u:2]
725 root 0:00 [kworker/0:2]
756 root 0:00 race_daemon sda true /devices/platform/fsl-ehci.0/usb1/1-
788 root 0:00 [ubi_bgt1d]
792 root 0:00 [ubi_bgt2d]
795 root 0:00 [ubifs_bgt1_0]
798 root 0:00 [ubifs_bgt2_0]
808 root 0:00 ./vmf
811 root 0:00 ./pwr_agent -o -v
812 root 0:00 ./sysupdate
825 root 0:00 ./mfid
833 root 0:00 -sh
842 root 0:00 ./vgw
848 root 0:00 [irq/283-atmel_m]
872 root 0:02 ./hmi
874 root 0:03 ./media
876 root 0:00 ./pss
880 root 0:00 ./audio_proc
919 root 0:02 ./bt
921 root 0:00 ./exlap
958 root 0:02 /usr/bin/devicemanager 5 0 /usr/bin 239 393247 1 1 0 0 0
967 root 0:00 [race_daemon]
981 root 0:00 ./diag
983 root 0:00 ./diag_eol
987 root 0:00 ./screenshot
992 root 0:00 [flush-ubifs_1_0]
993 root 0:00 [flush-ubifs_2_0]
998 root 0:00 ./mdnsd -debug
1009 root 0:00 ./connect_mgr
1014 root 0:00 ./carplay_App
1021 root 0:00 ./iap2d
1024 root 0:00 ./carlife_daemon
1026 root 0:00 ./carplayd
1050 root 0:00 {clear-cache.sh} /bin/sh ./clear-cache.sh
1095 root 0:00 [flush-8:0]
1106 root 0:00 sleep 2
1107 root 0:00 ps -ef

```

发现有很多进程，由于名字都是缩写，很难直接猜出这些程序的用途。例如vmf、mfid、vgw、hmi、exlap、diag等等这些都是鬼？他们大多没有-h –help 之类能显示信息的参数可用，想知道他们的用途，估计是要上IDA逆向一下……

经过了几天的研究，我终于知道了部分核心程序的用途。

他这套软件，大部分都是在后台工作的，只有少部分是QT编写的，有界面的。例如hmi和sysupdate。其中hmi体积最大，他就是界面主程序，他包含了所有的图形资源，多国语言字符串。

其他程序基本都没有界面，界面其实都是在hmi中。UI进程和逻辑进程之间使用类似IPC的消息机制，这个机制在这套系统中称为vmf。

那个vmf程序，起到一个消息中转站的作用，vmf可能是Virtual Message Function的缩写？他有Usage信息，显示的是vmffor linux version DOMAIN-SOCKET。

/opt/lib/libvmf_client.so这个就是vmf客户端支持库，很多程序通过调用libvmf_client.so的nw_vmf_send/receive函数来实现跨进程的IPC消息传递，这套消息机制是基于socket。在/tmp/目录有很多127.X.X.X文件，可能与此相关。在文章后面，我将详细分析这套API的原型和消息数据结构。

```
I have no name!@(none) ~$ ls /tmp
127.0.0.1 127.3.215.1 allgorpccomm1
127.3.104.1 127.3.219.1 carlife
127.3.106.1 127.3.241.1 devmgr_debug.txt
127.3.106.2 127.3.246.1 diag_enter_main
127.3.106.3 127.3.246.2 diag_initialize_OK
127.3.106.4 127.3.40.1 eol_enter_main
127.3.106.5 127.3.40.2 hdd
127.3.106.6 127.3.40.4 icr_device_process.txt
127.3.106.7 127.3.43.1 keyGenerationFile
127.3.106.8 127.3.44.1 race
127.3.106.9 127.3.74.1 race_key_gen_file.txt
127.3.108.1 127.3.74.2 shm_key_gen.txt
127.3.112.1 127.4.0.1 shm_key_index.txt
127.3.112.2 127.4.2.1 vaf_comm_mirrorlink
127.3.151.1 DevMgrRunning.txt var
127.3.153.1 MFISocket vmf_trace_startup
127.3.153.2 RPCClient1026
127.3.213.1 RPCSocket

```

pwr_agent，从名字看，显然是电源管理相关，我一直认为就是这个进程，在不断地与MCU通信，喂狗。

我用IDA把他逆了个遍，他确实有这个动作，但是他只是构造了个消息包，包发给了谁并不知道……他其实不光是电源管理，在/opt/launch.sh脚本中可以发现，脚本执行了vmf hal_init pwr_agent sysupdate mfid，就没有了。其实大多数进程，都是由pwr_agent带起来的，这在IDA逆向中能发现。

```
.rodata:00011183 ALIGN 4
.rodata:00011184 DCD a_Diag+8 ; ""
.rodata:00011188 DCD a_VgwDevNull21 ; "./vgw > /dev/null 2>&1 &"
.rodata:0001118C DCD a_PssDevNull21 ; "./pss > /dev/null 2>&1 &"
.rodata:00011190 DCD a_MediaDevNull2 ; "./media > /dev/null 2>&1 &"
.rodata:00011194 DCD a_Diag+8 ; ""
.rodata:00011198 DCD a_Audio_procDev ; "./audio_proc > /dev/null 2>&1 &"
.rodata:0001119C DCD a_BtDevNull21 ; "./bt > /dev/null 2>&1 &"
.rodata:000111A0 DCD a_Diag+8 ; ""
.rodata:000111A4 DCD a_HmiDevNull21 ; "./hmi > /dev/null 2>&1 &"
.rodata:000111A8 DCD a_WifiDevNull21 ; "./wifi > /dev/null 2>&1 &"
.rodata:000111AC DCD aCatProcDeferre ; "cat /proc/deferred_initcalls"
.rodata:000111B0 DCD a_Diag_eolDevNu ; "./diag_eol > /dev/null 2>&1 &"
.rodata:000111B4 DCD a_Audio_procDev ; "./audio_proc > /dev/null 2>&1 &"
.rodata:000111B8 DCD a_DiagDevNull21 ; "./diag > /dev/null 2>&1 &"
.rodata:000111BC DCD a_ScreenshotDev ; "./screenshot > /dev/null 2>&1 &"
.rodata:000111C0 DCD a_Start_carplay ; " ./start_carplay.sh > /dev/null 2>&1 & "
.rodata:000111C4 DCD a_Connect_mgrDe ; "./connect_mgr > /dev/null 2>&1 &"
.rodata:000111C8 DCD a_Carlife_daemo ; "./carlife_daemon > /dev/null 2>&1 &"
.rodata:000111CC DCD a_ExlapDevNull2 ; "./exlap > /dev/null 2>&1 &"
.rodata:000111D0 DCD a_Launch_aa_shD ; "./launch_aa.sh > /dev/null 2>&1 &"
.rodata:000111D4 DCD a_Net_windows_s ; "./net_windows.sh > /dev/null 2>&1 &"
.rodata:000111D8 DCD a_ClearCache_sh ; "./clear-cache.sh > /dev/null 2>&1 &"
.rodata:000111DC DCD a_Start_mdnsd_s ; "./start_mdnsd.sh > /dev/null 2>&1 &"

```

hal_init，这个进程很简单，只是调用了libHAL.so库中的一些初始化函数就结束了。

```
int __cdecl main(int argc, const char **argv, const char **envp)
{
int v3; // r0@1
int v4; // r0@1
int v5; // r0@1

v3 = hal_gpio_init(argc, (int)argv, (int)envp);
v4 = hal_i2c_init(v3);
v5 = hal_spi_init(v4);
hal_uart_init(v5);
return 0;
}

```

vgw，这是关键进程，我发现把他杀了，系统过一会就重启，所以他是喂狗嫌疑对象。通过逆向，发现他调用了libHAL.so里面的spi_ipcl_read、spi_ipcl_write等函数，通过Search And Replace搜索整个opt目录也证实，只有vgw程序调用了上面的spi_ipcl_read/write接口。这个vgw是什么缩写，我没找到，可能是Vehicle Gateway或者VIP Gateway？感觉这种解释不太靠谱……

/opt/lib目录就是这套程序的库文件了。

```
I have no name!@(none) ~$ ls /opt/lib
bt libOSAL.so
libAirPlay.so libOSAL.so.1
libAirPlaySupport.so libOSAL.so.1.0
libCarPlayAudio.so libOSAL.so.1.0.0
libCarPlayClient.so libdns_sd.so
libCarPlayDevice.so libiap2.so
libCarPlayPlatform.so libmfi.so
libCarPlayPlatformDevice.so libprotobuf-c.so
libCarPlayPlatformMFI.so libprotobuf-c.so.1
libCarPlayPlatformScreen.so libvmf_client.so
libCarPlayPlatformSystem.so libvmf_trace.so
libHAL.so libximage.so
libHAL.so.1 libximage.so.1
libHAL.so.1.0 libximage.so.1.0
libHAL.so.1.0.0 libximage.so.1.0.0
libHAL.so.1.0.0.bak media
libHAL.so.1.0.0.read mirrorlink
libHAL.so.1.0.0.write screenshot

```

重点关注其中的2个，libOSAL.so和libHAL.so，前者是操作系统支持库，可能是为了方便跨平台开发，将一些系统级API重新封装了一下，例如线程，定时器，互斥锁等。后者是硬件层面的接口，包括uart，spi，gpio，i2c等接口的读写函数封装。其他都是carplay carlifemirrorlink等程序的支持库。

/opt里面很多程序都被strip掉了符号表。在IDA分析时没有符号帮助，很难搞懂一些函数的作用，还好这些程序都调用了日志接口log_optional来打印日志，因此能获知一些函数的真实名字，以及他们的用途。比如下面这个例子：

显然，得知这个函数名字叫pwr_agent_vmf_rcv_handle_round_comm，日志类别是radio_updown。

log_optional的消息打印是受配置文件控制的，这些配置文件以进程名作为区分，位于/opt/config或/pss1/config

例如下面这个pwr_agent的config

```
I have no name!@(none) ~$ cat /pss1/config/pwr_agent.conf
[OPTION]
ENABLE=1 可见有很多开关，控制了哪些debug消息需要打印，哪些不打印。把一些需要的开关打开，可以方便逆向分析工作。主要开关就是ENABLE=1，OUTPUT是类别控制，控制对应的类别的日志要打印出来。

/opt/ftpdup2.sh这个脚本很有意思，他insmod了asix.ko，然后用ifconfig配置了ip地址，还启动了ftp服务。看来是开发人员为了方便网络传输文件留下来的。这个asix.ko是ASIX的usb转以太网芯片的驱动，很多常见的USB转网口的产品都是用ASIX芯片方案，例如绿联的USB百兆网卡。

```
I have no name!@(none) ~$ cat /opt/ftpdup2.sh
#!/bin/sh
#insmod mii.ko
#insmod usbnet.ko
insmod asix.ko

ifconfig eth0 192.168.5.1 up
while [ $? -ne 0 ]
do
sleep 1s
ifconfig eth0 192.168.5.1 up
done

echo network up at 192.168.5.1

cd /bin
if [ ! -e /bin/ftpd ]
then
ln -s busybox ftpd
fi

if [ ! -e /bin/tcpsvd ]
then
ln -s busybox tcpsvd
fi

cd /opt
tcpsvd -vE 0.0.0.0 21 ftpd -w &

```

我试着运行了一下，确实把网卡带起来了……

/opt还有一些usbgadget驱动程序，例如g_ether.ko和g_file_storage.ko，由于这台机器前面板的USB接口实际上是CPU的USB_OTG接口，因此我怀疑开发人员用这个驱动，直接把USBOTG虚拟成MassStorage或者网卡，与开发机用USB线连起来就能传文件了，是不是很有意思……

另外我还在/opt/tools发现了两个小程序

```
I have no name!@(none) ~$ ls -li /opt/tools/
total 16
16350 -rwxr-xr-x 1 messageb messageb 7555 Nov 23 2016 vmf_rcv
16349 -rwxr-xr-x 1 messageb messageb 7038 Nov 23 2016 vmf_send

```

Search AndReplace搜索后发现，这两个程序并没有被任何程序或脚本使用，应该是开发人员在开发或调试时留下的，有意思……这个vmf_send很有用，后面会用到。

0×06升级包分析

不知道网友有什么渠道，拿到了还没有发布的187B的升级包，上传到了网上，可以在百度网盘、或QQ群，找到这些升级包。用升级包给187A升级后，可以比原系统多出许多高级功能。比如苹果CarPlay，百度CarLife，还有什么超级蓝牙，我也没用过，不知道具体是怎么个超级法…

网友放出来的这些升级包，事实上可能都是开发阶段的测试包，有很多bug。有一个固件要求运行内存是512M，在256M的机器上刷了会变砖。另一个固件包支持256M内存，但也是Bug最多的版本。所以很多网友把天宝187A的机器换了内存，还有一些商家提供此类换内存服务，或者售卖已经换好内存的车机。

另外有少数商家有5314版本的升级包，这个版本与上市后的187B系统版本相同，估计算是正式版。但是这个升级包在网上找不到，貌似只掌握在少数以刷机为盈利目的的商家手上，且不外卖，在咸鱼上卖的5314据说都是假包。

接下来研究一下升级包的构造，升级包其实就是一个tar包，没有签名。

例如这个版本的包用7z打开是这样

用户名组名是wangshi，王石？看来制作升级包的系统，有是一个叫wangshi的用户，也许这个也是那位工程师的真实姓名……

看一下根目录的config.txt文件

```
kernel_update:1
bootloader_update:1
vip_update:1
bt_update:1
middleware_update:1
[file]:[opt]
[file]:[etc]
[file]:[usr]
[file]:[mirrorlink]
[file]:[lib/firmware]

```

好简单的设计，一目了然啊……

其中的vip_update其实指向的是vipImage.bin，大小1MB，文件末尾有哈希。这正好是瑞萨MCU的Flash大小，那么这个MCU在软件内部就是被叫做VIP咯，VIP是什么呢？Very Important Processor？Vehicle InformationProcessor？

其实如果知道了这个MCU的地址空间布局，完全可以拖进IDA逆向一下，看看里面的具体实现……不过经过后来一番分析，已经知道了VIP的大致作用：CAN总线通讯，身份信息，电源管理等等。身份信息包括零件号，平台(PQ/MQB)，序列号，车型(VW/SKODA)等。

现在知道这个MCU的代号其实就是VIP了，而且在分析软件程序的过程中，有很多字符串提到了VIP，那么我在后面文章就以VIP来称呼吧。

bt_update就是蓝牙了，这个机器的蓝牙模块是ALPS的一款模块，网上找不到任何资料。与CPU的通讯是通过UART串口。升级包bt_ac_firm目录里面的文件就是蓝牙控制芯片的固件了。

middleware就是中间件了，也就是opt那些目录，想覆盖哪个目录，写进config.txt就可以了。

升级包还有一个配置文件/opt/stdplus.ini。

```
[Version]
AP_VER=5.4.1.1
CUSTM_PQ=Y307
CUSTM_MQB=Y307
UBOOT_VER=uboot_1.3.2
UIMAGE_VER=uImage_5.0.11
KERNL_VER=3.0.35
BT_VER = C150
VIPAP_VER=5.4.1.1
VIPBL_VER=2.0.2
[Upgrade]
PACK_TYPE=VW_PQ_PSS
[CODING]
BT=0
CODINGS=0

```

里面有两个字段是必须的，Version小节的AP_VER字段，不能与主机/opt/stdplus.ini一致，否则sysupdate升级时会提示变量冲突。有Upgrade小节的PACK_TYPE，必须与主机/opt/stdplus.ini保持一致，否则不能升级。

其他字段都是可选的，根据升级需要设定即可。

另外升级包里面有一个script目录，里面有两个脚本。

看名字很容易猜得出，一个脚本是更新前执行，一个是更新后执行。

另外还有一个有意思的地方，用7z解压缩那个kernelImage，记事本打开，搜Linuxversion，可以看到编译人员的机器名和用户名，如图。

看，机器名是HP-640，用户名是changchun，难道内核编译人员来自长春？

升级包的结构清楚了，再通过逆向/opt/sysupdate，就对更新过程有了进一步了解。

0×07更新流程分析

通过逆向/opt/sysupdate可以对更新流程有个大致了解，具体的逆向分析流程我不多说了，他这个程序是qt写的，在log_optional这个日志打印函数的帮助下，很容易逆向/跟踪出具体流程。流程大致就是：验证包合法性，设置/sysupdate/flag.ini更新标志，保存包文件名，重启系统，重启后rcS脚本检查flag.ini文件，得知需要更新系统，然后走更新系统流程，启动sysupdate。sysupdate执行run_before_app_update_sh，刷内核（访问/dev/mtd，不是通过dd），替换中间件(hostapp)，刷bootloader(调用kobs-ng命令)，执行run_after_app_update_sh，刷蓝牙(uart传输)，刷VIP(spi传输)。

这个更新流程中的重启，很有意思，不是调用的reboot命令，而是硬重启！

我打开了sysupdate的log日志功能，通过日志输出，可以大致确定他是通过调用SendUpdateStatus(this, 1)来实现重启的，因为打印了这个日志就就没有日志了。

SendUpdateStatus这个函数发出了一个vmf消息，可能被vgw接收了，然后被vgw转手发给了VIP，由VIP触发了重启动作。这个函数的名字是SendUpdateStatus，发送更新状态。估计VIP还关注了更新的状态，能够根据更新状态来产生不同的动作，而这个状态1，就对应着复位CPU。

另外我还发现，一旦这个命令发出去重启了CPU，狗就被关起来了！电源不会每隔60秒被重置一次，这是一个很有意思的发现！

0×08破解通讯协议

现在知道的情况就是，pwr_agent肯定有一个定时器，每隔一段时间喂狗，sysupdate能通知VIP更新状态，当状态为1时，VIP就关掉了狗。

现在就有了两种办法关狗，一个是利用喂狗协议，一个利用升级状态协议。都是通过协议，现在没有协议，怎么办呢，想办法抓到通讯协议就好办了。

与VIP通讯的是vgw进程，IDA看看他里面的函数，有几个spi_ipcl_xxx的函数引起了我的注意，看来是通过调用libHAL.so的spi_ipcl_xxx读写函数实现通讯。前后找了下，没有可以利用的log打印。

再看看libHAL.so的spi_ipcl_write，竟然有日志打印，这太好了……

spi_ipcl_read也调用了这个IPCL_Print_Msg，第三个参数控制了是否输出日志，可惜被传了0，估计是一个宏开关，被编译时指定的。这难不住我，用WinHex把对应的指令字节码改掉。

有两种改法，改IPCL_Print_Msg外部call的传参，也就是0改成1，把MOV R2指令改成 MOV R2,#0就可以了；还可以进入IPCL_Print_Msg内部去改，去掉对第三个参数的判断，也就是将前面的LDMEQFD指令改成NOP。

由于IPCL_Print_Msg并不能打印出这个消息是read还是write，因此我使用前一种改法，改出2个文件，libHAL.so.read和libHAL.so.write，并备份原机文件为libHAL.so.bak。另外这个printf的fmt是“ %x ”，输出内容会很不友好，我改成了 “%02x ”。

有了这样3个文件，我就随时可以根据一条shell命令来更换libHAL文件。

```
恢复libHAL
mount -o remount rw / && cd /opt/lib && cp libHAL.so.1.0.0.bak libHAL.so.1.0.0

设置IPCL read
mount -o remount rw / && cd /opt/lib && cp libHAL.so.1.0.0.read libHAL.so.1.0.0

设置IPCL write
mount -o remount rw / && cd /opt/lib && cp libHAL.so.1.0.0.write libHAL.so.1.0.0

```

由于我替换的是系统关键库，为了避免错误发生，我修改了启动脚本rcS，加入了一个failsafe安全模式功能。改法是这样的

这样修改后，在系统启动时，我有一秒时间来输入y回车来进入安全模式，防止修改/opt/lib库文件导致系统关键进程无法启动、系统崩溃。不过因为没有喂狗，这个安全模式是有时间限制的，复制粘贴上面的指令能迅速帮我还原备份文件。

首先我替换了libHAL.so.write文件，重启后串口终端确实打印了大量通信报文。我发现每隔1秒都有一个8字节的报文送出，且有类似递增序号的字节，没错这一定是在喂狗！

另外我也抓到了sysupdate在调用SendUpdateStatus(xxx,1)关狗并重启时的8字节报文。也在上面的图片中

这些报文都是vgw进程通过spi_ipcl_write向VIP写入的。而这些报文的实际发起者其实并不是vgw。喂狗是pwr_agent发起的。关狗重启是sysupdate发起的。

虽然我可以通过调用libHAL.so的spi_ipcl_write来实现我想要的喂狗和关狗功能了，但是这要编译一个小程序，编译程序需要准备交叉编译环境，还要把这个程序放进去……呀，好麻烦……

为了体现我的高端，我还进一步研究了进程间的通讯，也就是那个vmf消息。比如这个喂狗流程应该是这样的：pwr_agent定时器，构造了一个包，发给了vmf，vmf转给了vgw，vgw通过spi喂狗。

是不是很复杂？没错，厂商确实是这样设计的，这套消息也确实是这样工作的……因为我把vmf杀掉，vgw就收不到消息，不再喂狗了。

首先我想用sysupdate来展开分析工作，我感觉我对这个程序了解的更多一些。我先用IDA定位到SendUpdateStatus(int this, int arg_status)。这个函数第二个参数就是状态值，有1、2、3、4、5、6、7……之类的，在这里关狗重启status值为1。他构造了一个5字节的包，传给了UpdateSendVmfMsg。

构造的包其实就是5个字节， 8F 19 00 01[StatusByte]

UpdateSendVmfMsg(_BYTE*arg_databuf, int arg_datalen)函数又将收到的包调用PackVmfMsg重构了一下，然后把重构的包调用libvmf_client.so!nw_vmf_send_basic发出去了。先看看包是怎么被PackVmfMsg重构的。

char __fastcallPackVmfMsg(_BYTE arg_databuf, unsigned int arg_datalen)

目前只是知道了数据包的布局，但是具体的字段含义还不清楚，例如那个8F是什么，19是什么，00 01有可能是BigEndian的包长？不过通过交叉引用vmf的消息处理接收函数nw_vmf_receive(inthandle, char *buf, int bufsize)不难得出，这个8F其实是groupid，19是eventid。

例如下面是对pwr_agent程序的nw_vmf_receive交叉引用分析出来的log_optional。

从这个log_optional的上下文代码中，我得出了这个vmf的数据包结构：

```
Offset Type Value
0 byte 定值2
20 byte groupid
21 byte eventid
24 word payload_len
26 byte[] payload

```

看起来很简单，只要按照这个数据结构把包发出去就可以了。这时就可以利用前面分析文件系统时发现的/opt/tools/vmf_send。

这个文件很小只有7K，拖进IDA，虽然没有usage消息，但是很轻松就逆出了他的用法：

由上面main函数得出vmf_send的用法

```
Usage: vmf_send [groupid] [eventid][payload_dec_byte0] [payload_dec_byte1] ...

```

有了这个命令，理论上我只需要知道groupid和eventid，就可以用vmf_send发送vmf消息给相应的进程，控制其产生相应的动作。例如上面分析到的关狗重启vmf消息，我可以调用vmf_send命令：

```
/opt/tools/vmf_send143 25 1

```

这样vgw就会收到vmf消息，然后发送8个字节的SPI报文给VIP，触发VIP的关狗重启动作。

有了vmf的相关函数原型以及vmf消息结构，我又逆向了pwr_agent，找到了他的喂狗程序。

然后分析出了喂狗的vmf消息结构

```
groupid = 143
eventid = 3
payload_len = 4
payload:
02
XX 每秒递增
01
01 变量VipEnabled的值，由argv参数-v指定

```

现在终于可以调用tools/vmf_send来发送喂狗消息，我写了一个脚本来实现后台喂狗：

```
#!/bin/sh

while true
do
for seqnum in $(seq 0 255)
do
echo $seqnum
/opt/tools/vmf_send 143 3 2 $seqnum 1 1
sleep 1
done
done

```

为了验证这个脚本是否有效，我重启进入failsafe模式，顺序启动vmf，启动hal_init，启动vgw，然后再运行这个脚本，过了1分多钟，狗确实被喂的很舒服，没有出来乱咬！哈哈！

现在就可以完善这个failsafe模式了，修改rcS脚本：

```
read -t 1 -p "Safemode[y/N]:" safemode
echo -e "\n"
if [ "$safemode" = "y" ];
then
echo "safemode"

/opt/vmf &
/opt/hal_init
/opt/vgw &
/opt/keepalive.sh > /dev/null

exit 0
fi

```

这样系统就有了具有喂狗功能的failsafe模式，是不是很厉害！

0×09 后记

至此，我已经有了一堆PlanB，我可以放心大胆的折腾这台机器了……

接下来的研究就轻松的多了。我又研究了很多有意思的东西，例如：

```
a)导出187A原厂文件系统
b)做了个升级包能把rootfs导出来。但是不包括蓝牙和VIP固件…
c)Hack了512M版本的固件，让他在256M内存上完美的跑起来了。
d)由于升级后，车机前面板键位错乱，是187B的键位，因此我Hack了hmi修改了键位。
e)写了个小程序生成BITMAP并计算哈希，做了个升级包可以替换开机Logo。
f)做了个升级包能进入一次failsafe模式，并加载网卡驱动，启动ftp…
g)做个原机备份固件。
h)有网友提供了编程器固件，从编程器固件提取uboot、kernel、rootfs。
i)...

```

等有时间我准备把上面的abcdefgh，和新研究出来的东西发在这里，作为续集。希望这篇文章，能给同样爱好折腾硬件折腾软件的人带来一些帮助……

*本文原创作者：mengxp，属Freebuf原创奖励计划，未经许可禁止转载

[-->](http://www.freebuf.com/author/mengxp)

[mengxp](http://www.freebuf.com/author/mengxp)
-->

1
篇文章
等级：
1级

[](http://www.freebuf.com/author/mengxp)
[](http://www.freebuf.com/user/pm?pmaction=newmessage&to=mengxp)

[](#)
[](#)
[](#)

|

[](http://www.freebuf.com/oauth)
[](#respond)

|

- 上一篇：[Natas overthewire 0-15解题报告](http://www.freebuf.com/articles/others-articles/128481.html)

- 下一篇：
[八百元八核的服务器？二手服务器搭建指南](http://www.freebuf.com/geek/130366.html)

这些评论亮了

-
[](http://www.freebuf.com/author/hjcsdc)

[hjcsdc](http://www.freebuf.com/author/hjcsdc)

(1级)

[回复](?replytocom=#respond)

看了一半 先膜拜~~

)7( [亮了](javascript:acv_vote(212273,1,2);)

[发表评论](#respond)
已有 57 条评论

-

[](http://www.freebuf.com/author/hjcsdc)

[hjcsdc ](http://www.freebuf.com/author/hjcsdc)

(1级)

2017-03-16

[回复](?replytocom=212273#respond)

1楼

看了一半 先膜拜~~

[亮了](javascript:acv_vote(212273,1);)(7)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

Jerry

2017-03-16

[回复](?replytocom=212278#respond)

2楼

屌到爆炸

[亮了](javascript:acv_vote(212278,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

cat

2017-03-16

[回复](?replytocom=212279#respond)

3楼

前提是有辆车

[亮了](javascript:acv_vote(212279,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

亲爱的

2017-03-16

[回复](?replytocom=212281#respond)

4楼

看不懂。。很厉害的样子

[亮了](javascript:acv_vote(212281,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

elppas

2017-03-16

[回复](?replytocom=212282#respond)

5楼

楼主厉害，软硬通吃！我原来也改过某电视盒子，取得权限，删除预装软件及其他，方法是参考网上的，基本上是模仿。向楼主这样自己分析搞定，只能膜拜了。希望多出好文，谢谢。

[亮了](javascript:acv_vote(212282,1);)(4)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/taffy5366)

[taffy5366 ](http://www.freebuf.com/author/taffy5366)

(1级)
这家伙太懒了，还未填写个人描述！
2017-03-16

[回复](?replytocom=212283#respond)

6楼

膜拜

[亮了](javascript:acv_vote(212283,1);)(1)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/郭小侠lower)

[郭小侠lower ](http://www.freebuf.com/author/郭小侠lower)

(1级)

2017-03-16

[回复](?replytocom=212285#respond)

7楼

楼主可以私聊么？想联系一下你。

[亮了](javascript:acv_vote(212285,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

yyy3333

2017-03-16

[回复](?replytocom=212287#respond)

8楼

楼主发完文章…… 然后就启动车去了 大众4S ……

[亮了](javascript:acv_vote(212287,1);)(1)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

exp

2017-03-16

[回复](?replytocom=212288#respond)

9楼

这几把牛逼

[亮了](javascript:acv_vote(212288,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/Cunlin)

[Cunlin ](http://www.freebuf.com/author/Cunlin)

[](http://zhuanlan.freebuf.com)
(2级)
微信同用户名~
2017-03-16

[回复](?replytocom=212289#respond)

10楼

holy niubi~~~

[亮了](javascript:acv_vote(212289,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/susu1399)

[susu1399 ](http://www.freebuf.com/author/susu1399)

(1级)

2017-03-16

[回复](?replytocom=212291#respond)

11楼

拜服！厉害！

[亮了](javascript:acv_vote(212291,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/DDvv)

[DDvv ](http://www.freebuf.com/author/DDvv)

(4级)
app爬虫爱好者
2017-03-16

[回复](?replytocom=212292#respond)

12楼

感动的要哭要哭的，早上还在想杂从板子上找调试接口，吃个午饭回来就有了这篇文章！

别拦我，我要哭。

右键保存了365份。

[亮了](javascript:acv_vote(212292,1);)(5)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/ChaceShadow)

[ChaceShadow ](http://www.freebuf.com/author/ChaceShadow)

(1级)

2017-03-16

[回复](?replytocom=212297#respond)

13楼

五体投地

[亮了](javascript:acv_vote(212297,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/cuger)

[cuger ](http://www.freebuf.com/author/cuger)

(1级)

2017-03-16

[回复](?replytocom=212298#respond)

14楼

牛人！！！

[亮了](javascript:acv_vote(212298,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

Ag_Bullet

2017-03-16

[回复](?replytocom=212306#respond)

15楼

刚看完[哆啦A梦吃惊]

[亮了](javascript:acv_vote(212306,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

PigletTurbo

2017-03-16

[回复](?replytocom=212307#respond)

16楼

真正的Geek

[亮了](javascript:acv_vote(212307,1);)(1)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/h2z)

[h2z ](http://www.freebuf.com/author/h2z)

(2级)

2017-03-16

[回复](?replytocom=212315#respond)

17楼

看了一半 先膜拜~~

[亮了](javascript:acv_vote(212315,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/freebug_n)

[freebug_n ](http://www.freebuf.com/author/freebug_n)

(4级)
You hava no body！
2017-03-16

[回复](?replytocom=212326#respond)

18楼

牛逼！！两字奉上！！！

[亮了](javascript:acv_vote(212326,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

weejii

2017-03-16

[回复](?replytocom=212342#respond)

19楼

66666666

[亮了](javascript:acv_vote(212342,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

Xgrick

2017-03-16

[回复](?replytocom=212355#respond)

20楼

会玩[doge]

[亮了](javascript:acv_vote(212355,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/Scales)

[Scales ](http://www.freebuf.com/author/Scales)

(3级)
我这么复杂，一句话怎么能够概括呢~
2017-03-17

[回复](?replytocom=212410#respond)

21楼

真吊完全看不懂

[亮了](javascript:acv_vote(212410,1);)(1)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

大神带我飞

2017-03-17

[回复](?replytocom=212424#respond)

22楼

大神带我飞

[亮了](javascript:acv_vote(212424,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/遺丅の涙Ю)

[遺丅の涙Ю ](http://www.freebuf.com/author/遺丅の涙Ю)

(1级)
带着装不满的酱油瓶各种乱飘
2017-03-17

[回复](?replytocom=212425#respond)

23楼

五体投地

[亮了](javascript:acv_vote(212425,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

Astrong

2017-03-17

[回复](?replytocom=212427#respond)

24楼

给你32个膝盖

[亮了](javascript:acv_vote(212427,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

Astrong

2017-03-17

[回复](?replytocom=212428#respond)

25楼

@mengxp 是西邮的mengxp？

[亮了](javascript:acv_vote(212428,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/mengxp)

[mengxp ](http://www.freebuf.com/author/mengxp)

(1级)

2017-03-17

[回复](?replytocom=212447#respond)

@ Astrong 嗯，是，哈哈

[亮了](javascript:acv_vote(212447,1);)(1)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/夜尽天明)

[夜尽天明 ](http://www.freebuf.com/author/夜尽天明)

(6级)
千秋邈矣独留我，百战归来再读书
2017-03-17

[回复](?replytocom=212440#respond)

26楼

虽然完全看不懂你在写什么，但是认证的看完了

[亮了](javascript:acv_vote(212440,1);)(1)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/jimoyong)

[jimoyong ](http://www.freebuf.com/author/jimoyong)

(4级)

2017-03-17

[回复](?replytocom=212442#respond)

27楼

orz

[亮了](javascript:acv_vote(212442,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/吞龙)

[吞龙 ](http://www.freebuf.com/author/吞龙)

(1级)

2017-03-17

[回复](?replytocom=212460#respond)

28楼

DATASHEET是个好地方，大部分标准的器件芯片都可以查询到相关参数和典型使用，可以减少电路分析的难度。

[亮了](javascript:acv_vote(212460,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/Danielzero)

[Danielzero ](http://www.freebuf.com/author/Danielzero)

(1级)

2017-03-17

[回复](?replytocom=212488#respond)

29楼

我现在用的187B的机子，但是有个比较蛋疼的地方，就是每次唤醒Siri语音控制之后，车子都会用最大开机音量（设置里面有）播放音乐，还要手动关闭。楼主的有这个情况么

[亮了](javascript:acv_vote(212488,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/mengxp)

[mengxp ](http://www.freebuf.com/author/mengxp)

(1级)

2017-03-17

[回复](?replytocom=212495#respond)

@ Danielzero  我没把机器装车，只是研究。。你这个情况我没有研究……

[亮了](javascript:acv_vote(212495,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

good

2017-03-17

[回复](?replytocom=212521#respond)

30楼

真心佩服，比那个数字的什么*车实验室厉害多了，原来和他们聊过，狗屁不懂，给我们出的方案有些概念都是错的。以后还是要少看宣传，多些踏实的人。

[亮了](javascript:acv_vote(212521,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

gdsgds

2017-03-17

[回复](?replytocom=212525#respond)

31楼

求教大神！有办法绕开电视机的固件验证么？我的电视使用的linux，升级固件时会检测是否为亚太版的固件。一直没有思路，想想也是这10年买的就自带2.4G和5.8G的双频网卡了，肯定还有值得挖掘的地方。NX710

[亮了](javascript:acv_vote(212525,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/mengxp)

[mengxp ](http://www.freebuf.com/author/mengxp)

(1级)

2017-03-21

[回复](?replytocom=212910#respond)

@ gdsgds 对电视机没有研究，你可以把升级程序搞出来逆向啊~

[亮了](javascript:acv_vote(212910,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/Syne)

[Syne ](http://www.freebuf.com/author/Syne)

(2级)
做奥利奥的
2017-03-18

[回复](?replytocom=212599#respond)

32楼

老哥稳。身为SE，比不少EE内行还要专业。

[亮了](javascript:acv_vote(212599,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/mengxp)

[mengxp ](http://www.freebuf.com/author/mengxp)

(1级)

2017-03-21

[回复](?replytocom=212909#respond)

@ Syne  过奖了哈~

[亮了](javascript:acv_vote(212909,1);)(1)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/孤叶随风)

[孤叶随风 ](http://www.freebuf.com/author/孤叶随风)

(1级)

2017-03-20

[回复](?replytocom=212721#respond)

33楼

高手，软硬通吃膜拜

[亮了](javascript:acv_vote(212721,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/江南)

[江南 ](http://www.freebuf.com/author/江南)

(1级)

2017-03-22

[回复](?replytocom=212951#respond)

34楼

根据手册上的型号说明，MC-IMX6-S-6-A-VM-08-A-C是单Cortex-A9核。

[亮了](javascript:acv_vote(212951,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

first

2017-03-23

[回复](?replytocom=213048#respond)

35楼

牛人，来我们公司上班吧，教教那帮软件崽子怎么防破解，偷偷告诉你，天宝只是我们的工厂

[亮了](javascript:acv_vote(213048,1);)(5)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/mengxp)

[mengxp ](http://www.freebuf.com/author/mengxp)

(1级)

2017-03-23

[回复](?replytocom=213110#respond)

@ first 哦？？你们是哪家公司呀？

[亮了](javascript:acv_vote(213110,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/laurenthuang)

[laurenthuang ](http://www.freebuf.com/author/laurenthuang)

(1级)

2017-03-31

[回复](?replytocom=214343#respond)

@ mengxp  我知道

[亮了](javascript:acv_vote(214343,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

风暴之鳞

2017-03-25

[回复](?replytocom=213335#respond)

36楼

西邮的mengxp师兄，我传个5314的固件给你研究吧，我按你的方法查了下编译机器名也是HP-640，用户名是changchun，不过日期是9月份

[亮了](javascript:acv_vote(213335,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/少年已不年少)

[少年已不年少 ](http://www.freebuf.com/author/少年已不年少)

(1级)

2017-03-27

[回复](?replytocom=213501#respond)

37楼

用了你的替换LOGO刷机包，但是试了N次都没有成功。怎么破？

[亮了](javascript:acv_vote(213501,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

脚印

2017-03-27

[回复](?replytocom=213504#respond)

38楼

兄弟，我给你台导航的研究一下吧

[亮了](javascript:acv_vote(213504,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/laurenthuang)

[laurenthuang ](http://www.freebuf.com/author/laurenthuang)

(1级)

2017-03-31

[回复](?replytocom=214344#respond)

39楼

仁兄毅力惊人，要我发现JTAG fuse烧了，也就放弃了，佩服啊！！

[亮了](javascript:acv_vote(214344,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

lleheaven

2017-04-01

[回复](?replytocom=214399#respond)

40楼

大神有没有研究过mirrorlink，mirrorlink连接需要指定证书认证的软件才能开车中使用，是否可以替换它的根证书，然后自签名相关mirrorlink的软件让其都可以使用？

[亮了](javascript:acv_vote(214399,1);)(2)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/supportf)

[supportf ](http://www.freebuf.com/author/supportf)

(1级)

2017-04-10

[回复](?replytocom=215342#respond)

41楼

是真佩服

[亮了](javascript:acv_vote(215342,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/necroave)

[necroave ](http://www.freebuf.com/author/necroave)

(1级)

2017-04-11

[回复](?replytocom=215445#respond)

42楼

Can you help me unbrick MIB?

I wrote you full situation in a private message

[亮了](javascript:acv_vote(215445,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

smallwolf

2017-05-05

[回复](?replytocom=217648#respond)

43楼

楼主真是全能，看了您的文章收益颇多，只是我刷机的时候刷坏了，只能进uboot不知道能不能救活了，如果您方便的话能否发我一份编程器固件不行了我只能上编程器写固件了，谢谢了

[亮了](javascript:acv_vote(217648,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/沿途的向阳花，)

[沿途的向阳花， ](http://www.freebuf.com/author/沿途的向阳花，)

(1级)

2017-06-17

[回复](?replytocom=221555#respond)

44楼

先来膜拜下再说

[亮了](javascript:acv_vote(221555,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/dream801)

[dream801 ](http://www.freebuf.com/author/dream801)

(1级)

2017-06-25

[回复](?replytocom=222114#respond)

45楼

膜拜

[亮了](javascript:acv_vote(222114,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

Bigbig 象

2017-07-19

[回复](?replytocom=224340#respond)

46楼

楼主软硬都会，小生佩服的五体投地

[亮了](javascript:acv_vote(224340,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/大辣子)

[大辣子 ](http://www.freebuf.com/author/大辣子)

(1级)

2017-08-13

[回复](?replytocom=226930#respond)

47楼

真牛！膜拜大神！有机会研究一下宝马NBT EVO主机，谢谢！

[亮了](javascript:acv_vote(226930,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

unname

2017-10-18

[回复](?replytocom=232330#respond)

48楼

厉害了大神 膜拜

[亮了](javascript:acv_vote(232330,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

[](http://www.freebuf.com/author/DZP2017)

[DZP2017 ](http://www.freebuf.com/author/DZP2017)

(1级)

2017-11-12

[回复](?replytocom=234461#respond)

49楼

哥们 ，怎么联系你？

[亮了](javascript:acv_vote(234461,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

-

alexey

2017-12-12

[回复](?replytocom=237073#respond)

50楼

Hi, after the software update, there was a crash, and Linux does not boot up, cursing the lack of the rc.d / rcS file, there is no possibility of logging in to this system. Prompt to load U-boot in this situation?

[亮了](javascript:acv_vote(237073,1);)(0)

举报

- [广告等垃圾信息](javascript:;)

- [不友善内容](javascript:;)

- [违反法律法规的内容](javascript:;)

- [不宜公开讨论的政治内容](javascript:;)

- [其他](javascript:;)

1
[2](http://www.freebuf.com/geek/129160.html/comment-page-2#comments)
[下一页](http://www.freebuf.com/geek/129160.html/comment-page-2#comments)

昵称

必须您当前尚未登录。[登陆？](http://www.freebuf.com/oauth)[注册](https://account.tophant.com/register.html)

邮箱

必须（保密）

[表情](javascript:;)[插图](javascript:;)

[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()[](javascript:grin()

[取消](javascript:;)

有人回复时邮件通知我

Loading...
