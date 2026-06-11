**AArch64（ARM64）架构下Linux内核的链接脚本，核心作用是定义内核镜像的内存布局、段（Section）划分、符号导出与内存断言检查，是内核编译链接阶段的关键文件。以下从核心维度拆解说明：

### 一、文件核心定位
链接脚本（Linker Script）决定了编译器（ld）如何将内核的各个目标文件（.o）合并为最终的`vmlinux`内核镜像，包括：
- 内存地址分配（如代码段、数据段、BSS段的起始/结束地址）；
- 段的对齐、加载地址与运行地址映射；
- 符号（Symbol）的导出与校验；
- 内存布局的合法性断言（ASSERT）。

### 二、核心模块解析
#### 1. 基础配置与架构声明
```
OUTPUT_ARCH(aarch64)  // 声明目标架构为AArch64
ENTRY(_text)          // 内核入口符号：_text（内核代码段起始）
jiffies = jiffies_64; // 内核时钟节拍符号映射
PECOFF_FILE_ALIGNMENT = 0x200; // PE/COFF格式的文件对齐（适配EFI等场景）
```

#### 2. 段（SECTIONS）核心划分
链接脚本的核心是`SECTIONS`块，定义了内核镜像的内存布局，关键段如下：

| 段名                | 作用                                                                 |
|---------------------|----------------------------------------------------------------------|
| `/DISCARD/`         | 丢弃无用段（如动态链接、调试无关段），减小内核体积                   |
| `.head.text`        | 内核头部代码段（启动入口、异常向量表等核心启动代码）                 |
| `.text`             | 内核核心代码段（按功能细分：中断、软中断、调度、锁、KProbe等）       |
| `.rodata`           | 只读数据段（调度类、跳转表、追踪点等只读数据）                       |
| `.init.text/.init.data` | 初始化段（内核启动时的初始化代码/数据，启动完成后可释放）         |
| `.data`             | 可读写数据段（全局变量、线程栈、缓存对齐数据等）                     |
| `.bss`              | 未初始化数据段（全局未初始化变量，启动时清零）                       |
| `.hyp.*`            | HYP（Hypervisor）模式段（KVM虚拟化相关的代码/数据段）                |
| `.percpu`           | 每CPU数据段（多核架构下每个CPU独立的私有数据）                       |

##### 关键细节：
- **地址对齐**：大量使用`ALIGN(1 << 12)`（即4KB对齐），适配ARM64的MMU页大小；
- **HYP模式专属段**：`.hyp.text`/`.hyp.rodata`等是AArch64虚拟化扩展的核心，用于KVM的Hypervisor代码；
- **初始化段（.init）**：启动完成后会被释放，节省内存；
- **DISCARD段**：过滤掉动态链接（.dynsym/.dynstr）、调试（.note.GNU-stack）等非必需段，仅保留内核运行核心。

#### 3. 符号导出（PROVIDE）
通过`PROVIDE`导出核心符号，供内核启动代码、虚拟化模块（KVM）、EFI等场景使用，例如：
```
PROVIDE(__efistub_primary_entry = primary_entry); // EFI启动入口
PROVIDE(__pi_swapper_pg_dir = swapper_pg_dir);    // 页目录符号（启动阶段使用）
PROVIDE(__kvm_nvhe___hyp_text_start = __hyp_text_start); // KVM HYP段起始
```
同时通过`ASSERT`校验符号地址合法性，例如：
```
ASSERT(_text == ((((-(((1)) << (((48)) - 1)))) + (0x80000000))), "HEAD is misaligned")
// 校验内核代码段起始地址是否符合48位虚拟地址的对齐要求
```

#### 4. KVM虚拟化专属定义
针对AArch64 KVM（NVHE模式，Non-VHE Hypervisor），导出了大量虚拟化相关符号：
- `__kvm_nvhe_kvm_patch_vector_branch`：KVM向量分支补丁；
- `__kvm_nvhe_hyp_bss_start`：HYP模式BSS段起始；
- `__kvm_nvhe___kvm_ex_table`：KVM异常表；
- 同时通过`ASSERT`限制HYP代码段大小（如`__hyp_idmap_text_end - __hyp_idmap_text_start <= 4KB`），确保符合虚拟化模式的内存限制。

#### 5. 内存断言（ASSERT）
通过`ASSERT`校验内存布局的合法性，避免编译/运行错误：
- 校验HYP/IDMAP代码段大小（如HYP初始化代码不超过4KB）；
- 校验页目录偏移（如swapper_pg_dir与reserved_pg_dir的偏移）；
- 校验内核入口地址对齐（_text符合48位虚拟地址要求）；
- 校验BSS符号不被导出到启动代码（避免未初始化数据被误用）。

### 三、关键设计背景
1. **AArch64 48位虚拟地址**：代码中大量出现`48`相关的位运算，是因为ARM64v8架构默认支持48位虚拟地址（`(1 << 48)`），内核需适配该地址空间；
2. **虚拟化（HYP）**：AArch64的EL2（HYP）模式是KVM的核心运行级别，`.hyp.*`段专门为该模式划分，确保虚拟化代码与内核核心代码隔离；
3. **EFI适配**：`PECOFF_FILE_ALIGNMENT`、`__efistub_*`符号适配EFI固件启动流程；
4. **内存效率**：初始化段（.init）释放、DISCARD无用段，最大化运行时内存利用率。

### 四、总结
该文件是AArch64 Linux内核的“内存布局蓝图”：
- 定义了内核从启动到运行的全内存布局；
- 适配ARM64架构特性（48位虚拟地址、HYP虚拟化、per-CPU数据）；
- 通过严格的断言确保内存布局合法，避免运行时崩溃；
- 为KVM、EFI、内核启动等核心场景导出关键符号，是内核编译的核心依赖。

