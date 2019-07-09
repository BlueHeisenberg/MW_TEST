/************************************************************************//**
 * \brief 1985 Channel
 * \author Jesús Alonso (doragasu)
 * \date   2019
 * \defgroup 1985ch main
 * \{
 ****************************************************************************/
#include "vdp.h"
#include "mw/util.h"
#include "mw/mpool.h"
#include "mw/megawifi.h"
#include "mw/loop.h"

/// Length of the wflash buffer
#define MW_BUFLEN	1440

/// TCP port to use (set to Megadrive release year ;-)
#define MW_CH_PORT 	1985

/// Maximum number of loop functions
#define MW_MAX_LOOP_FUNCS	2

/// Maximun number of loop timers
#define MW_MAX_LOOP_TIMERS	4

/// Command buffer
static char cmd_buf[MW_BUFLEN];

static void udp_recv_cb(enum lsd_status stat, uint8_t ch,
		char *data, uint16_t len, void *ctx);

static void println(const char *str, int color)
{
	static unsigned int line = 2;

	if (str) {
		VdpDrawText(VDP_PLANEA_ADDR, 2, line, color, 36, str, 0);
	}
	line++;
}

static void idle_cb(struct loop_func *f)
{
	UNUSED_PARAM(f);
	mw_process();
}

static void udp_send_complete_cb(enum lsd_status stat, void *ctx)
{
	struct mw_reuse_payload *pkt =
		(struct mw_reuse_payload * const)cmd_buf;
	UNUSED_PARAM(ctx);
	UNUSED_PARAM(stat);

	// Trigger reception of another UDP packet
	mw_udp_reuse_recv(pkt, MW_BUFLEN, NULL, udp_recv_cb);
}

static void udp_recv_cb(enum lsd_status stat, uint8_t ch,
		char *data, uint16_t len, void *ctx)
{
	const struct mw_reuse_payload *udp =
		(const struct mw_reuse_payload*)data;
	UNUSED_PARAM(ctx);

	if (LSD_STAT_COMPLETE == stat) {
		mw_udp_reuse_send(ch, udp, len, NULL, udp_send_complete_cb);
	}
}

static void udp_normal_test(void)
{
	char line[40];
	int16_t len = sizeof(line);
	uint8_t ch = 1;

	// Send UDP data to peer and wait for reply
	mw_udp_set(ch, "192.168.1.10", "12345", NULL);
	mw_send_sync(ch, "MegaWiFi UDP test!", 19, 0);
	mw_recv_sync(&ch, line, &len, 5 * 60);
	line[39] = '\0';
	if (1 == ch) {
		println("Got UDP reply:", VDP_TXT_COL_CYAN);
		println(line, VDP_TXT_COL_WHITE);
	}
	mw_udp_unset(ch);
}

static void udp_reuse_test(void)
{
	struct mw_reuse_payload *pkt =
		(struct mw_reuse_payload * const)cmd_buf;

	// Start UDP echo task
	mw_udp_set(1, NULL, NULL, "7");
	mw_udp_reuse_recv(pkt, MW_BUFLEN, NULL, udp_recv_cb);
}

#define BUFF_LENGHT 256
char strin[BUFF_LENGHT];
const char* strout = "Sending text to the future from BLASTPROCESSING";
static void run_test(struct loop_timer *t)
{
	enum mw_err err;

	// Join AP
	println("Associating to AP", VDP_TXT_COL_WHITE);
	err = mw_ap_assoc(0);
	if (err != MW_ERR_NONE) {
		goto err;
	}
	err = mw_ap_assoc_wait(MS_TO_FRAMES(30000));
	if (err != MW_ERR_NONE) {
		goto err;
	} else {
        // Wait an additional second to ensure DNS service is up
        mw_sleep(MS_TO_FRAMES(1000));
        println("DONE!", VDP_TXT_COL_CYAN);
        println(NULL, 0);
	}

	// Connect to www.duck.com on port 443
	println("Connecting www.duck.com", VDP_TXT_COL_WHITE);
	err = mw_tcp_connect(1, "192.168.0.8", "1337", NULL);
    if (err != MW_ERR_NONE) {
        goto err;
    }

    println("DONE!", VDP_TXT_COL_CYAN);

    uint8_t channel = 1;
	int16_t bufLength = BUFF_LENGHT;

    err = mw_recv_sync(&channel, strin, &bufLength, MS_TO_FRAMES(5000));
    if (err != MW_ERR_NONE) {
        goto err;
    }

    println(NULL, 0);
    println(NULL, 0);

    println("Received: ", 0);

    println(strin, VDP_TXT_COL_WHITE);

    mw_sleep(MS_TO_FRAMES(1000));

    err = mw_send_sync(channel, strout, strlen(strout) * sizeof(char), MS_TO_FRAMES(10000));
    if (err != MW_ERR_NONE) {
        goto err;
    }

    goto out;

	// Test UDP in normal mode
	//udp_normal_test();

	// Test UDP in reuse mode
	//udp_reuse_test();

err:
    println("ERROR", VDP_TXT_COL_MAGENTA);

out:
	loop_timer_del(t);

    mw_tcp_disconnect(1);
    mw_ap_disassoc();
}

/// MegaWiFi initialization
static void megawifi_init_cb(struct loop_timer  *t)
{
	uint8_t ver_major = 0, ver_minor = 0;
	char *variant = NULL;
	enum mw_err err;
	char line[] = "MegaWiFi version X.Y";

	// Try detecting the module
	err = mw_detect(&ver_major, &ver_minor, &variant);

	if (MW_ERR_NONE != err) {
		// Megawifi not found
		println("MegaWiFi not found!", VDP_TXT_COL_MAGENTA);
	} else {
		// Megawifi found
		line[17] = ver_major + '0';
		line[19] = ver_minor + '0';
		println(line, VDP_TXT_COL_WHITE);
		println(NULL, 0);
		// Configuration complete, run test function next frame
		t->timer_cb = run_test;
		loop_timer_start(t, 1);

	}
}

/// Loop run while idle
static void main_loop_init(void)
{
	// Run next frame, do not auto-reload
	static struct loop_timer frame_timer = {
		.timer_cb = megawifi_init_cb,
		.frames = 1
	};
	static struct loop_func megawifi_loop = {
		.func_cb = idle_cb
	};

	loop_init(MW_MAX_LOOP_FUNCS, MW_MAX_LOOP_TIMERS);
	loop_timer_add(&frame_timer);
	loop_func_add(&megawifi_loop);
}

/// Global initialization
static void init(void)
{
	// Initialize memory pool
	mp_init(0);
	// Initialize VDP
	VdpInit();
	// Initialize game loop
	main_loop_init();
	// Initialize MegaWiFi
	mw_init(cmd_buf, MW_BUFLEN);
}

/// Entry point
int main(void)
{
	init();

	// Enter game loop (should never return)
	loop();

	return 0;
}

/** \} */

