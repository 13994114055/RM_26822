# MirocFly 相机层 (Form A) —— 交叉编译
# 编译 scpcom 源码树的 sophgo_middleware.c + 本模块, 链接板上 scpcom 库。
# 共享库在 ../../board_libs (用 ../../fetch_board_libs.sh 拉取)。

ROOT     := $(abspath $(CURDIR)/../..)
SCP      := $(ROOT)/LicheeSG-Nano-Build_scpcom
OFF      := $(ROOT)/LicheeRV-Nano-Build_official

TC       := $(OFF)/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
CC       := $(TC)gcc
CXX      := $(TC)g++

MW       := $(SCP)/middleware
COMMON   := $(MW)/sample/common
MAIXMMF  := $(MW)/sample/test_mmf/maix_mmf
TESTMMF  := $(MW)/sample/test_mmf
KINC     := $(OFF)/linux_5.10/build/sg2002_licheervnano_sd/riscv/usr/include
BOARD    := $(ROOT)/board_libs

ARCHFLAGS := -mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d
DEFS      := -DARCH_CV181X -D__CV181X__ -D__SOC_MARS__ -DF10
INCS      := -I. -I$(MAIXMMF) -I$(TESTMMF) \
             -I$(MW)/include -I$(MW)/modules/isp/include/cv181x -I$(MW)/modules/ive/include \
             -I$(MW)/component/isp/common -I$(COMMON) \
             -I$(SCP)/osdrv/interdrv/include/common/uapi \
             -I$(SCP)/osdrv/interdrv/include/chip/cv181x/uapi \
             -I$(KINC)
CFLAGS    := $(ARCHFLAGS) -Os -Wall $(DEFS) $(INCS)

TARGET    := frame_grab
MW_SRC    := $(MAIXMMF)/sophgo_middleware.c
OBJS      := frame_grab.o camera.o sophgo_middleware.o

# 板上 scpcom 运行库 (librgn/libvo 供中间件用, libgdc 必须显式带: musl 无惰性绑定)
BOARD_LIBS := -L$(BOARD) -lsample \
              -lsys -lvi -lvpss -lvenc -lvdec -lmisc -lrgn -lvo -lgdc \
              -lcvi_bin -lcvi_bin_isp -lisp -lisp_algo -lae -laf -lawb \
              -lsns_full -lcvi_ive -lini
TAIL_LIBS := -latomic -lpthread -lm

.PHONY: all clean check-libs

all: $(TARGET)

check-libs:
	@test -d $(BOARD) || { echo "缺少 $(BOARD), 先跑 ../../fetch_board_libs.sh"; exit 1; }

$(TARGET): check-libs $(OBJS)
	$(CXX) -o $@ $(OBJS) -Wl,--no-as-needed -Wl,--start-group $(BOARD_LIBS) -Wl,--end-group \
		-Wl,-rpath-link,$(BOARD) -Wl,--allow-shlib-undefined $(TAIL_LIBS)

frame_grab.o: frame_grab.c camera.h
	$(CC) $(CFLAGS) -c -o $@ $<

camera.o: camera.c camera.h sophgo_middleware.h
	$(CC) $(CFLAGS) -c -o $@ $<

sophgo_middleware.o: $(MW_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
