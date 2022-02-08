/*
 * Copyright (C) 2013 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <time.h>
#include <regex.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#define SYSLOG_NAMES
#include <syslog.h>

#include <libubox/ustream.h>
#include <libubox/blobmsg_json.h>
#include <libubox/usock.h>
#include <libubox/uloop.h>
#include "libubus.h"
#include "syslog.h"

#include <stdlib.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <errno.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#define LOGD_CONNECT_RETRY	10

// #define ENABLE_ENCRYPT_DEBUG 1
// #define ENABLE_SET_IVLEN 1

#ifdef ENABLE_ENCRYPT_DEBUG
#define ENCRYPT_DEBUG_FILE "/tmp/logread_dbg.txt"
#define ENCRYPT_DEBUG_PRINT(fmt, ...) do { \
	if (fp_debug != NULL) { \
		fprintf(fp_debug, fmt, ##__VA_ARGS__); \
		fflush(fp_debug); \
	} \
} while (0)
static FILE *fp_debug;
#else
#define ENCRYPT_DEBUG_PRINT(fmt, ...)
#endif
#define ENCRYPT_DEBUG(fmt, ...) ENCRYPT_DEBUG_PRINT("%16s:%-4d: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define ENCRYPT_ALGO EVP_aes_256_gcm()
#define AES_256_KEY_SIZE 32
#define UI_AES_GCM_TAG_SIZE 16
#define UI_LOG_HEADER_LEN 9
#define HASH_ID_SIZE 8
#define MODE_ENCRYPT 1
#define MODE_DECRYPT 0
#define MODE_NO_CHNG -1

static int ui_encrypt_en;
static int ui_encrypt_ready;
static unsigned char log_key[AES_256_KEY_SIZE];

/* The Authenticated Associated Data consists of
 * UI Syslog Header
 *  Magic number 0xABCDDCBA
 *  version 0x01
 *  flags 0x00000001
 *  8-byte Device ID
 * Followed by
 *  12-byte IV
 */
unsigned char gcm_aad[] = {
	0xAB, 0xCD, 0xDC, 0xBA, 0x01,
	0x00, 0x00, 0x00, 0x01,
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
};

static unsigned char gcm_iv[12];

enum {
	LOG_STDOUT,
	LOG_FILE,
	LOG_NET,
};

enum {
	LOG_MSG,
	LOG_ID,
	LOG_PRIO,
	LOG_SOURCE,
	LOG_TIME,
	__LOG_MAX
};

static const struct blobmsg_policy log_policy[] = {
	[LOG_MSG] = { .name = "msg", .type = BLOBMSG_TYPE_STRING },
	[LOG_ID] = { .name = "id", .type = BLOBMSG_TYPE_INT32 },
	[LOG_PRIO] = { .name = "priority", .type = BLOBMSG_TYPE_INT32 },
	[LOG_SOURCE] = { .name = "source", .type = BLOBMSG_TYPE_INT32 },
	[LOG_TIME] = { .name = "time", .type = BLOBMSG_TYPE_INT64 },
};

static struct uloop_timeout retry;
static struct uloop_fd sender;
static regex_t regexp_preg;
static const char *log_file, *log_ip, *log_port, *log_prefix, *pid_file, *hostname, *regexp_pattern;
static int log_type = LOG_STDOUT;
static int log_size, log_udp, log_follow, log_trailer_null = 0;
static int log_timestamp;
static int logd_conn_tries = LOGD_CONNECT_RETRY;

static const char* getcodetext(int value, CODE *codetable) {
	CODE *i;

	if (value >= 0)
		for (i = codetable; i->c_val != -1; i++)
			if (i->c_val == value)
				return (i->c_name);
	return "<unknown>";
};

static void log_handle_reconnect(struct uloop_timeout *timeout)
{
	sender.fd = usock((log_udp) ? (USOCK_UDP) : (USOCK_TCP), log_ip, log_port);
	if (sender.fd < 0) {
		fprintf(stderr, "failed to connect: %m\n");
		uloop_timeout_set(&retry, 1000);
	} else {
		uloop_fd_add(&sender, ULOOP_READ);
		syslog(LOG_INFO, "Logread connected to %s:%s\n", log_ip, log_port);
	}
}

static void log_handle_fd(struct uloop_fd *u, unsigned int events)
{
	if (u->eof) {
		uloop_fd_delete(u);
		close(sender.fd);
		sender.fd = -1;
		uloop_timeout_set(&retry, 1000);
	}
}

/* Allow enough space in output buffer for additional aes block
 * return < 0 for error */
static int ui_encrypt(
		const unsigned char *key,
		const unsigned char *aad, int aad_len,
		const unsigned char *iv, int iv_len,
		const unsigned char *ptext, int ptext_len,
		unsigned char *cipher_out, int cipher_out_len)
{
	EVP_CIPHER_CTX *ctx;
	int out_len = 0;
	int out_len_tmp;
#ifdef ENABLE_SET_IVLEN
	int default_iv_length;
#endif

	if (ptext_len < 0)
		return -1;
	// Note AES GCM does not perform padding of messages block_size == 1.
	if (aad_len + iv_len + ptext_len + UI_AES_GCM_TAG_SIZE > cipher_out_len) {
		fprintf(stderr, "ERROR: not enough space to encrypt aad %d iv %d ptext %d tag %d out %d\n",
				aad_len, iv_len, ptext_len, UI_AES_GCM_TAG_SIZE, cipher_out_len);
		return -1;
	}
	if (ptext_len == 0)
		return 0;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL) {
		fprintf(stderr, "ERROR: EVP_CIPHER_CTX_new failed. OpenSSL error: %s\n",
				ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}

	if (!EVP_CipherInit_ex(ctx, ENCRYPT_ALGO, NULL, NULL, NULL, MODE_ENCRYPT)) {
		fprintf(stderr, "ERROR: EVP_CipherInit_ex failed. OpenSSL error: %s\n",
				ERR_error_string(ERR_get_error(), NULL));
		goto err_cleanup;
	}
#ifdef ENABLE_SET_IVLEN
	/* EVP_CTRL_GCM_SET_IVLEN is broken openssl version OpenSSL 1.1.0g 2 Nov 2017,
	 * it does not change the length. We are stuck with 12-byte (96-bit) IV.
	 */
	default_iv_length = EVP_CIPHER_CTX_iv_length(ctx);
	ENCRYPT_DEBUG("default iv_length:%d\n", default_iv_length);
	if (iv_len != default_iv_length) {
		if(!EVP_CIPHER_CTX_ctrl(ctx,  EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL)) {
			fprintf(stderr, "ERROR: EVP_CIPHER_CTX_ctrl failed. OpenSSL error: %s\n",
				ERR_error_string(ERR_get_error(), NULL));
			goto err_cleanup;
		}
		ENCRYPT_DEBUG("Set IV len : %d now %d\n",
				iv_len, EVP_CIPHER_CTX_iv_length(ctx));
	}
#endif /* ENABLE_SET_IVLEN */

	if (!EVP_CipherInit_ex(ctx, NULL, NULL, key, iv, MODE_NO_CHNG)) {
		fprintf(stderr, "ERROR: EVP_CipherInit_ex failed key/iv. OpenSSL error: %s\n",
				ERR_error_string(ERR_get_error(), NULL));
		goto err_cleanup;
	}

	ENCRYPT_DEBUG("ptext_len:%d iv_length:%d\n", ptext_len, EVP_CIPHER_CTX_iv_length(ctx));

	/* Additional Associated Data
	 * UI Magic | version | flags | Device ID | IV */
	memcpy(cipher_out + out_len, aad, aad_len);
	out_len += aad_len;
	memcpy(cipher_out + out_len, iv, iv_len);
	out_len += iv_len;

	/* Pass AAD data through cipher */
	if (!EVP_CipherUpdate(ctx, NULL, &out_len_tmp, cipher_out, out_len)) {
		fprintf(stderr, "ERROR:  EVP_CipherUpdate failed. OpenSSL error: %s\n",
				ERR_error_string(ERR_get_error(), NULL));
		goto err_cleanup;
	}
	ENCRYPT_DEBUG("%5s: out_len_tmp:%4d out_len:%4d \n", "AAD", out_len_tmp, out_len);

	/* Plain text to encrypt */
	if (!EVP_CipherUpdate(ctx, cipher_out + out_len, &out_len_tmp, ptext, ptext_len)) {
		fprintf(stderr, "ERROR:  EVP_CipherUpdate failed. OpenSSL error: %s\n",
				ERR_error_string(ERR_get_error(), NULL));
		goto err_cleanup;
	}
	out_len += out_len_tmp;
	ENCRYPT_DEBUG("%5s: out_len_tmp:%4d out_len:%4d \n", "ptext", out_len_tmp, out_len);

	/* Finalize the cipher seal the EVP */
	if (!EVP_CipherFinal_ex(ctx, cipher_out + out_len, &out_len_tmp)) {
		fprintf(stderr, "ERROR:  EVP_CipherFinal_ex failed. OpenSSL error: %s\n",
				ERR_error_string(ERR_get_error(), NULL));
		goto err_cleanup;
	}
	out_len += out_len_tmp;
	ENCRYPT_DEBUG("%5s: out_len_tmp:%4d out_len:%4d \n", "final", out_len_tmp, out_len);

	/* Get the TAG and concatenate to end of cipher_out buffer */
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, UI_AES_GCM_TAG_SIZE, cipher_out + out_len)) {
		fprintf(stderr, "ERROR:  EVP_CIPHTER_CTX_ctrl GET_TAG  failed. OpenSSL error: %s\n",
				ERR_error_string(ERR_get_error(), NULL));
		goto err_cleanup;
	}
	out_len += UI_AES_GCM_TAG_SIZE;
	ENCRYPT_DEBUG("%5s: out_len_tmp:%4d out_len:%4d \n", "tag", out_len_tmp, out_len);
	EVP_CIPHER_CTX_free(ctx);
	return out_len;

err_cleanup:
	EVP_CIPHER_CTX_free(ctx);
	return -1;
}

#define DEV_RANDOM "/dev/random"
#define SEED_BYTES 64 /* openssl recommends *at least* 32 */
static int ui_init_iv(void)
{
	int rc;

	rc = RAND_load_file(DEV_RANDOM, SEED_BYTES);
	if (rc != SEED_BYTES) {
		fprintf(stderr, "Failed to seed PRNG from '%s'\n", DEV_RANDOM);
		return -1;
	}

	rc = RAND_bytes(gcm_iv, sizeof(gcm_iv));
	if (rc <= 0) {
		fprintf(stderr, "Failed to init random IV: %s\n", ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}
	return rc;
}

static void ui_bump_iv(void)
{
	int i = sizeof(gcm_iv)-1;
	while (i >= 0) {
		gcm_iv[i] += 1;
		if (gcm_iv[i] != 0)
			break;
		i--;
	}
}

static int log_notify(struct blob_attr *msg)
{
	struct blob_attr *tb[__LOG_MAX];
	struct stat s;
	char buf[LOG_LINE_SIZE + 128];
	unsigned char buf_enc[LOG_LINE_SIZE + 128 + sizeof(gcm_aad) + sizeof(gcm_iv) + UI_AES_GCM_TAG_SIZE];
	char *sendbuf;
	int len;
	char buf_ts[32];
	uint32_t p;
	time_t t;
	uint32_t t_ms = 0;
	char *c, *m;
	int ret = 0;

	if (sender.fd < 0)
		return 0;

	blobmsg_parse(log_policy, ARRAY_SIZE(log_policy), tb, blob_data(msg), blob_len(msg));
	if (!tb[LOG_ID] || !tb[LOG_PRIO] || !tb[LOG_SOURCE] || !tb[LOG_TIME] || !tb[LOG_MSG])
		return 1;

	if ((log_type == LOG_FILE) && log_size && (!stat(log_file, &s)) && (s.st_size > log_size)) {
		char *old = malloc(strlen(log_file) + 5);

		close(sender.fd);
		if (old) {
			sprintf(old, "%s.old", log_file);
			rename(log_file, old);
			free(old);
		}
		sender.fd = open(log_file, O_CREAT | O_WRONLY | O_APPEND, 0600);
		if (sender.fd < 0) {
			fprintf(stderr, "failed to open %s: %m\n", log_file);
			exit(-1);
		}
	}

	m = blobmsg_get_string(tb[LOG_MSG]);
	if (regexp_pattern &&
	    regexec(&regexp_preg, m, 0, NULL, 0) == REG_NOMATCH)
		return 0;
	t = blobmsg_get_u64(tb[LOG_TIME]) / 1000;
	if (log_timestamp) {
		t_ms = blobmsg_get_u64(tb[LOG_TIME]) % 1000;
		snprintf(buf_ts, sizeof(buf_ts), "[%lu.%03u] ",
				(unsigned long)t, t_ms);
	}
	c = ctime(&t);
	p = blobmsg_get_u32(tb[LOG_PRIO]);
	c[strlen(c) - 1] = '\0';

	if (log_type == LOG_NET) {
		int err = 0;

		snprintf(buf, sizeof(buf), "<%u>", p);
		strncat(buf, c + 4, 16);
		if (log_timestamp) {
			strncat(buf, buf_ts, sizeof(buf) - strlen(buf) - 1);
		}
		if (hostname) {
			strncat(buf, hostname, sizeof(buf) - strlen(buf) - 1);
			strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
		}
		if (log_prefix) {
			strncat(buf, log_prefix, sizeof(buf) - strlen(buf) - 1);
			strncat(buf, ": ", sizeof(buf) - strlen(buf) - 1);
		}
		if (blobmsg_get_u32(tb[LOG_SOURCE]) == SOURCE_KLOG)
			strncat(buf, "kernel: ", sizeof(buf) - strlen(buf) - 1);
		strncat(buf, m, sizeof(buf) - strlen(buf) - 1);
		if (log_udp) {
			len = strlen(buf);
			sendbuf = buf;
			if (ui_encrypt_en) {
				if (ui_encrypt_ready) {
					ui_bump_iv();
					len = ui_encrypt(log_key,
							gcm_aad, sizeof(gcm_aad),
							gcm_iv, sizeof(gcm_iv),
							(unsigned char *)sendbuf, len,
							buf_enc, sizeof(buf_enc));
					ENCRYPT_DEBUG("ui_encrypt returns:%d\n", len)
					if (len > 0) {
						sendbuf = (char *)buf_enc;
					} else {
						len = 0;
					}
				} else {
					len = 0;
				}
			}
			if (len) {
				err = write(sender.fd, sendbuf, len);
			}
		}
		else {
			size_t buflen = strlen(buf);
			if (!log_trailer_null)
				buf[buflen] = '\n';
			err = send(sender.fd, buf, buflen + 1, 0);
		}

		if (err < 0) {
			syslog(LOG_INFO, "failed to send log data to %s:%s via %s\n",
				log_ip, log_port, (log_udp) ? ("udp") : ("tcp"));
			uloop_fd_delete(&sender);
			close(sender.fd);
			sender.fd = -1;
			uloop_timeout_set(&retry, 1000);
		}
	} else {
		snprintf(buf, sizeof(buf), "%s %s%s.%s%s %s\n",
			c, log_timestamp ? buf_ts : "",
			getcodetext(LOG_FAC(p) << 3, facilitynames),
			getcodetext(LOG_PRI(p), prioritynames),
			(blobmsg_get_u32(tb[LOG_SOURCE])) ? ("") : (" kernel:"), m);
		ret = write(sender.fd, buf, strlen(buf));
	}

	if (log_type == LOG_FILE)
		fsync(sender.fd);

	return ret;
}

static int usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [options]\n"
		"Options:\n"
		"    -s <path>		Path to ubus socket\n"
		"    -l	<count>		Got only the last 'count' messages\n"
		"    -e	<pattern>	Filter messages with a regexp\n"
		"    -r	<server> <port>	Stream message to a server\n"
		"    -F	<file>		Log file\n"
		"    -S	<bytes>		Log size\n"
		"    -p	<file>		PID file\n"
		"    -h	<hostname>	Add hostname to the message\n"
		"    -P	<prefix>	Prefix custom text to streamed messages\n"
		"    -f			Follow log messages\n"
		"    -u			Use UDP as the protocol\n"
		"    -t			Add an extra timestamp\n"
		"    -0			Use \\0 instead of \\n as trailer when using TCP\n"
		"    -E	<encrypt-key>	encrypt the message\n"
		"\n", prog);
	return 1;
}

static void logread_fd_data_cb(struct ustream *s, int bytes)
{
	while (true) {
		struct blob_attr *a;
		int len, cur_len;

		a = (void*) ustream_get_read_buf(s, &len);
		if (len < sizeof(*a))
			break;

		cur_len = blob_len(a) + sizeof(*a);
		if (len < cur_len)
			break;

		log_notify(a);
		ustream_consume(s, cur_len);
	}
}

static void logread_fd_state_cb(struct ustream *s)
{
	if (log_follow)
		logd_conn_tries = LOGD_CONNECT_RETRY;
	uloop_end();
}

static void logread_fd_cb(struct ubus_request *req, int fd)
{
	static struct ustream_fd test_fd;

	memset(&test_fd, 0, sizeof(test_fd));

	test_fd.stream.notify_read = logread_fd_data_cb;
	test_fd.stream.notify_state = logread_fd_state_cb;
	ustream_fd_init(&test_fd, fd);
}

static void logread_setup_output(void)
{
	if (sender.fd || sender.cb)
		return;

	if (log_ip && log_port) {
		openlog("logread", LOG_PID, LOG_DAEMON);
		log_type = LOG_NET;
		sender.cb = log_handle_fd;
		retry.cb = log_handle_reconnect;
		uloop_timeout_set(&retry, 1000);
	} else if (log_file) {
		log_type = LOG_FILE;
		sender.fd = open(log_file, O_CREAT | O_WRONLY| O_APPEND, 0600);
		if (sender.fd < 0) {
			fprintf(stderr, "failed to open %s: %m\n", log_file);
			exit(-1);
		}
	} else {
		sender.fd = STDOUT_FILENO;
	}
}

#define UBNTHAL_SYS_INFO "/proc/ubnthal/system.info"
static int ui_set_hashid_in_aad(void)
{
	FILE *fp;
	char string[256];
	char searchstr[] = "device.hashid";
	char *fieldptr;
	unsigned char hash_id[HASH_ID_SIZE];
	int rc;
	int count;

	fp = fopen(UBNTHAL_SYS_INFO, "r");
	if (!fp) {
		fprintf(stderr, "Failed to open '%s': %s\n", UBNTHAL_SYS_INFO, strerror(errno));
		return -1;
	}

	while (fscanf(fp, "%s", string) == 1) {
		if ((rc = strncmp(string, searchstr, sizeof(searchstr)-1)) == 0) {
			fieldptr = strchr(string, '=');
			fieldptr++;

			for (count=0; count < HASH_ID_SIZE; count++) {
				sscanf(fieldptr, "%2hhx", &hash_id[count]);
				fieldptr += 2;
			}
		}
	}
	memcpy(gcm_aad+UI_LOG_HEADER_LEN, hash_id, HASH_ID_SIZE);
	fclose(fp);
	return 0;
}

static int ui_key_conv(const char *key_in, unsigned char *key_out)
{
	int i, j;
	int len = strlen(key_in);
	ENCRYPT_DEBUG("input  %s len:%d\n", key_in, len);

	if (len != AES_256_KEY_SIZE * 2) {
		fprintf(stderr, "Invalid Key size\n");
		return -1;
	}
	j = 0;
	i = 0;
	ENCRYPT_DEBUG("output ");
	while(i < len) {
		if (sscanf(key_in + i, "%2hhx", key_out + j) <= 0) {
			fprintf(stderr, "Invalid Key: %s\n", key_in);
			ENCRYPT_DEBUG_PRINT("\n");
			return -1;
		}
		ENCRYPT_DEBUG_PRINT("%02x", key_out[j]);
		j++;
		i += 2;
	}
	ENCRYPT_DEBUG_PRINT("\n");
	return 0;
}

static int ui_encrypt_init(char *key)
{
	int ret;
	int block_size;

	// Ensure block_size is 1 => no padding
	if ((block_size = EVP_CIPHER_block_size(ENCRYPT_ALGO)) != 1) {
		fprintf(stderr, "Encryption invalid block_size: %d\n", block_size);
		return -1;
	}

	ret = ui_set_hashid_in_aad();
	if (ret < 0)
		return ret;
	ENCRYPT_DEBUG("hashid:%02x%02x..%02x%02x\n",
			gcm_aad[9], gcm_aad[10],
			gcm_aad[sizeof(gcm_aad) - 2], gcm_aad[sizeof(gcm_aad) - 1]);
	ret = ui_key_conv(key, log_key);
	ENCRYPT_DEBUG("ui_key_conv ret=%d\n", ret);
	if (ret < 0)
		return ret;
	ret = ui_init_iv();
	if (ret < 0)
		return ret;
	ENCRYPT_DEBUG("iv:%02x%02x..%02x%02x\n", gcm_iv[0], gcm_iv[1],
			gcm_iv[sizeof(gcm_iv) - 2], gcm_iv[sizeof(gcm_iv) - 1]);
	return 0;
}

int main(int argc, char **argv)
{
	struct ubus_context *ctx;
	uint32_t id;
	const char *ubus_socket = NULL;
	int ch, ret, lines = 0;
	static struct blob_buf b;
	ui_encrypt_en = ui_encrypt_ready = 0;

	signal(SIGPIPE, SIG_IGN);

	while ((ch = getopt(argc, argv, "u0fcs:l:r:F:p:S:P:h:e:tE:")) != -1) {
		switch (ch) {
		case 'u':
			log_udp = 1;
			break;
		case '0':
			log_trailer_null = 1;
			break;
		case 's':
			ubus_socket = optarg;
			break;
		case 'r':
			log_ip = optarg++;
			log_port = argv[optind++];
			break;
		case 'F':
			log_file = optarg;
			break;
		case 'p':
			pid_file = optarg;
			break;
		case 'P':
			log_prefix = optarg;
			break;
		case 'f':
			log_follow = 1;
			break;
		case 'l':
			lines = atoi(optarg);
			break;
		case 'S':
			log_size = atoi(optarg);
			if (log_size < 1)
				log_size = 1;
			log_size *= 1024;
			break;
		case 'h':
			hostname = optarg;
			break;
		case 'e':
			if (!regcomp(&regexp_preg, optarg, REG_NOSUB)) {
				regexp_pattern = optarg;
			}
			break;
		case 't':
			log_timestamp = 1;
			break;
		case 'E':
#ifdef ENABLE_ENCRYPT_DEBUG
			fp_debug = fopen(ENCRYPT_DEBUG_FILE, "w+");
			if (fp_debug == NULL) {
				fprintf(stderr, "Failed to open '%s': %s\n", ENCRYPT_DEBUG_FILE, strerror(errno));
			}
#endif
			ui_encrypt_en = 1;
			ui_encrypt_ready = (ui_encrypt_init(optarg) == 0);
			break;
		default:
			return usage(*argv);
		}
	}
	uloop_init();

	ctx = ubus_connect(ubus_socket);
	if (!ctx) {
		fprintf(stderr, "Failed to connect to ubus\n");
		return -1;
	}
	ubus_add_uloop(ctx);

	if (log_follow && pid_file) {
		FILE *fp = fopen(pid_file, "w+");
		if (fp) {
			fprintf(fp, "%d", getpid());
			fclose(fp);
		}
	}

	blob_buf_init(&b, 0);
	blobmsg_add_u8(&b, "stream", 1);
	blobmsg_add_u8(&b, "oneshot", !log_follow);
	if (lines)
		blobmsg_add_u32(&b, "lines", lines);
	else if (log_follow)
		blobmsg_add_u32(&b, "lines", 0);

	/* ugly ugly ugly ... we need a real reconnect logic */
	do {
		struct ubus_request req = { 0 };

		ret = ubus_lookup_id(ctx, "log", &id);
		if (ret) {
			fprintf(stderr, "Failed to find log object: %s\n", ubus_strerror(ret));
			sleep(1);
			continue;
		}
		logd_conn_tries = 0;
		logread_setup_output();

		ubus_invoke_async(ctx, id, "read", b.head, &req);
		req.fd_cb = logread_fd_cb;
		ubus_complete_request_async(ctx, &req);

		uloop_run();

	} while (logd_conn_tries--);

	ubus_free(ctx);
	uloop_done();

	if (log_follow && pid_file)
		unlink(pid_file);

#ifdef ENABLE_ENCRYPT_DEBUG
	if (fp_debug)
		fclose(fp_debug);
#endif

	return ret;
}
