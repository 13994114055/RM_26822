# MirocFly 实验09 步骤1: 底层 cviruntime NPU demo
ROOT := $(abspath $(CURDIR)/../..)
OFF  := $(ROOT)/LicheeRV-Nano-Build_official
TC   := $(OFF)/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CC   := $(TC)gcc
CXX  := $(TC)g++
SCP  := $(ROOT)/LicheeSG-Nano-Build_scpcom

ARCHFLAGS := -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d
CFLAGS    := $(ARCHFLAGS) -Os -Wall -Iinclude

TARGET := npu_hello
NPU    := $(CURDIR)/npu_libs
TDL    := $(CURDIR)/tdl_libs

.PHONY: all clean

all: $(TARGET) tdl_detect vi_detect

$(TARGET): npu_hello.c
	$(CC) $(CFLAGS) -o $@ $< -L$(NPU) -lcviruntime -lcvikernel -lcvimath \
		-Wl,-rpath-link,$(NPU) -lpthread -lm

# 步骤2: 链接板上 TDL 运行时(用 scpcom 头)
# TDL 会用到板上中间件符号(CVI_SYS_/VPSS_/...), musl 不会自动加载 -> 显式链接 board_libs
BOARD := $(ROOT)/board_libs
BOARD_LIBS := -L$(BOARD) -lsample -lsys -lvi -lvpss -lvenc -lvdec -lmisc -lrgn -lvo -lgdc \
              -lcvi_bin -lcvi_bin_isp -lisp -lisp_algo -lae -laf -lawb -lsns_full -lcvi_ive -lini

tdl_detect: tdl_detect.c
	$(CC) $(ARCHFLAGS) -Os -Wall -Iinclude_tdl -Iinclude_tdl/c_apis -o $@ $< \
		-L$(TDL) -ltdl_core -ltdl_utils \
		-Wl,--no-as-needed -Wl,--start-group $(BOARD_LIBS) -Wl,--end-group \
		-L$(NPU) -lcviruntime -lcvikernel -lcvimath \
		-Wl,-rpath-link,$(TDL) -Wl,-rpath-link,$(NPU) -Wl,-rpath-link,$(BOARD) \
		-Wl,--allow-shlib-undefined -lgcc_s -latomic -lpthread -lm

# 步骤3: 相机实时检测 (TDL 内部 VI 取帧 + TDL_Detection); C+C++ 混编
TDL_INCS := -Iinclude_tdl -Iinclude_tdl/c_apis -Iinclude_tdl/framework -Iinclude_tdl/components -Iinclude_tdl/nn \
            -Ithirdparty -I$(ROOT)/opencv-mobile-5.0.0-licheerv-nano/include/opencv5 -I$(SCP)/tdl_sdk/src/c_apis/include -Isample_utils \
            -I$(SCP)/middleware/include \
            -I$(SCP)/osdrv/interdrv/include/common/uapi \
            -I$(SCP)/osdrv/interdrv/include/chip/cv181x/uapi

vi_detect: vi_detect.o sample_utils/sample_utils.o
	$(CXX) -o $@ vi_detect.o sample_utils/sample_utils.o \
		-L$(TDL) -ltdl_core -ltdl_utils \
		-Wl,--no-as-needed -Wl,--start-group $(BOARD_LIBS) -Wl,--end-group \
		-L$(NPU) -lcviruntime -lcvikernel -lcvimath \
		-Wl,-rpath-link,$(TDL) -Wl,-rpath-link,$(NPU) -Wl,-rpath-link,$(BOARD) \
		-Wl,--gc-sections -Wl,--allow-shlib-undefined -lgcc_s -latomic -lpthread -lm

vi_detect.o: vi_detect.c
	$(CC) $(ARCHFLAGS) -Os -Wall -DARCH_CV181X -D__CV181X__ -D__SOC_MARS__ $(TDL_INCS) -ffunction-sections -fdata-sections -c -o $@ $<

sample_utils/sample_utils.o: sample_utils/sample_utils.cpp
	$(CXX) $(ARCHFLAGS) -Os -Wall -DARCH_CV181X -D__CV181X__ -D__SOC_MARS__ $(TDL_INCS) -std=gnu++14 -ffunction-sections -fdata-sections -c -o $@ $<

clean:
	rm -f $(TARGET) tdl_detect vi_detect *.o sample_utils/*.o
