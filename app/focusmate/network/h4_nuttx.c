/******************************************************************************
 *
 * Copyright (C) 2024 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *****************************************************************************/

/* FocusMate adaptation: cross-task file_t I/O; original Apache license above. */
#include <nuttx/fs/fs.h>
#include <nuttx/semaphore.h>
#include <nuttx/clock.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <debug.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/bluetooth.h>

extern void btsnoop_log_capture(uint8_t is_receive, uint8_t *hci_pkt, uint32_t hci_pkt_size);

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_driver);

#define DT_DRV_COMPAT zephyr_bt_hci_ttyHCI
#define RX_THREAD_STACK_USED_MODE_SIZE    (3072)
#define RX_THREAD_STACK_DEBUG_MODE_SIZE   (4096)

struct h4_data {
	struct file file;
        bool opened;
        sem_t rx_ready;
        struct pollfd rx_poll;
	pthread_mutex_t mutex;
	bt_hci_recv_t recv;
	void *hci_data;
	struct k_thread rx_thread_data;
#if (CONFIG_BLUETOOTH_SERVICE_LOG_LEVEL > 2 || CONFIG_BT_DEBUG_LOG > 2)
	K_KERNEL_STACK_DEFINE(rx_thread_stack, RX_THREAD_STACK_DEBUG_MODE_SIZE);
#else
	K_KERNEL_STACK_DEFINE(rx_thread_stack, RX_THREAD_STACK_USED_MODE_SIZE);
#endif //(CONFIG_BLUETOOTH_SERVICE_LOG_LEVEL > 2 || CONFIG_BT_DEBUG_LOG > 2)
	uint8_t frame[1026];
};

#define HCI_DEBUG 0

static void h4_data_dump(const char *tag, uint8_t type, uint8_t *data, uint32_t len)
{
#if HCI_DEBUG
	struct iovec bufs[2];

	bufs[0].iov_base = &type;
	bufs[0].iov_len = 1;
	bufs[1].iov_base = data;
	bufs[1].iov_len = len;

	lib_dumpvbuffer(tag, bufs, 2);
#endif
}

static int h4_send_data(struct h4_data *h4, uint8_t *buf, size_t count)
{
        size_t written = 0;
        clock_t start = clock_systime_ticks();
        while (written < count) {
                ssize_t ret = file_write(&h4->file, buf + written, count - written);
                if (ret > 0) written += ret;
                else if (ret != -EAGAIN && ret != -EINTR) {
                        printf("[h4-file] send error=%ld\n", (long)ret);
                        return ret ? ret : -EIO;
                }
                /* Controller startup can make the first file_write slow.
                 * A completed write must succeed, even after the retry budget.
                 * Otherwise the host can overwrite a real Command Complete
                 * with a synthetic send error and double-complete a command. */
                if (written == count) break;
                if (clock_systime_ticks() - start > SEC2TICK(2)) return -ETIMEDOUT;
                if (ret <= 0) usleep(1000);
        }
        return written;
}

static struct net_buf *get_rx(const uint8_t *buf)
{
	bool discardable = false;
	k_timeout_t timeout = K_FOREVER;

	switch (buf[0]) {
	case BT_HCI_H4_EVT:
		if (buf[1] == BT_HCI_EVT_LE_META_EVENT &&
		    (buf[3] == BT_HCI_EVT_LE_ADVERTISING_REPORT)) {
			discardable = true;
			timeout = K_NO_WAIT;
		}

		return bt_buf_get_evt(buf[1], discardable, timeout);
	case BT_HCI_H4_ACL:
		return bt_buf_get_rx(BT_BUF_ACL_IN, K_FOREVER);
	case BT_HCI_H4_ISO:
		if (IS_ENABLED(CONFIG_BT_ISO)) {
			return bt_buf_get_rx(BT_BUF_ISO_IN, K_FOREVER);
		}
		__fallthrough;
	default:
		LOG_ERR("Unknown packet type: %u", buf[0]);
	}

	return NULL;
}

/**
 * @brief Decode the length of an HCI H4 packet and check it's complete
 * @details Decodes packet length according to Bluetooth spec v5.4 Vol 4 Part E
 * @param buf	Pointer to a HCI packet buffer
 * @param buf_len	Bytes available in the buffer
 * @return Length of the complete HCI packet in bytes, -1 if cannot find an HCI
 *         packet, 0 if more data required.
 */
static int32_t hci_packet_complete(const uint8_t *buf, uint16_t buf_len)
{
	uint16_t payload_len = 0;
	const uint8_t type = buf[0];
	uint8_t header_len = sizeof(type);
	const uint8_t *hdr = &buf[sizeof(type)];

	switch (type) {
	case BT_HCI_H4_CMD: {
		if (buf_len < header_len + BT_HCI_CMD_HDR_SIZE) {
			return 0;
		}
		const struct bt_hci_cmd_hdr *cmd = (const struct bt_hci_cmd_hdr *)hdr;

		/* Parameter Total Length */
		payload_len = cmd->param_len;
		header_len += BT_HCI_CMD_HDR_SIZE;
		break;
	}
	case BT_HCI_H4_ACL: {
		if (buf_len < header_len + BT_HCI_ACL_HDR_SIZE) {
			return 0;
		}
		const struct bt_hci_acl_hdr *acl = (const struct bt_hci_acl_hdr *)hdr;

		/* Data Total Length */
		payload_len = sys_le16_to_cpu(acl->len);
		header_len += BT_HCI_ACL_HDR_SIZE;
		break;
	}
	case BT_HCI_H4_SCO: {
		if (buf_len < header_len + BT_HCI_SCO_HDR_SIZE) {
			return 0;
		}
		const struct bt_hci_sco_hdr *sco = (const struct bt_hci_sco_hdr *)hdr;

		/* Data_Total_Length */
		payload_len = sco->len;
		header_len += BT_HCI_SCO_HDR_SIZE;
		break;
	}
	case BT_HCI_H4_EVT: {
		if (buf_len < header_len + BT_HCI_EVT_HDR_SIZE) {
			return 0;
		}
		const struct bt_hci_evt_hdr *evt = (const struct bt_hci_evt_hdr *)hdr;

		/* Parameter Total Length */
		payload_len = evt->len;
		header_len += BT_HCI_EVT_HDR_SIZE;
		break;
	}
	case BT_HCI_H4_ISO: {
		if (buf_len < header_len + BT_HCI_ISO_HDR_SIZE) {
			return 0;
		}
		const struct bt_hci_iso_hdr *iso = (const struct bt_hci_iso_hdr *)hdr;

		/* ISO_Data_Load_Length parameter */
		payload_len = bt_iso_hdr_len(sys_le16_to_cpu(iso->len));
		header_len += BT_HCI_ISO_HDR_SIZE;
		break;
	}
	/* If no valid packet type found */
	default:
		LOG_WRN("Unknown packet type 0x%02x", type);
		return -1;
	}

	/* Request more data */
	if (buf_len < header_len + payload_len) {
		return 0;
	}

	return (int32_t)header_len + payload_len;
}

#if 1
static void h4_poll_ready(struct pollfd *pfd)
{
        nxsem_post((sem_t *)pfd->arg);
}

static bool h4_ready(struct h4_data *h4)
{
        if (!h4->opened) return false;
        while (nxsem_trywait(&h4->rx_ready) == 0) { }
        memset(&h4->rx_poll, 0, sizeof(h4->rx_poll));
        h4->rx_poll.fd = -1;
        h4->rx_poll.events = POLLIN;
        h4->rx_poll.arg = &h4->rx_ready;
        h4->rx_poll.cb = h4_poll_ready;
        int ret = file_poll(&h4->file, &h4->rx_poll, true);
        if (ret < 0) return false;
        if (!h4->rx_poll.revents)
                nxsem_tickwait_uninterruptible(&h4->rx_ready, MSEC2TICK(500));
        file_poll(&h4->file, &h4->rx_poll, false);
        return (h4->rx_poll.revents & (POLLIN | POLLERR | POLLHUP)) != 0;
}
#endif

static void h4_rx_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;
	struct h4_data *h4 = dev->data;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_DBG("started");

	ssize_t frame_size = 0;

	while (1) {
		struct net_buf *buf;
		size_t buf_tailroom;
		size_t buf_add_len;
		ssize_t len;
		const uint8_t *frame_start = h4->frame;

#if 1
		if (!h4_ready(h4)) {
			usleep(1000);
			continue;
		}
#endif

		LOG_DBG("calling read()");

		len = file_read(&h4->file, h4->frame + frame_size, sizeof(h4->frame) - frame_size);
		if (len < 0) {
			if (len == -EINTR) {
				continue;
			}

			if (len == -EAGAIN) {
				usleep(500);
				continue;
			}

			LOG_ERR("Reading hci failed, errno %d", errno);
			file_close(&h4->file);
			h4->opened = false;
			return;
		}

		frame_size += len;

		while (frame_size > 0) {
			const uint8_t *buf_add;
			const uint8_t packet_type = frame_start[0];
			const int32_t decoded_len = hci_packet_complete(frame_start, frame_size);

			if (decoded_len == -1) {
				LOG_ERR("HCI Packet type is invalid, length could not be decoded");
				frame_size = 0; /* Drop buffer */
				break;
			}

			if (decoded_len == 0) {
				if (frame_size == sizeof(h4->frame)) {
					LOG_ERR("HCI Packet (%d bytes) is too big for frame (%d "
						"bytes)",
						decoded_len, sizeof(h4->frame));
					frame_size = 0; /* Drop buffer */
					break;
				}
				if (frame_start != h4->frame) {
					memmove(h4->frame, frame_start, frame_size);
				}
				/* Read more */
				break;
			}

			buf_add = frame_start + sizeof(packet_type);
			buf_add_len = decoded_len - sizeof(packet_type);

			buf = get_rx(frame_start);

			btsnoop_log_capture(1, (uint8_t *)frame_start, decoded_len);

			frame_size -= decoded_len;
			frame_start += decoded_len;

			if (!buf) {
				LOG_DBG("Discard adv report due to insufficient buf");
				continue;
			}

			buf_tailroom = net_buf_tailroom(buf);
			if (buf_tailroom < buf_add_len) {
				LOG_ERR("Not enough space in buffer %zu/%zu", buf_add_len,
					buf_tailroom);
				net_buf_unref(buf);
				continue;
			}

			net_buf_add_mem(buf, buf_add, buf_add_len);

			LOG_DBG("Calling bt_recv(%p)", buf);

			h4_data_dump("BT RX", packet_type, buf->data, buf_add_len);
			h4->recv(dev, buf, h4->hci_data);
		}
	}
}

static int h4_send(const struct device *dev, struct net_buf *buf)
{
	struct h4_data *h4 = dev->data;
	int len;
	int ret;

	LOG_DBG("buf %p type %u len %u", buf, bt_buf_get_type(buf), buf->len);

	switch (bt_buf_get_type(buf)) {
	case BT_BUF_ACL_OUT:
		net_buf_push_u8(buf, BT_HCI_H4_ACL);
		break;
	case BT_BUF_CMD:
		net_buf_push_u8(buf, BT_HCI_H4_CMD);
		break;
	case BT_BUF_ISO_OUT:
		if (IS_ENABLED(CONFIG_BT_ISO)) {
			net_buf_push_u8(buf, BT_HCI_H4_ISO);
			break;
		}
		__fallthrough;
	default:
		LOG_ERR("Unknown buffer type");
		return -EINVAL;
	}

	h4_data_dump("BT TX", buf->data[0], buf->data + 1, buf->len - 1);

	pthread_mutex_lock(&h4->mutex);
	len = buf->len;
	btsnoop_log_capture(0, buf->data, buf->len);
	ret = h4_send_data(h4, buf->data, buf->len);
	if (ret != len) {
		/* Host releases failed sends; retain its buffer ownership. */
		if (ret >= 0) ret = -EIO;
		net_buf_pull_u8(buf);
	} else {
		net_buf_unref(buf);
		ret = 0;
	}
	pthread_mutex_unlock(&h4->mutex);

	return ret;
}

static int h4_open(const struct device *dev, bt_hci_recv_t recv, void *hci_data)
{
        struct h4_data *h4 = dev->data;
        if (!dev->name) return -EINVAL;
        int ret = file_open(&h4->file, dev->name, O_RDWR | O_NONBLOCK | O_BINARY);
        if (ret < 0) return ret;
        nxsem_init(&h4->rx_ready, 0, 0);
        nxsem_set_protocol(&h4->rx_ready, SEM_PRIO_NONE);
        memset(&h4->rx_poll, 0, sizeof(h4->rx_poll));
        h4->recv = recv;
        h4->hci_data = hci_data;
        h4->opened = true;
        ret = (int)k_thread_create(&h4->rx_thread_data, h4->rx_thread_stack,
                  K_THREAD_STACK_SIZEOF(h4->rx_thread_stack), h4_rx_thread,
                  (void *)dev, NULL, NULL, K_PRIO_COOP(CONFIG_BT_RX_PRIO), 0, K_NO_WAIT);
        if (ret < 0) {
                h4->opened = false;
                file_close(&h4->file);
                nxsem_destroy(&h4->rx_ready);
                return ret;
        }
        k_thread_name_set(&h4->rx_thread_data, "BT H4 file RX");
        printf("[h4-file] opened %s; cross-task file I/O\n", dev->name);
        return 0;
}

static int h4_close(const struct device *dev)
{
        struct h4_data *h4 = dev->data;
        if (!h4->opened) return 0;
        k_thread_abort(&h4->rx_thread_data);
        file_poll(&h4->file, &h4->rx_poll, false);
        h4->opened = false;
        int ret = file_close(&h4->file);
        nxsem_destroy(&h4->rx_ready);
        return ret;
}

static const struct bt_hci_driver_api h4_drv_api = {
	.open = h4_open,
	.close = h4_close,
	.send = h4_send,
};

static int h4_init(const struct device *dev)
{
	LOG_INF("Bluetooth H4 driver %s", dev->name);

	return 0;
}

#define DT_HCI_INST(node, inst) DT_CAT(node, inst)

#define H4_DEVICE_INIT(inst)                                                                       \
	static struct h4_data h4_data_##inst = {                                                   \
		.opened = false,                                                                             \
		.mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP,                                   \
	};                                                                                         \
	DEVICE_DT_DEFINE(DT_HCI_INST(DT_DRV_INST(inst), inst), h4_init, NULL, &h4_data_##inst, NULL, POST_KERNEL,             \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &h4_drv_api)

H4_DEVICE_INIT(0);
H4_DEVICE_INIT(1);
