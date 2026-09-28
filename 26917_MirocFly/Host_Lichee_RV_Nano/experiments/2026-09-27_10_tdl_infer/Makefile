# MirocFly 实验10: TDL 推理层 (相机检测 + 画框 + RTSP); 自包含
ROOT := $(abspath $(CURDIR)/../..)
SCP  := $(ROOT)/LicheeSG-Nano-Build_scpcom
OFF  := $(ROOT)/LicheeRV-Nano-Build_official
TC   := $(OFF)/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CC   := $(TC)gcc
CXX  := $(TC)g++
BOARD := $(ROOT)/board_libs

TP   := $(CURDIR)/thirdparty
TDL  := $(CURDIR)/libs/tdl_libs
NPU  := $(CURDIR)/libs/npu_libs

ARCHFLAGS := -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d
DEFS      := -DARCH_CV181X -D__CV181X__ -D__SOC_MARS__

INCS := -Iinclude \
        -I$(TP) -I$(TP)/include_tdl -I$(TP)/include_tdl/c_apis -I$(TP)/include_tdl/framework \
        -I$(TP)/include_tdl/components -I$(TP)/include_tdl/nn \
        -I$(SCP)/tdl_sdk/src/c_apis/include \
        -I$(ROOT)/opencv-mobile-5.0.0-licheerv-nano/include/opencv5 \
        -I$(SCP)/middleware/include \
        -I$(SCP)/osdrv/interdrv/include/common/uapi \
        -I$(SCP)/osdrv/interdrv/include/chip/cv181x/uapi

CFLAGS   := $(ARCHFLAGS) -Os -Wall $(DEFS) $(INCS) -ffunction-sections -fdata-sections
CXXFLAGS := $(CFLAGS) -std=gnu++14

TARGET := tdl_app
OBJS   := apps/tdl_app.o src/mf_tdl.o thirdparty/sample_utils.o

BOARD_LIBS := -L$(BOARD) -lsample -lsys -lvi -lvpss -lvenc -lvdec -lmisc -lrgn -lvo -lgdc \
              -lcvi_bin -lcvi_bin_isp -lisp -lisp_algo -lae -laf -lawb -lsns_full -lcvi_ive -lini

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) \
		-L$(TDL) -ltdl_core -ltdl_utils \
		-Wl,--no-as-needed -Wl,--start-group $(BOARD_LIBS) -Wl,--end-group \
		-L$(NPU) -lcviruntime -lcvikernel -lcvimath -lcvi_rtsp \
		-Wl,-rpath-link,$(TDL) -Wl,-rpath-link,$(NPU) -Wl,-rpath-link,$(BOARD) \
		-Wl,--gc-sections -Wl,--allow-shlib-undefined -lgcc_s -latomic -lpthread -lm

apps/tdl_app.o: apps/tdl_app.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
src/mf_tdl.o: src/mf_tdl.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
thirdparty/sample_utils.o: thirdparty/sample_utils.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
thirdparty/meta_visualize.o: thirdparty/meta_visualize.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) apps/*.o src/*.o thirdparty/*.o
