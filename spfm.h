/* See LICENSE for licence details. */
enum fd_state_t {
	FD_IS_BUSY     = -1,
	FD_IS_READABLE = 0,
	FD_IS_WRITABLE = 1,
};

enum check_type_t {
	CHECK_READ_FD  = 0,
	CHECK_WRITE_FD = 1,
};

/* spfm functions */
int serial_init(struct termios *old_termio)
{
	int fd = -1;
	bool get_old_termio = false;
	struct termios cur_termio;

	if ((fd = eopen(serial_dev, O_RDWR | O_NOCTTY | O_NDELAY)) < 0
		|| etcgetattr(fd, old_termio) < 0)
		goto err;

	get_old_termio = true;
	cur_termio = *old_termio;

	/*
	 * SPFM light serial:
	 *
	 * baud        : 1500000
	 * data size   : 8bit
	 * parity      : none
	 * flow control: disable
	 */

	cur_termio.c_iflag = 0;
	cur_termio.c_oflag = 0;
	cur_termio.c_lflag = 0;
	cur_termio.c_cflag = CS8;

	cur_termio.c_cc[VMIN]  = 1;
	cur_termio.c_cc[VTIME] = 0;

	if (ecfsetispeed(&cur_termio, B1500000) < 0
		|| etcsetattr(fd, TCSAFLUSH, &cur_termio) < 0)
		goto err;

	return fd;

err:
	if (get_old_termio)
		etcsetattr(fd, TCSAFLUSH, old_termio);

	if (fd != -1)
		eclose(fd);

	return -1;
}

void serial_die(int fd, struct termios *old_termio)
{
	etcsetattr(fd, TCSAFLUSH, old_termio);
	eclose(fd);
}

/*
 * SPFM light protocol:
 * <client>                <light>
 *
 * check interface:
 *         --> 0xFF    -->
 *         <-- 'L' 'T' <--
 * reset:
 *         --> 0xFE    -->
 *         <-- 'O' 'K' <--
 * nop:
 *         --> 0x80    -->
 *         <-- (none)  <--
 *
 * send register data:
 *
 * 1st byte: module number (0x00 or 0x01)
 * 2nd byte: command byte  (0x0n, n: set A0-A3 bit)
 * 3rd byte: register address
 * 4th byte: register data
 *
 * command byte:
 *
 * bit 0: A0 bit
 * bit 1: A1 bit
 * bit 2: A2 bit
 * bit 3: A3 bit
 * bit 4: CS1 bit
 * bit 5: CS2 bit
 * bit 6: CS3 bit
 * bit 7: (on: check/reset/nop, off: other commands)
 *
 * send data:
 *
 * 1st byte: module number (0x00 or 0x01)
 * 2nd byte: command byte  (0x8n, n: set A0-A3 bit)
 * 3rd byte: data
 *
 * SN76489 send data:
 *
 * 1st byte: module number (0x00 or 0x01)
 * 2nd byte: command byte  (0x20)
 * 3rd byte: data
 * 4th byte: 0x00          (dummy)
 * 5th byte: 0x00          (dummy)
 * 6th byte: 0x00          (dummy)
 *
 * read data (not implemented):
 *
 * 1st byte: module number (0x00 or 0x01)
 * 2nd byte: command byte  (0x4n, n: set A0-A3 bit?)
 * 3rd byte: register address
 *
 */

enum fd_state_t check_fds(int fd, enum check_type_t type)
{
	struct timeval tv;
	fd_set rfds, wfds;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	FD_SET(fd, &rfds);
	FD_SET(fd, &wfds);

	tv.tv_sec  = 0;
	tv.tv_usec = SELECT_TIMEOUT;

	eselect(fd + 1, &rfds, &wfds, NULL, &tv);

	if (type == CHECK_READ_FD && FD_ISSET(fd, &rfds))
		return FD_IS_READABLE;
	else if (type == CHECK_WRITE_FD && FD_ISSET(fd, &wfds))
		return FD_IS_WRITABLE;
	else
		return FD_IS_BUSY;
}

void send_data(int fd, uint8_t *buf, int size)
{
	ssize_t wsize;
	
	while (check_fds(fd, CHECK_WRITE_FD) != FD_IS_WRITABLE);

	wsize = ewrite(fd, buf, size);

	/*
	if (LOG_LEVEL == DEBUG) {
		logging(DEBUG, "%ld byte(s) wrote\t", wsize);
		for (int i = 0; i < wsize; i++)
			fprintf(stderr, "0x%.2X ", buf[i]);
		fprintf(stderr, "\n");
	}
	*/
}

void recv_data(int fd, uint8_t *buf, int size)
{
	ssize_t rsize;

	while (check_fds(fd, CHECK_READ_FD) != FD_IS_READABLE);

	rsize = eread(fd, buf, size);

	/*
	if (LOG_LEVEL == DEBUG) {
		logging(DEBUG, "%ld byte(s) read\t", rsize);
		for (int i = 0; i < rsize; i++)
			fprintf(stderr, "0x%.2X ('%c') ", buf[i], buf[i]);
		fprintf(stderr, "\n");
	}
	*/
}

bool spfm_reset(int fd)
{
	uint8_t buf[BUFSIZE];

	send_data(fd, &(uint8_t){0xFF}, 1);
	recv_data(fd, buf, BUFSIZE);

	if (strncmp((char *) buf, "LT", 2) != 0)
		return false;

	send_data(fd, &(uint8_t){0xFE}, 1);
	recv_data(fd, buf, BUFSIZE);

	if (strncmp((char *) buf, "OK", 2) != 0)
		return false;

	return true;
}

void OPNA_register_info(uint8_t port, uint8_t addr)
{
	if (port == 0x00) {
		if (addr <= 0x0F)
			logging(DEBUG, "SSG:\n");
		else if (0x10 <= addr && addr <= 0x1F)
			logging(DEBUG, "RHYTHM:\n");
		else if (0x20 <= addr && addr <= 0x2F)
			logging(DEBUG, "FM COMMON:\n");
		else if (0x30 <= addr && addr <= 0xB6)
			logging(DEBUG, "FM (1-3ch):\n");
		else
			logging(DEBUG, "unknown:\n");
	} else {
		if (addr <= 0x10)
			logging(DEBUG, "ADPCM:\n");
		else if (0x30 <= addr && addr <= 0xB6)
			logging(DEBUG, "FM (4-6ch):\n");
		else
			logging(DEBUG, "unknown:\n");
	}
}

void spfm_send(int fd, uint8_t slot, uint8_t port, uint8_t addr, uint8_t data)
{
	uint8_t buf[BUFSIZE];
	uint8_t* p = &buf[0];
	const int wait_count = 13;

	*p++ = 0x00 | (slot & 0xF);
	*p++ = 0x00 | ((port & 0x1) << 1);
	*p++ = addr;
	*p++ = data;
	for (int i = 0; i < wait_count; ++i) {
		*p++ = 0x80;
	}
	send_data(fd, buf, 4 + wait_count);

	logging(DEBUG, "slot:0x%.2X port:0x%.2X addr:0x%.2X data:0x%.2X\n",
		slot, port, addr, data);
}

void OPNA_reset(int fd, uint8_t slot)
{
	spfm_send(fd, slot, 0, 0x29, 0x80); // OPNA mode.
	spfm_send(fd, slot, 0, 0xB4, 0xC0);
	spfm_send(fd, slot, 0, 0xB5, 0xC0);
	spfm_send(fd, slot, 0, 0xB6, 0xC0);
	spfm_send(fd, slot, 1, 0xB4, 0xC0);
	spfm_send(fd, slot, 1, 0xB5, 0xC0);
	spfm_send(fd, slot, 1, 0xB6, 0xC0);
	for (uint8_t i = 0x40; i <= 0x4E; ++i) {
		spfm_send(fd, slot, 0, i, 0x7F);
		spfm_send(fd, slot, 1, i, 0x7F);
	}
	for (uint8_t i = 0x80; i <= 0x8E; ++i) {
		spfm_send(fd, slot, 0, i, 0x0F);
		spfm_send(fd, slot, 1, i, 0x0F);
	}
	for (uint8_t i = 0; i < 6; i++) {
		spfm_send(fd, slot, 0, 0x28, i);
	}
	spfm_send(fd, slot, 1, 0x02, 0x00);
	spfm_send(fd, slot, 1, 0x03, 0x00);
	spfm_send(fd, slot, 1, 0x04, 0x00);
	spfm_send(fd, slot, 1, 0x05, 0x00);
	//spfm_send(fd, slot, 1, 0x01, 0xC0);
	spfm_send(fd, slot, 1, 0x00, 0xB0);

	spfm_send(fd, slot, 0, 0x07, 0x3F);
	spfm_send(fd, slot, 0, 0x08, 0x00);
	spfm_send(fd, slot, 0, 0x09, 0x00);
	spfm_send(fd, slot, 0, 0x0A, 0x00);
}
