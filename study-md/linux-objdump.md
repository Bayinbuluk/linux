# 一、完整反汇编 vmlinux 带全部调试/内置KVM EL2代码的全套流程
## 前置要求（必须开启，否则丢失符号、行号、源码映射）
### 1. 内核编译关键配置（.config）
```ini
# 核心调试符号
CONFIG_DEBUG_INFO=y
CONFIG_DEBUG_INFO_DWARF4=y          # DWARF4 完整调试信息
CONFIG_DEBUG_INFO_BTF=y
CONFIG_GDB_SCRIPTS=y
CONFIG_DEBUG_INFO_REDUCED=n        # 关闭精简调试，保留完整
CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=y              # 包含所有静态/内置函数符号

# KVM EL2 相关必须开启（ARM64）
CONFIG_KVM=y
CONFIG_KVM_ARM_HOST=y
CONFIG_KVM_ARM_VGIC=y
CONFIG_KVM_INDIRECT_VECTORS=y
CONFIG_ARM64_VHE=y                 # EL2 VHE 虚拟化，KVM EL2 核心代码
CONFIG_DEBUG_KVM=y                 # KVM 内部调试符号/日志

# 禁止剥离符号
CONFIG_STRIP_ASM_SYMS=n
CONFIG_BUILTIN_DTB_DEBUG=y
```
### 2. 编译内核带完整DWARF，不strip
```bash
make -j$(nproc) vmlinux CFLAGS_KERNEL="-g3 -gdwarf-4"
# -g3：最高等级调试，包含宏定义、局部变量、行号、源码路径
# -gdwarf-4：标准可被objdump/readelf识别
```

## 二、objdump 生成带源码、调试、内置模块、KVM EL2 的完整 list 文件
### 命令1：最全输出（源码+汇编+行号+符号+DWARF+段信息，推荐）
```bash
objdump -DzwSrlgF vmlinux > vmlinux_full_disasm.lst
```
参数逐行解释（每条对应你的需求）：
| 参数 | 作用 |
|------|------|
| `-D` | 反汇编**所有段**（.text/.init.text/.kvm.text/.rodata 包含KVM EL2内置代码，不跳过init/内置段） |
| `-z` | 显示零填充空白段，不截断KVM内置函数 |
| `-w` | 宽输出，完整长符号名（KVM函数名超长不会截断） |
| `-S` | 混入对应C源码（依赖-g调试信息，C/汇编一一对应） |
| `-r` | 打印重定位符号（内置built-in静态函数重定位项） |
| `-l` | 打印源码文件名+行号（精准定位KVM EL2源码行） |
| `-g` | 加载DWARF调试信息，输出局部变量、栈布局、类型信息 |
| `-F` | 显示每个函数所属ELF段（区分 .kvm.text EL2 虚拟化代码段） |

### 命令2：只导出 KVM + VHE EL2 相关代码（过滤精简版）
如果只需要ARM64 EL2 KVM，过滤段 `.kvm.text`、函数前缀`kvm_`、`vhe_`：
```bash
objdump -DzwSrlgF vmlinux | grep -A20 -B5 -E '\.kvm\.text|kvm_|vhe_|el2_' > vmlinux_kvm_el2_only.lst
```

### 命令3：区分内置built-in vs 可加载模块符号
内置代码（编译进vmlinux，非ko）全部在`.text/.init.text/.kvm.text`；
可通过`objdump -t vmlinux`过滤：
```bash
# 只看内置静态符号（无 [module] 标记）
objdump -t vmlinux | grep -v '\[module\]' > builtin_symbol.list
```

## 三、配套补充工具：readelf 导出段/符号辅助定位KVM EL2
### 1. 查看内核所有ELF段，确认KVM EL2段存在
```bash
readelf -S vmlinux > vmlinux_section.list
# 重点查找段：.kvm.text、.kvm_rodata、.vhe_entry
```
### 2. 导出完整全局符号表（包含所有built-in KVM函数）
```bash
readelf -Ws vmlinux > vmlinux_all_symbols.list
# 检索 kvm_vhe、kvm_el2_enter、__kvm_hyp_vector 等EL2入口
```

## 四、gdb 交互式查看（精准跳转KVM EL2调试信息）
```bash
gdb vmlinux
# 加载内核gdb脚本
source scripts/gdb/vmlinux-gdb.py
# 直接搜索所有EL2 VHE KVM函数
info functions kvm_vhe
disassemble kvm_el2_enter
# 查看DWARF局部变量调试信息
ptype struct kvm_vcpu
```

## 五、输出lst文件内容示例（KVM EL2 VHE 典型片段）
```
0xffff800010098000 <kvm_vhe_el2_enter>:
// arch/arm64/kvm/hyp/vhe.S:45
ffff800010098000:        d2800001        mov     x1, #0x0
ffff800010098004:        f9400002        ldr     x2, [x0]
ffff800010098008:        d69f0000        eret
```
特征：
1. 带源码文件+行号 `arch/arm64/kvm/hyp/vhe.S:45`
2. 完整虚拟地址、符号名、汇编指令
3. 区分 `.kvm.text` EL2 Hypervisor 段
4. 保留DWARF调试对应的源码映射

## 六、常见问题修复
### 问题1：反汇编无C源码、只有汇编
原因：未加`-g3`编译、CONFIG_DEBUG_INFO=n
修复：重新`make clean && make vmlinux CFLAGS_KERNEL="-g3 -gdwarf-4"`

### 问题2：找不到KVM EL2 / VHE 代码
1. 确认 `.config` 开启 `CONFIG_ARM64_VHE=y CONFIG_KVM_ARM_HOST=y`
2. objdump 必须用 `-D`（全部段），不要用 `-d`（仅普通.text，跳过.kvm.text）

### 问题3：lst文件过大（几十GB）优化方案
1. 拆分输出，分段导出：
```bash
# 仅导出 .kvm.text EL2 段
objdump -DzwSrlgF --section=.kvm.text vmlinux > kvm_el2_disasm.lst
# 导出普通内置内核代码
objdump -DzwSrlgF --section=.text vmlinux > kernel_builtin_disasm.lst
```
2. 压缩输出：`... | gzip > vmlinux_full.lst.gz`

## 七、一键整合脚本（直接复制使用）
```bash
#!/bin/bash
# 完整反汇编vmlinux，保留调试、源码、KVM EL2、全部内置代码
if [ ! -f ./vmlinux ]; then
    echo "Error: 无vmlinux文件"
    exit 1
fi

echo "生成全量反汇编文件 vmlinux_full_disasm.lst ..."
objdump -DzwSrlgF vmlinux > vmlinux_full_disasm.lst

echo "提取KVM VHE EL2 单独反汇编 vmlinux_kvm_el2.lst ..."
objdump -DzwSrlgF --section=.kvm.text vmlinux > vmlinux_kvm_el2.lst

echo "导出完整符号表"
readelf -Ws vmlinux > all_symbol_table.list
echo "完成"
```