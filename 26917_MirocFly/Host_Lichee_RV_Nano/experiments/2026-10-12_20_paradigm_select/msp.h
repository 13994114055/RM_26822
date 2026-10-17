#ifndef MIROCFLY_MSP_H
#define MIROCFLY_MSP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MSP v1 常用命令 */
#define MSP_STATUS          101
#define MSP_RAW_IMU         102
#define MSP_MOTOR           104   /* out: 8x u16 电机输出(1000-2000) */
#define MSP_RC              105   /* out: FC 实际在用通道(override 之后), 内部序 [roll pitch yaw throttle ...] */
#define MSP_ATTITUDE        108
#define MSP_ALTITUDE        109   /* out: u32 估计高度[cm] + u16 垂速[cm/s] + u32 baro[cm] (估计器 Z, 非测距原始值) */
#define MSP_ACTIVEBOXES     113   /* out: 全宽 box 位掩码(注意: 按"下发顺序索引"打包, 非 box 枚举号!) */
#define MSP_BOXIDS          119   /* out: 各 box 的永久ID, 顺序 = MSP box 索引(可据此定位 OVERRIDE=50) */
#define MSP_RX_MAP          64    /* out: 通道映射(注意旧 msp.h 误写成 105) */
#define MSP_SET_RAW_RC      200
/* MSP v1 扩展命令 */
#define MSP_SONAR_ALTITUDE  58    /* out: u32 测距高度 [cm] (MTF-02P) */
/* MSP v2 (16-bit cmd, 帧 $X<flags cmd size payload crc8_dvb_s2>) */
#define MSP2_INAV_STATUS                0x2000
#define MSP2_INAV_OPTICAL_FLOW          0x2001  /* out: quality(u8) flowRateX/Y(i16) bodyRateX/Y(i16) 度/s */
#define MSP2_INAV_ESTIMATED_POSITION    0x2012
#define MSP2_INAV_FULL_LOCAL_POSE       0x2220  /* out: rpy + pos/vel */

#define MSP_MAX_PAYLOAD 255

/* 字节方向 */
#define MSP_DIR_TO_FC    '<'
#define MSP_DIR_FROM_FC  '>'
#define MSP_DIR_ERROR    '!'

/* ---- 编码 (v1) ---- */
/* 返回整帧长度, out 至少 6+MSP_MAX_PAYLOAD 字节 */
int msp_v1_encode(uint8_t dir, uint8_t cmd, const uint8_t *payload, uint8_t len, uint8_t *out);

/* 写整帧 (串口或任意 fd), 内部循环写满 */
int msp_send_v1(int fd, uint8_t cmd, const uint8_t *payload, uint8_t len);

/* 发 SET_RAW_RC: chans 为 AETR... 值 1000-2000, nchan 个 */
int msp_send_set_raw_rc(int fd, const uint16_t *chans, int nchan);
/* 请求命令(空负载) */
int msp_send_request(int fd, uint8_t cmd);

/* ---- 流式解析 ---- */
typedef struct {
	int state;
	uint8_t dir;
	uint8_t size;
	uint8_t cmd;
	uint8_t data[MSP_MAX_PAYLOAD];
	int idx;
	uint8_t csum;
} msp_parser_t;

void msp_parser_init(msp_parser_t *p);
/* 喂入一个字节; 解析出一帧返回 1 (并写入 out_dir/out_cmd/out_payload/out_len), 否则 0 */
int msp_parser_feed(msp_parser_t *p, uint8_t b,
                    uint8_t *out_dir, uint8_t *out_cmd,
                    uint8_t *out_payload, uint8_t *out_len);

/* DVB-S2 CRC8 (MSP v2 用) */
uint8_t msp_crc8_dvb_s2(uint8_t crc, uint8_t b);

/* ---- MSP v2 ---- */
int msp2_send_request(int fd, uint16_t cmd);

/* 统一解析器: 同时吃 v1('$M') 与 v2('$X') 帧 */
typedef struct {
	int state;
	uint8_t ver;      /* 1 或 2 */
	uint8_t dir;
	uint8_t flags;
	uint16_t cmd;
	uint16_t size;
	uint16_t idx;
	uint8_t data[MSP_MAX_PAYLOAD];
	uint8_t csum1;
	uint8_t csum2;
} msp_parser2_t;

void msp_parser2_init(msp_parser2_t *p);
/* 解析出一帧返回 1 (写 out_dir/out_cmd/out_payload/out_len), 否则 0 */
int msp_parser2_feed(msp_parser2_t *p, uint8_t b,
                     uint8_t *out_dir, uint16_t *out_cmd,
                     uint8_t *out_payload, uint16_t *out_len);

/* ---- 串口 ---- */
int msp_serial_open(const char *dev, int baud); /* 返回 fd, <0 失败 */
void msp_serial_close(int fd);

/* ---- 视觉误差 -> RC 摇杆 (AETR) ----
 * offsetX/Y: 目标质心 - 画面中心 (像素, 正=右/下)
 * 返回放在 rc[0..3] (roll,pitch,throttle,yaw), 中位 1500。
 * kp: 每像素修正 (RC 单位), max_delta: 最大偏离 (如 300)
 */
void msp_offset_to_rc(int offsetX, int offsetY, int w, int h,
                      double kp, int max_delta, uint16_t *rc);

#ifdef __cplusplus
}
#endif

#endif
