H   := ../..
E   := ../2026-09-27_10_tdl_infer
SCP := $(H)/LicheeSG-Nano-Build_scpcom
OFF := $(H)/LicheeRV-Nano-Build_official
BOARD := $(H)/board_libs
TC  := $(OFF)/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CXX := $(TC)g++
TDL := $(E)/libs/tdl_libs
NPU := $(E)/libs/npu_libs
ARCHFLAGS := -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d
DEFS := -DARCH_CV181X -D__CV181X__ -D__SOC_MARS__
INCS := -I. -I$(H)/opencv-mobile-5.0.0-licheerv-nano/include/opencv5 -I$(E)/thirdparty -I$(E)/thirdparty/include_tdl -I$(E)/thirdparty/include_tdl/c_apis \
        -I$(E)/thirdparty/include_tdl/framework -I$(E)/thirdparty/include_tdl/components -I$(E)/thirdparty/include_tdl/nn \
        -I$(SCP)/tdl_sdk/src/c_apis/include -I$(SCP)/middleware/include \
        -I$(SCP)/osdrv/interdrv/include/common/uapi -I$(SCP)/osdrv/interdrv/include/chip/cv181x/uapi
CXXFLAGS := $(ARCHFLAGS) -Os -Wall -Wno-unused-function $(DEFS) $(INCS) -std=gnu++14
BOARD_LIBS := -L$(BOARD) -lsample -lsys -lvi -lvpss -lvenc -lvdec -lmisc -lrgn -lvo -lgdc \
              -lcvi_bin -lcvi_bin_isp -lisp -lisp_algo -lae -laf -lawb -lsns_full -lcvi_ive -lini
LDFLAGS := -L$(TDL) -ltdl_core -ltdl_utils -Wl,--no-as-needed -Wl,--start-group $(BOARD_LIBS) -Wl,--end-group \
           -L$(NPU) -lcviruntime -lcvikernel -lcvimath -lcvi_rtsp \
           -Wl,-rpath-link,$(TDL) -Wl,-rpath-link,$(NPU) -Wl,-rpath-link,$(BOARD) \
           -Wl,--gc-sections -Wl,--allow-shlib-undefined -lgcc_s -latomic -lpthread -lm
all: mirovision
mirovision: mirovision.cpp $(E)/thirdparty/sample_utils.cpp
	$(CXX) $(CXXFLAGS) -o $@ mirovision.cpp $(E)/thirdparty/sample_utils.cpp $(LDFLAGS)
clean:
	rm -f mirovision
.PHONY: all clean
