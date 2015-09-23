/* See LICENSE for licence details. */
#define _XOPEN_SOURCE 600
#define _BSD_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

enum misc_t {
	BITS_PER_BYTE  = 8,
	BUFSIZE        = 32,
	SELECT_TIMEOUT = 15000, /* usec */
	/* SPFM Light slot: 0x00 or 0x01 */
	OPM_SLOT_NUM   = 0x00,
	OPNA_SLOT_NUM  = 0x01,
	OPL3_SLOT_NUM  = 0xFF,
};

static const char *serial_dev      = "/dev/ttyUSB0";
volatile sig_atomic_t catch_sigint = false;
