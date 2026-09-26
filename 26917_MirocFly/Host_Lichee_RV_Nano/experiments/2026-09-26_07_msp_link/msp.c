/* msp.c - MirocFly MSP v1 编解码 + 串口 + 视觉->RC 映射 */
#include "msp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>

uint8_t msp_crc8_dvb_s2(uint8_t crc, uint8_t b)
{
	crc ^= b;
	for (int i = 0; i < 8; i++)
		crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
	return crc;
}

int msp_v1_encode(uint8_t dir, uint8_t cmd, const uint8_t *payload, uint8_t len, uint8_t *out)
{
	out[0] = '$';
	out[1] = 'M';
	out[2] = dir;
	out[3] = len;
	out[4] = cmd;
	uint8_t csum = len ^ cmd;
	for (uint8_t i = 0; i < len; i++) {
		out[5 + i] = payload[i];
		csum ^= payload[i];
	}
	out[5 + len] = csum;
	return 6 + len;
}

static int write_all(int fd, const uint8_t *buf, int len)
{
	int off = 0;
	while (off < len) {
		int n = (int)write(fd, buf + off, len - off);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		off += n;
	}
	return off;
}

int msp_send_v1(int fd, uint8_t cmd, const uint8_t *payload, uint8_t len)
{
	uint8_t buf[6 + MSP_MAX_PAYLOAD];
	int n = msp_v1_encode(MSP_DIR_TO_FC, cmd, payload, len, buf);
	return write_all(fd, buf, n);
}

int msp_send_set_raw_rc(int fd, const uint16_t *chans, int nchan)
{
	if (nchan < 0) nchan = 0;
	if (nchan > 16) nchan = 16;
	uint8_t p[32];
	for (int i = 0; i < nchan; i++) {
		p[i * 2] = chans[i] & 0xFF;
		p[i * 2 + 1] = (chans[i] >> 8) & 0xFF;
	}
	return msp_send_v1(fd, MSP_SET_RAW_RC, p, (uint8_t)(nchan * 2));
}

int msp_send_request(int fd, uint8_t cmd)
{
	return msp_send_v1(fd, cmd, NULL, 0);
}

void msp_parser_init(msp_parser_t *p)
{
	memset(p, 0, sizeof(*p));
}

int msp_parser_feed(msp_parser_t *p, uint8_t b,
                    uint8_t *out_dir, uint8_t *out_cmd,
                    uint8_t *out_payload, uint8_t *out_len)
{
	switch (p->state) {
	case 0: if (b == '$') p->state = 1; break;
	case 1: p->state = (b == 'M') ? 2 : 0; break;
	case 2: p->dir = b; p->state = 3; break;
	case 3:
		p->size = b; p->csum = b; p->state = 4; break;
	case 4:
		p->cmd = b; p->csum ^= b; p->idx = 0;
		p->state = (p->size > 0) ? 5 : 6;
		break;
	case 5:
		p->data[p->idx++] = b;
		p->csum ^= b;
		if (p->idx >= p->size) p->state = 6;
		break;
	case 6:
		p->state = 0;
		if (b == p->csum) {
			if (out_dir) *out_dir = p->dir;
			if (out_cmd) *out_cmd = p->cmd;
			if (out_len) *out_len = p->size;
			if (out_payload && p->size) memcpy(out_payload, p->data, p->size);
			return 1;
		}
		break;
	}
	return 0;
}

int msp_serial_open(const char *dev, int baud)
{
	int fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0)
		return -1;
	struct termios tio;
	if (tcgetattr(fd, &tio) != 0) { close(fd); return -1; }
	cfmakeraw(&tio);
	tio.c_cflag |= (CLOCAL | CREAD);
	tio.c_cflag &= ~CRTSCTS;
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 1; /* 100ms */
	if (cfsetispeed(&tio, baud) != 0 || cfsetospeed(&tio, baud) != 0) {
		close(fd);
		return -2;
	}
	if (tcsetattr(fd, TCSANOW, &tio) != 0) { close(fd); return -1; }
	tcflush(fd, TCIOFLUSH);
	return fd;
}

void msp_serial_close(int fd)
{
	if (fd >= 0)
		close(fd);
}

static uint16_t clamp_rc(int v)
{
	if (v < 1000) v = 1000;
	if (v > 2000) v = 2000;
	return (uint16_t)v;
}

void msp_offset_to_rc(int offsetX, int offsetY, int w, int h,
                      double kp, int max_delta, uint16_t *rc)
{
	(void)w; (void)h;
	int dx = (int)(kp * offsetX);
	int dy = (int)(kp * offsetY);
	if (dx > max_delta) dx = max_delta;
	if (dx < -max_delta) dx = -max_delta;
	if (dy > max_delta) dy = max_delta;
	if (dy < -max_delta) dy = -max_delta;
	/* AETR: roll=右, pitch=前。目标偏右 -> roll 右; 目标偏上(offsetY<0) -> pitch 前。
	 * 具体 pitch 正负与 INAV 约定需实机确认, 此处给默认。 */
	rc[0] = clamp_rc(1500 + dx);
	rc[1] = clamp_rc(1500 - dy);
	rc[2] = 1500;
	rc[3] = 1500;
}
