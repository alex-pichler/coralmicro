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

#ifndef LIBS_CDC_ECM_CDC_ECM_H_
#define LIBS_CDC_ECM_CDC_ECM_H_

#include <atomic>
#include <map>
#include <string>

/* clang-format off */
#include "libs/usb/descriptors.h"
#include "third_party/freertos_kernel/include/FreeRTOS.h"
#include "third_party/freertos_kernel/include/queue.h"
#include "third_party/freertos_kernel/include/semphr.h"
#include "third_party/nxp/rt1176-sdk/middleware/lwip/src/include/lwip/netifapi.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/device/usb_device.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/include/usb.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/output/source/device/class/usb_device_class.h"  // Must be above other class headers.
#include "libs/nxp/rt1176-sdk/usb_device_cdc_ecm.h"
/* clang-format on */

namespace coralmicro {

// USB Ethernet (CDC-ECM) function with an lwIP netif and a DHCP server on it.
//
// ECM rather than EEM, for macOS. AppleUSBEEM 5.0.0 (macOS 13.7) setLength()s
// each of its 8 output buffers to the transfer size and never restores it, so
// once all have carried an ARP no frame over 42 bytes leaves the host.
class CdcEcm {
 public:
  CdcEcm() = default;
  CdcEcm(const CdcEcm &) = delete;
  CdcEcm &operator=(const CdcEcm &) = delete;
  void Init(uint8_t interrupt_in_ep, uint8_t bulk_in_ep, uint8_t bulk_out_ep,
            uint8_t comm_iface, uint8_t data_iface, uint8_t mac_string);
  const usb_device_class_config_struct_t &config_data() const {
    return config_;
  }
  const void *descriptor_data() const { return &descriptor_; }
  size_t descriptor_data_size() const { return sizeof(descriptor_); }
  void SetClassHandle(class_handle_t class_handle) {
    handle_map_[class_handle] = this;
    class_handle_ = class_handle;
  }
  bool HandleEvent(uint32_t event, void *param);

  // MAC address the host gives its end of the link, as the 12 hex digits the
  // iMACAddress string descriptor carries.
  static std::string HostMacString();

 private:
  static constexpr uint8_t kCommInterface = 2;
  static constexpr uint8_t kDataInterface = 3;
  static constexpr uint8_t kNotifyEndpoint = 4;
  static constexpr uint8_t kBulkInEndpoint = 5;
  static constexpr uint8_t kBulkOutEndpoint = 6;
  static constexpr uint8_t kMacString = 4;
  static constexpr uint16_t kMaxFrameSize = 1514;

  enum class Notification {
    kNone,
    kNetworkConnection,
    kConnectionSpeedChange,
  };

  static std::map<class_handle_t, CdcEcm *> handle_map_;
  static usb_status_t StaticHandler(class_handle_t class_handle, uint32_t event,
                                    void *param) {
    return handle_map_[class_handle]->Handler(event, param);
  }
  usb_status_t Handler(uint32_t event, void *param);
  void SetDataAlternate(uint8_t alternate);
  void SendNextNotification();

  // LwIP hooks
  static err_t StaticNetifInit(struct netif *netif) {
    return static_cast<CdcEcm *>(netif->state)->NetifInit(netif);
  }
  err_t NetifInit(struct netif *netif);

  static err_t StaticTxFunc(struct netif *netif, struct pbuf *p) {
    return static_cast<CdcEcm *>(netif->state)->TxFunc(netif, p);
  }
  err_t TxFunc(struct netif *netif, struct pbuf *p);

  static void StaticTaskFunction(void *param) {
    static_cast<CdcEcm *>(param)->TaskFunction(param);
  }
  void TaskFunction(void *param);

  err_t TransmitFrame(void *buffer, uint32_t length);
  err_t ReceiveFrame(uint8_t *buffer, uint32_t length);

  usb_device_endpoint_struct_t comm_endpoints_[1] = {
      {
          .endpointAddress = 0,  // set in Init
          .transferType = USB_ENDPOINT_INTERRUPT,
          .maxPacketSize = 16,
          .interval = 0,
      },
  };
  usb_device_endpoint_struct_t data_endpoints_[2] = {
      {
          .endpointAddress = 0,  // set in Init
          .transferType = USB_ENDPOINT_BULK,
          .maxPacketSize = 512,
          .interval = 0,
      },
      {
          .endpointAddress = 0,  // set in Init
          .transferType = USB_ENDPOINT_BULK,
          .maxPacketSize = 512,
          .interval = 0,
      },
  };
  usb_device_interface_struct_t comm_interface_[1] = {
      {
          .alternateSetting = 0,
          .endpointList =
              {
                  .count = ARRAY_SIZE(comm_endpoints_),
                  .endpoint = comm_endpoints_,
              },
          .classSpecific = nullptr,
      },
  };
  usb_device_interface_struct_t data_interface_[2] = {
      {
          .alternateSetting = 0,
          .endpointList =
              {
                  .count = 0,
                  .endpoint = nullptr,
              },
          .classSpecific = nullptr,
      },
      {
          .alternateSetting = 1,
          .endpointList =
              {
                  .count = ARRAY_SIZE(data_endpoints_),
                  .endpoint = data_endpoints_,
              },
          .classSpecific = nullptr,
      },
  };
  usb_device_interfaces_struct_t interfaces_[2] = {
      {
          .classCode = USB_DEVICE_CDC_ECM_COMM_CLASS_CODE,
          .subclassCode = USB_DEVICE_CDC_ECM_COMM_SUBCLASS_CODE,
          .protocolCode = 0x00,
          .interfaceNumber = 0,  // set in Init
          .interface = comm_interface_,
          .count = ARRAY_SIZE(comm_interface_),
      },
      {
          .classCode = USB_DEVICE_CDC_ECM_DATA_CLASS_CODE,
          .subclassCode = 0x00,
          .protocolCode = 0x00,
          .interfaceNumber = 0,  // set in Init
          .interface = data_interface_,
          .count = ARRAY_SIZE(data_interface_),
      },
  };
  usb_device_interface_list_t interface_list_[1] = {
      {
          .count = ARRAY_SIZE(interfaces_),
          .interfaces = interfaces_,
      },
  };
  usb_device_class_struct_t class_struct_{
      .interfaceList = interface_list_,
      .type = kUSB_DeviceClassTypeEcm,
      .configurations = ARRAY_SIZE(interface_list_),
  };
  usb_device_class_config_struct_t config_{
      .classCallback = StaticHandler,
      .classHandle = nullptr,
      .classInfomation = &class_struct_,
  };
  static constexpr CdcEcmClassDescriptor descriptor_ = {
      .iad =
          {
              .length = sizeof(InterfaceAssociationDescriptor),
              .descriptor_type = 0x0B,
              .first_interface = kCommInterface,
              .interface_count = 2,
              .function_class = 0x02,
              .function_subclass = 0x06,
              .function_protocol = 0x00,
              .interface = 0,
          },
      .comm_iface =
          {
              .length = sizeof(InterfaceDescriptor),
              .descriptor_type = 0x04,
              .interface_number = kCommInterface,
              .alternate_setting = 0,
              .num_endpoints = 1,
              .interface_class = 0x02,
              .interface_subclass = 0x06,
              .interface_protocol = 0x00,
              .interface = 0,
          },
      .hdr_fd =
          {
              .length = sizeof(CdcHeaderFunctionalDescriptor),
              .descriptor_type = 0x24,
              .descriptor_subtype = 0x00,
              .cdc = 0x0110,
          },
      .union_fd =
          {
              .function_length = sizeof(CdcUnionFunctionalDescriptor),
              .descriptor_type = 0x24,
              .descriptor_subtype = 0x06,
              .controller_iface = kCommInterface,
              .peripheral_iface0 = kDataInterface,
          },
      .ethernet_fd =
          {
              .function_length = sizeof(CdcEthernetFunctionalDescriptor),
              .descriptor_type = 0x24,
              .descriptor_subtype = 0x0F,
              .mac_address = kMacString,
              .ethernet_statistics = 0,
              .max_segment_size = kMaxFrameSize,
              .number_mc_filters = 0,
              .number_power_filters = 0,
          },
      .notify_ep =
          {
              .length = sizeof(EndpointDescriptor),
              .descriptor_type = 0x05,
              .endpoint_address = kNotifyEndpoint | 0x80,
              .attributes = 0x03,
              .max_packet_size = 16,
              .interval = 9,
          },
      .data_iface_idle =
          {
              .length = sizeof(InterfaceDescriptor),
              .descriptor_type = 0x04,
              .interface_number = kDataInterface,
              .alternate_setting = 0,
              .num_endpoints = 0,
              .interface_class = 0x0A,
              .interface_subclass = 0x00,
              .interface_protocol = 0x00,
              .interface = 0,
          },
      .data_iface =
          {
              .length = sizeof(InterfaceDescriptor),
              .descriptor_type = 0x04,
              .interface_number = kDataInterface,
              .alternate_setting = 1,
              .num_endpoints = 2,
              .interface_class = 0x0A,
              .interface_subclass = 0x00,
              .interface_protocol = 0x00,
              .interface = 0,
          },
      .in_ep = {.length = sizeof(EndpointDescriptor),
                .descriptor_type = 0x05,
                .endpoint_address = kBulkInEndpoint | 0x80,
                .attributes = 0x02,
                .max_packet_size = 512,
                .interval = 0},
      .out_ep = {.length = sizeof(EndpointDescriptor),
                 .descriptor_type = 0x05,
                 .endpoint_address = kBulkOutEndpoint,
                 .attributes = 0x02,
                 .max_packet_size = 512,
                 .interval = 0},
  };

  // Bulk OUT ends on a short packet, so ask for more than one frame holds.
  uint8_t rx_buffer_[3 * 512];
  uint8_t tx_buffer_[kMaxFrameSize];
  uint8_t notify_buffer_[16];
  QueueHandle_t tx_queue_;
  SemaphoreHandle_t tx_done_;
  class_handle_t class_handle_ = nullptr;

  // Set once the host selects data alternate 1.
  std::atomic<bool> link_up_{false};
  Notification next_notification_ = Notification::kNone;

  ip4_addr_t netif_ipaddr_, netif_netmask_, netif_gw_;
  struct netif netif_;
};

}  // namespace coralmicro

#endif  // LIBS_CDC_ECM_CDC_ECM_H_
