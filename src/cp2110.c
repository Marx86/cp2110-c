/**************************************************************************
 * Copyright (c) 2015 - Gray Cat Labs - https://graycat.io
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **************************************************************************/

/**
 * @file cp2110.c
 * @author Alex Hiam - <alex@graycat.io>
 *
 * @brief A basic userspace driver for the CP2110 HID USB-UART IC.
 *
 */

#include <hidapi/hidapi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cp2110.h"


struct hid_device_info *CP2110_enumerate(void) {
  return hid_enumerate(CP2110_VID, CP2110_PID); 
}

CP2110_dev *CP2110_init(void) {
  CP2110_dev *handle;
  handle = hid_open(CP2110_VID, CP2110_PID, NULL);
  hid_set_nonblocking(handle, 1);
  return handle;
}

void CP2110_release(CP2110_dev *handle) {
    hid_close(handle);
    hid_exit();
}

int CP2110_uartEnabled(CP2110_dev *handle) {
  int ret;
  uint8_t buf[2];
  buf[0] = REPORT_GET_SET_UART_ENABLE;
  ret = hid_get_feature_report(handle, buf, sizeof(buf));
  if (ret) return buf[1];
  return -1;
}

int CP2110_enableUART(CP2110_dev *handle) {
  int ret;
  uint8_t buf[3];
  buf[0] = REPORT_GET_SET_UART_ENABLE;
  buf[1] = 1;
  ret = hid_send_feature_report(handle, buf, sizeof(buf));
  if (ret > 0) return 1;
  return ret;
}

int CP2110_disableUART(CP2110_dev *handle) {
  int ret;
  uint8_t buf[3];
  buf[0] = REPORT_GET_SET_UART_ENABLE;
  buf[1] = 0;
  ret = hid_send_feature_report(handle, buf, sizeof(buf));
  if (ret > 0) return 1;
  return ret;
}

int CP2110_purgeFIFO(CP2110_dev *handle, CP2110_fifo fifo) {
  int ret;
  uint8_t buf[2];
  buf[0] = REPORT_SET_PURGE_FIFOS;
  ret = hid_get_feature_report(handle, buf, sizeof(buf));
  if (ret) return buf[1];
  return -1;
}

int CP2110_getUARTConfig(CP2110_dev *handle, uint8_t *config) {
  int ret;
  uint8_t buf[9];
  buf[0] = REPORT_GET_SET_UART_CONFIG;
  ret = hid_get_feature_report(handle, buf, sizeof(buf));
  if (ret < 1) return ret;

  printf("baud: %d\n", (unsigned int) (buf[1]<<(8*3) | buf[2]<<(8*2) | buf[3]<<8 | buf[4]));
  printf("parity: %d\n", buf[5]);
  printf("flow control: %d\n", buf[6]);
  printf("data bits: %d\n", buf[7]);
  printf("stop bits: %d\n\n", buf[8]);

  memcpy(config, buf+1, 8);
  return 1;
}


int CP2110_setUARTConfig(CP2110_dev *handle, 
                         uint32_t baud,
                         CP2110_parity parity,
                         CP2110_flow_control flow_control,
                         CP2110_data_bits data_bits,
                         CP2110_stop_bits stop_bits) {
  int ret, i;
  uint8_t buf[9];
  buf[0] = REPORT_GET_SET_UART_CONFIG;
  
  if (baud < 300) baud = 300;
  else if (baud > 500000) baud = 500000;

  // Force MSB-first ordering:
  for (i=0; i<4; i++) {
    buf[i+1] = 0xff & (baud >> ((3-i)*8));
  }

  buf[5] = (uint8_t) parity;
  buf[6] = (uint8_t) flow_control;
  buf[7] = (uint8_t) data_bits;
  buf[8] = (uint8_t) stop_bits;

  ret = hid_send_feature_report(handle, buf, sizeof(buf));
  if (ret > 0) return 1;
  return ret;
}


int CP2110_write(CP2110_dev *handle, char *tx_buf, int len) {
  int ret, index, n_sent;
  uint8_t buf[REPORT_DATA_RX_TX_MAX+1];
  n_sent = 0;
  index = 0;
  while (len >= REPORT_DATA_RX_TX_MAX) {
    buf[0] = REPORT_DATA_RX_TX_MAX;
    memcpy(buf+1, tx_buf+index, REPORT_DATA_RX_TX_MAX);
    ret = hid_write(handle, buf, sizeof(buf));
    if (ret < 0) return ret;
    n_sent += ret-1;
    if (ret < REPORT_DATA_RX_TX_MAX+1) {
      // Not all bytes were written, assume error and return
      return n_sent;
    }
    index += REPORT_DATA_RX_TX_MAX;
    len -= REPORT_DATA_RX_TX_MAX;
  }
  if (len) {
    buf[0] = len;
    memcpy(buf+1, tx_buf+index, len);
    ret = hid_write(handle, buf, len+1);
    if (ret < 0) return ret;
    n_sent += ret-1;
  }
  return n_sent;
}


int CP2110_read(CP2110_dev *handle, char *rx_buf, int len) {
  int ret, index, n_read;
  uint8_t buf[REPORT_DATA_RX_TX_MAX+1];
  n_read = 0;
  index = 0;

  while (len >= REPORT_DATA_RX_TX_MAX) {
    buf[0] = REPORT_DATA_RX_TX_MAX;
    ret = hid_read(handle, buf, sizeof(buf));
    if (ret < 0) return ret;
    n_read += ret ? ret-1 : 0;

    if (ret) memcpy(rx_buf+index, buf, n_read);

    if (ret < REPORT_DATA_RX_TX_MAX) {
      // Not all bytes were written, assume error and return
      return n_read;
    }
    index += REPORT_DATA_RX_TX_MAX;
    len -= REPORT_DATA_RX_TX_MAX;
  }
  if (len) {
    buf[0] = len;
    ret = hid_read(handle, buf, len+1);
    if (ret < 0) return ret;
    n_read += ret ? ret-1 : 0;

    if (ret) memcpy(rx_buf+index, buf, n_read);
  }
  return n_read;
}


int CP2110_getGPIOPin(CP2110_dev *handle, uint8_t pin) {
  int ret;
  uint8_t buf[3];
  uint16_t values, mask;
  // Only GPIO pins 0-9 available:
  if (pin > 9) return -1;
  buf[0] = REPORT_GET_GPIO_VALUES;
  ret = hid_get_feature_report(handle, buf, sizeof(buf));
  if (ret <= 0) return -1;

  values = (buf[1] << 8) | buf[2];
  switch (pin) {
    case 0:
      mask = CP2110_GPIO0_MASK;
      break;
    case 1:
      mask = CP2110_GPIO1_MASK;
      break;
    case 2:
      mask = CP2110_GPIO2_MASK;
      break;
    case 3:
      mask = CP2110_GPIO3_MASK;
      break;
    case 4:
      mask = CP2110_GPIO4_MASK;
      break;
    case 5:
      mask = CP2110_GPIO5_MASK;
      break;
    case 6:
      mask = CP2110_GPIO6_MASK;
      break;
    case 7:
      mask = CP2110_GPIO7_MASK;
      break;
    case 8:
      mask = CP2110_GPIO8_MASK;
      break;
    case 9:
      mask = CP2110_GPIO9_MASK;
      break;
    default:
      // This shouldn't ever happen, but just in case:
      return -1;
      break;
  }
  if (values & mask) return 1;
  else return 0;
}

int CP2110_setGPIOPin(CP2110_dev *handle, uint8_t pin, uint8_t state) {
  int ret;
  uint8_t buf[5];
  uint16_t values, mask;
  // Only GPIO pins 0-9 available:
  if (pin > 9) return -1;
  buf[0] = REPORT_GET_GPIO_VALUES;
  ret = hid_get_feature_report(handle, buf, 3);
  if (ret <= 0) return -1;

  values = (buf[1] << 8) | buf[2];
  switch (pin) {
    case 0:
      mask = CP2110_GPIO0_MASK;
      break;
    case 1:
      mask = CP2110_GPIO1_MASK;
      break;
    case 2:
      mask = CP2110_GPIO2_MASK;
      break;
    case 3:
      mask = CP2110_GPIO3_MASK;
      break;
    case 4:
      mask = CP2110_GPIO4_MASK;
      break;
    case 5:
      mask = CP2110_GPIO5_MASK;
      break;
    case 6:
      mask = CP2110_GPIO6_MASK;
      break;
    case 7:
      mask = CP2110_GPIO7_MASK;
      break;
    case 8:
      mask = CP2110_GPIO8_MASK;
      break;
    case 9:
      mask = CP2110_GPIO9_MASK;
      break;
    default:
      // This shouldn't ever happen, but just in case:
      return -1;
      break;
  }

  if (state) values |= mask;
  else values &= ~mask;

  buf[0] = REPORT_SET_GPIO_VALUES;
  buf[1] = (values>>8) & 0xff;
  buf[2] = values & 0xff;
  buf[3] = (mask>>8) & 0xff;
  buf[4] = mask & 0xff;
  ret = hid_send_feature_report(handle, buf, sizeof(buf));
  if (ret <= 0) return -1;
  return 0;
}

int CP2110_setGPIOConfig(CP2110_dev *handle, uint8_t pin, uint8_t mode) {
  int ret;
  uint8_t buf[11];

  if (pin > 9 || (pin > 5 && mode == GPIO_ALTERNATE)) {
    return -1;
  }

  buf[0] = REPORT_GET_SET_GPIO_CONFIG;
  buf[pin+1] = mode;

  ret = hid_send_feature_report(handle, buf, sizeof(buf));
  if (ret <= 0) return -1;
  return 0;
}

