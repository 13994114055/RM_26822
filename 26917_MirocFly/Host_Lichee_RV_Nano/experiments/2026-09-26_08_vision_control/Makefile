# MirocFly 实验 08: 绿色识别并飞向目标 (检测+MSP 闭环)
ROOT := $(abspath $(CURDIR)/../..)
SCP  := $(ROOT)/LicheeSG-Nano-Build_scpcom
OFF  := $(ROOT)/LicheeRV-Nano-Build_official
TC   := $(OFF)/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CC   := $(TC)gcc
CXX  := $(TC)g++

MW       := $(SCP)/middleware
TESTMMF  := $(MW)/sample/test_mmf
MAIXMMF  := $(TESTMMF)/maix_mmf
COMMON   := $(MW)/sample/common
KINC     := $(OFF)/linux_5.10/build/sg2002_licheervnano_sd/riscv/usr/include
BOARD    := $(ROOT)/board_libs
CV_DIR   := $(ROOT)/opencv-mobile-5.0.0-licheerv-nano
CV_INC   := $(CV_DIR)/include/opencv5
CV_LIB   := $(CV_DIR)/lib

ARCHFLAGS := -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d
DEFS      := -DARCH_CV181X -D__CV181X__ -D__SOC_MARS__ -DF10

INCS := -I. -I$(CV_INC) -I$(MAIXMMF) -I$(TESTMMF) \
        -I$(MW)/include -I$(MW)/modules/isp/include/cv181x -I$(MW)/modules/ive/include \
        -I$(MW)/component/isp/common -I$(COMMON) \
        -I$(SCP)/osdrv/interdrv/include/common/uapi \
        -I$(SCP)/osdrv/interdrv/include/chip/cv181x/uapi \
        -I$(KINC)

CFLAGS   := $(ARCHFLAGS) -Os -Wall $(DEFS) $(INCS)
CXXFLAGS := $(ARCHFLAGS) -Os -Wall $(DEFS) $(INCS) -std=gnu++14

TARGET    := green_fly
MW_SRC    := $(MAIXMMF)/sophgo_middleware.c
OBJS      := green_fly.o camera.o sophgo_middleware.o msp.o

CV_LIBS    := -L$(CV_LIB) -lopencv_geometry -lopencv_imgproc -lopencv_features -lopencv_core
BOARD_LIBS := -L$(BOARD) -lsample -lsys -lvi -lvpss -lvenc -lvdec -lmisc -lrgn -lvo -lgdc \
              -lcvi_bin -lcvi_bin_isp -lisp -lisp_algo -lae -laf -lawb -lsns_full -lcvi_ive -lini
TAIL_LIBS  := -latomic -lpthread -lm

.PHONY: all clean check
all: $(TARGET)

check:
	@test -d $(BOARD) || { echo "缺少 board_libs, 先跑 ../../fetch_board_libs.sh"; exit 1; }
	@test -d $(CV_LIB) || { echo "缺少 opencv-mobile"; exit 1; }

$(TARGET): check $(OBJS)
	$(CXX) -o $@ $(OBJS) -Wl,--no-as-needed -Wl,--start-group $(CV_LIBS) $(BOARD_LIBS) -Wl,--end-group \
		-Wl,-rpath-link,$(BOARD) -Wl,--allow-shlib-undefined $(TAIL_LIBS)

green_fly.o: green_fly.cpp camera.h msp.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<
camera.o: camera.c camera.h sophgo_middleware.h
	$(CC) $(CFLAGS) -c -o $@ $<
msp.o: msp.c msp.h
	$(CC) $(CFLAGS) -c -o $@ $<
sophgo_middleware.o: $(MW_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS) *.bmp
