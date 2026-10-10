# MirocFly 实验 15: 闭环可视化叠加 (VI->检测/画框->VENC(H265)->RTSP) + MSP 闭环
# 复用 scpcom: sophgo_middleware.c + rtsp_server/ + media_server 静态库; 另加 opencv-mobile + msp

ROOT := $(abspath $(CURDIR)/../..)
SCP  := $(ROOT)/LicheeSG-Nano-Build_scpcom
OFF  := $(ROOT)/LicheeRV-Nano-Build_official

TC   := $(OFF)/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CC   := $(TC)gcc
CXX  := $(TC)g++

MW      := $(SCP)/middleware
TESTMMF := $(MW)/sample/test_mmf
MAIXMMF := $(TESTMMF)/maix_mmf
COMMON  := $(MW)/sample/common
KINC    := $(OFF)/linux_5.10/build/sg2002_licheervnano_sd/riscv/usr/include
BOARD   := $(ROOT)/board_libs
MSK     := $(TESTMMF)/media_server-1.0.x
RTSP_INC:= $(TESTMMF)/rtsp_server/inc
RTSP_SRC:= $(TESTMMF)/rtsp_server/src

CV_DIR  := $(ROOT)/opencv-mobile-5.0.0-licheerv-nano
CV_INC  := $(CV_DIR)/include/opencv5
CV_LIB  := $(CV_DIR)/lib

ARCHFLAGS := -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d
DEFS      := -DARCH_CV181X -D__CV181X__ -D__SOC_MARS__ -DF10 -D__ERROR__=00

MW_INCS := -I. -I$(CV_INC) -I$(MAIXMMF) -I$(TESTMMF) \
           -I$(MW)/include -I$(MW)/modules/isp/include/cv181x -I$(MW)/modules/ive/include \
           -I$(MW)/component/isp/common -I$(COMMON) \
           -I$(SCP)/osdrv/interdrv/include/common/uapi \
           -I$(SCP)/osdrv/interdrv/include/chip/cv181x/uapi \
           -I$(KINC)

RTSP_INCS := -I$(RTSP_INC) -I$(MSK)/include -I$(MSK) \
             -I$(MSK)/media-server/librtsp/include -I$(MSK)/media-server/librtsp/source/server \
             -I$(MSK)/sdk/include -I$(MSK)/sdk/libhttp/include \
             -I$(MSK)/media-server/librtp/include -I$(MSK)/media-server/libmpeg/include \
             -I$(MSK)/media-server/libflv/include -I$(MSK)/media-server/libmkv/include \
             -I$(MSK)/media-server/libmov/include \
             -I$(MSK)/avcodec/avcodec/include -I$(MSK)/avcodec/avbsf/include

CFLAGS   := $(ARCHFLAGS) -Os -Wall $(DEFS) $(MW_INCS) $(RTSP_INCS)
CXXFLAGS := $(ARCHFLAGS) -Os -Wall $(DEFS) $(MW_INCS) $(RTSP_INCS) -std=gnu++14

RTSP_CPP := $(wildcard $(RTSP_SRC)/*.cpp)
RTSP_OBJ := $(patsubst $(RTSP_SRC)/%.cpp,%.o,$(RTSP_CPP))

TARGET := loop_overlay
OBJS   := loop_overlay.o overlay_cv.o msp.o sophgo_middleware.o $(RTSP_OBJ)

CV_LIBS    := -L$(CV_LIB) -lopencv_geometry -lopencv_imgproc -lopencv_features -lopencv_core
MSK_LIBS   := -L$(MSK)/release.linux -lrtsp -lhttp -lflv -lmov -lrtp -lmpeg -lsdk -lavcodec -lavbsf
BOARD_LIBS := -L$(BOARD) -lsample -lsys -lvi -lvpss -lvenc -lvdec -lmisc -lrgn -lvo -lgdc \
              -lcvi_bin -lcvi_bin_isp -lisp -lisp_algo -lae -laf -lawb -lsns_full -lcvi_ive -lini
TAIL_LIBS  := -latomic -lpthread -lm

.PHONY: all clean check-libs

all: $(TARGET)

check-libs:
	@test -d $(BOARD) || { echo "缺少 $(BOARD), 先跑 ../../fetch_board_libs.sh"; exit 1; }
	@test -d $(MSK)/release.linux || { echo "缺少 media_server release.linux"; exit 1; }
	@test -d $(CV_LIB) || { echo "缺少 opencv-mobile"; exit 1; }

$(TARGET): check-libs $(OBJS)
	$(CXX) -o $@ $(OBJS) -Wl,--no-as-needed -Wl,--start-group $(CV_LIBS) $(MSK_LIBS) $(BOARD_LIBS) -Wl,--end-group \
		-Wl,-rpath-link,$(BOARD) -Wl,--allow-shlib-undefined $(TAIL_LIBS)

loop_overlay.o: loop_overlay.c overlay_api.h msp.h
	$(CC) $(CFLAGS) -c -o $@ $<
overlay_cv.o: overlay_cv.cpp overlay_api.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<
msp.o: msp.c msp.h
	$(CC) $(CFLAGS) -c -o $@ $<
sophgo_middleware.o: $(MAIXMMF)/sophgo_middleware.c
	$(CC) $(CFLAGS) -c -o $@ $<
$(RTSP_OBJ): %.o: $(RTSP_SRC)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
