/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "libs/cdc_ecm/cdc_ecm.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "libs/base/check.h"
#include "libs/base/tasks.h"
#include "libs/base/utils.h"
#include "third_party/nxp/rt1176-sdk/middleware/lwip/src/include/lwip/etharp.h"
#include "third_party/nxp/rt1176-sdk/middleware/lwip/src/include/netif/ethernet.h"

extern "C" void start_dhcp_server(uint32_t local_addr);

namespace coralmicro {
namespace {
constexpr int kDataIn = 0;
constexpr int kDataOut = 1;
// High speed bulk, 13 packets of 512 bytes per microframe.
constexpr uint32_t kBitRate = 13 * 512 * 8 * 1000 * 8;
}  // namespace

std::map<class_handle_t, CdcEcm *> CdcEcm::handle_map_;

std::string CdcEcm::HostMacString() {
  // Locally administered, so Linux keeps naming the interface usb%d.
  const uint64_t id = GetUniqueId();
  char mac[13];
  snprintf(mac, sizeof(mac), "021A11%06lX",
           static_cast<uint32_t>(id & 0xFFFFFF));
  return mac;
}

void CdcEcm::Init(uint8_t interrupt_in_ep, uint8_t bulk_in_ep,
                  uint8_t bulk_out_ep, uint8_t comm_iface, uint8_t data_iface,
                  uint8_t mac_string) {
  // descriptor_ is constant; the values handed out must match it.
  CHECK(interrupt_in_ep == kNotifyEndpoint);
  CHECK(bulk_in_ep == kBulkInEndpoint);
  CHECK(bulk_out_ep == kBulkOutEndpoint);
  CHECK(comm_iface == kCommInterface);
  CHECK(data_iface == kDataInterface);
  CHECK(mac_string == kMacString);

  comm_endpoints_[0].endpointAddress = interrupt_in_ep | (USB_IN << 7);
  data_endpoints_[kDataIn].endpointAddress = bulk_in_ep | (USB_IN << 7);
  data_endpoints_[kDataOut].endpointAddress = bulk_out_ep | (USB_OUT << 7);
  interfaces_[0].interfaceNumber = comm_iface;
  interfaces_[1].interfaceNumber = data_iface;

  tx_queue_ = xQueueCreate(10, sizeof(void *));
  CHECK(tx_queue_);
  tx_done_ = xSemaphoreCreateBinary();
  CHECK(tx_done_);
  CHECK(xTaskCreate(CdcEcm::StaticTaskFunction, "cdc_ecm_task",
                    configMINIMAL_STACK_SIZE * 10, this, kUsbDeviceTaskPriority,
                    nullptr) == pdPASS);

  std::string usb_ip;
  if (!GetUsbIpAddress(&usb_ip) ||
      !ipaddr_aton(usb_ip.c_str(), &netif_ipaddr_)) {
    IP4_ADDR(&netif_ipaddr_, 10, 10, 10, 1);
  }
  IP4_ADDR(&netif_netmask_, 255, 255, 255, 0);
  IP4_ADDR(&netif_gw_, 0, 0, 0, 0);
  netifapi_netif_add(&netif_, &netif_ipaddr_, &netif_netmask_, &netif_gw_, this,
                     CdcEcm::StaticNetifInit, tcpip_input);
  netifapi_netif_set_default(&netif_);
  netifapi_netif_set_link_up(&netif_);
  netifapi_netif_set_up(&netif_);
  start_dhcp_server(netif_ipaddr_.addr);
}

void CdcEcm::TaskFunction(void *param) {
  while (true) {
    std::vector<uint8_t> *packet;
    if (xQueueReceive(tx_queue_, &packet, portMAX_DELAY) == pdTRUE) {
      TransmitFrame(packet->data(), packet->size());
      delete packet;
    }
  }
}

err_t CdcEcm::NetifInit(struct netif *netif) {
  netif->name[0] = 'u';
  netif->name[1] = 's';
  netif->output = etharp_output;
  netif->linkoutput = CdcEcm::StaticTxFunc;
  netif->mtu = 300;
  netif->hwaddr_len = 6;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

  netif->hwaddr[0] = 0x00;
  netif->hwaddr[1] = 0x1A;
  netif->hwaddr[2] = 0x11;
  netif->hwaddr[3] = 0xBA;
  netif->hwaddr[4] = 0xDF;
  netif->hwaddr[5] = 0xAD;

  return ERR_OK;
}

err_t CdcEcm::TxFunc(struct netif *netif, struct pbuf *p) {
  if (!link_up_) {
    return ERR_IF;
  }
  auto *packet = new std::vector<uint8_t>(p->tot_len);
  if (pbuf_copy_partial(p, packet->data(), p->tot_len, 0) != p->tot_len) {
    delete packet;
    return ERR_IF;
  }

  if (xQueueSendToBack(tx_queue_, &packet, 0) != pdTRUE) {
    delete packet;
    return ERR_IF;
  }

  return ERR_OK;
}

err_t CdcEcm::TransmitFrame(void *buffer, uint32_t length) {
  if (!link_up_ || length > sizeof(tx_buffer_)) {
    return ERR_IF;
  }
  std::memcpy(tx_buffer_, buffer, length);

  xSemaphoreTake(tx_done_, 0);
  if (USB_DeviceCdcEcmSend(class_handle_, tx_buffer_, length) !=
      kStatus_USB_Success) {
    return ERR_IF;
  }
  // Blocks while the host is not reading; TxFunc drops once the queue fills.
  // Closing the endpoint or a bus reset completes the transfer as cancelled.
  xSemaphoreTake(tx_done_, portMAX_DELAY);
  return ERR_OK;
}

err_t CdcEcm::ReceiveFrame(uint8_t *buffer, uint32_t length) {
  struct pbuf *frame = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
  if (!frame) {
    printf("Failed to allocate pbuf\r\n");
    return ERR_BUF;
  }
  pbuf_take(frame, buffer, length);
  err_t ret = netif_.input(frame, &netif_);
  if (ret != ERR_OK) {
    printf("tcpip_input() failed %d\r\n", ret);
    pbuf_free_callback(frame);
    return ERR_IF;
  }

  return ERR_OK;
}

void CdcEcm::SendNextNotification() {
  uint32_t length = 8;
  notify_buffer_[0] = 0xA1;  // class, interface, device to host
  notify_buffer_[2] = 0x00;
  notify_buffer_[3] = 0x00;
  notify_buffer_[4] = interfaces_[0].interfaceNumber;
  notify_buffer_[5] = 0x00;
  notify_buffer_[6] = 0x00;
  notify_buffer_[7] = 0x00;

  Notification after;
  switch (next_notification_) {
    case Notification::kNetworkConnection:
      notify_buffer_[1] = USB_DEVICE_CDC_ECM_NOTIF_NETWORK_CONNECTION;
      notify_buffer_[2] = 0x01;  // connected
      after = Notification::kConnectionSpeedChange;
      break;
    case Notification::kConnectionSpeedChange:
      notify_buffer_[1] = USB_DEVICE_CDC_ECM_NOTIF_CONNECTION_SPEED_CHANGE;
      notify_buffer_[6] = 0x08;
      // Downlink, then uplink, little-endian like the rest of USB.
      std::memcpy(&notify_buffer_[8], &kBitRate, sizeof(kBitRate));
      std::memcpy(&notify_buffer_[12], &kBitRate, sizeof(kBitRate));
      length = 16;
      after = Notification::kNone;
      break;
    default:
      return;
  }
  // Busy or closed: the pipe's completion callback retries.
  if (USB_DeviceCdcEcmNotify(class_handle_, notify_buffer_, length) ==
      kStatus_USB_Success) {
    next_notification_ = after;
  }
}

void CdcEcm::SetDataAlternate(uint8_t alternate) {
  link_up_ = (alternate == 1);
  if (!link_up_) {
    next_notification_ = Notification::kNone;
    return;
  }
  USB_DeviceCdcEcmRecv(class_handle_, rx_buffer_, sizeof(rx_buffer_));
  // Hosts hold the link down until told otherwise.
  next_notification_ = Notification::kNetworkConnection;
  SendNextNotification();
}

bool CdcEcm::HandleEvent(uint32_t event, void *param) {
  switch (event) {
    case kUSB_DeviceEventSetConfiguration:
      SetDataAlternate(0);
      break;
    case kUSB_DeviceEventSetInterface: {
      // Interface in the high byte, alternate in the low one.
      const uint16_t interface_alternate = *static_cast<uint16_t *>(param);
      if ((interface_alternate >> 8) == interfaces_[1].interfaceNumber) {
        SetDataAlternate(interface_alternate & 0xFF);
      }
      break;
    }
    default:
      DbgConsole_Printf("%s unhandled event %d\r\n", __PRETTY_FUNCTION__,
                        event);
      return false;
  }
  return true;
}

usb_status_t CdcEcm::Handler(uint32_t event, void *param) {
  auto *ep_cb =
      static_cast<usb_device_endpoint_callback_message_struct_t *>(param);
  const bool cancelled = ep_cb->length == USB_CANCELLED_TRANSFER_LENGTH;

  switch (event) {
    case kUSB_DeviceEcmEventRecvResponse:
      if (!cancelled && ep_cb->length >= sizeof(struct eth_hdr)) {
        ReceiveFrame(rx_buffer_, ep_cb->length);
      }
      // Fails when the endpoint has just been closed, which is the intent.
      USB_DeviceCdcEcmRecv(class_handle_, rx_buffer_, sizeof(rx_buffer_));
      break;
    case kUSB_DeviceEcmEventSendResponse:
      xSemaphoreGive(tx_done_);
      break;
    case kUSB_DeviceEcmEventNotifyResponse:
      if (cancelled && link_up_) {
        next_notification_ = Notification::kNetworkConnection;
      }
      SendNextNotification();
      break;
    default:
      DbgConsole_Printf("Unhandled ECM event: %d\r\n", event);
      return kStatus_USB_Error;
  }

  return kStatus_USB_Success;
}

}  // namespace coralmicro
