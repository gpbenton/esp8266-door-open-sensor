/* main.c -- MQTT client example
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "wifi.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"

#define FPM_SLEEP_MAX_TIME 0xFFFFFFF

static bool closed_sent = false;
static char topic[32];
static const int hold_pin = GPIO_ID_PIN(0); // GPIO pin to hold CH_PD high until we shut down
static const int detect_pin = GPIO_ID_PIN(2); // GPIO pin to detect door closed

MQTT_Client mqttClient;

static void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status)
{
  if (status == STATION_GOT_IP) {
    MQTT_Connect(&mqttClient);
  } else {
    MQTT_Disconnect(&mqttClient);
  }
}

/**
 * Callback when MQTT connected after door closed.
 */
static void ICACHE_FLASH_ATTR doorClosedConnectedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;

  MQTT_Publish(client, topic, "closed", 6, 0, 0);
  closed_sent = true;
}

/**
 * Callback when door closed MQTT message has been sent.
 */
static void ICACHE_FLASH_ATTR doorClosedPublishedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;

  INFO("%s", __FUNC__);
  if (closed_sent && QUEUE_IsEmpty(&(client->msgQueue))) {
    // Shut down by pulling GPIO0 low
    GPIO_OUTPUT_SET(hold_pin, 0);
  }

}

/**
 * Callback when detect_pin goes low indicating door closed.
 */
static void ICACHE_FLASH_ATTR door_closed_callback() 
{
  INFO("door_closed_callback");

  wifi_fpm_close();

  MQTT_OnPublished(&mqttClient, doorClosedPublishedCb);
  MQTT_OnConnected(&mqttClient, doorClosedConnectedCb);
  wifi_set_opmode(STATION_MODE);
  wifi_station_connect();
}

/**
 * Callback when door open detected.
 */
static void ICACHE_FLASH_ATTR doorOpenConnectedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;

  INFO("MQTT: Connected\r\n");

  MQTT_Publish(client, topic, "open", 4, 0, 0);

  if (GPIO_INPUT_GET(detect_pin) == 0) {
    // already low, so publish a close
    MQTT_Publish(client, topic, "closed", 6, 0, 0);
    closed_sent = true;
  }
}

/**
 * Callback after door open (and possibly closed) MQTT messages sent
 */
static void ICACHE_FLASH_ATTR doorOpenPublishedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;
  INFO("MQTT: Published\r\n");
  if (closed_sent) {
    if (QUEUE_IsEmpty(&(client->msgQueue))) {
      // Shut down by pulling GPIO0 low
      GPIO_OUTPUT_SET(hold_pin, 0);
    }
  } else {
    // open has been published, so go to sleep and wait for
    // the detect_pin to go low to wake us up and send
    // a closed message
    wifi_station_disconnect();
    wifi_set_opmode(NULL_MODE);   // set WiFi mode to null mode.
    wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);      // light sleep
    wifi_fpm_open();              // enable force sleep
    //wifi_enable_gpio_wakeup(detect_pin, GPIO_PIN_INTR_LOLEVEL);
    gpio_pin_wakeup_enable(detect_pin, GPIO_PIN_INTR_LOLEVEL);
    wifi_fpm_set_wakeup_cb(door_closed_callback);   // Set wakeup callback 
    wifi_fpm_do_sleep(FPM_SLEEP_MAX_TIME);
  }

}

void ICACHE_FLASH_ATTR print_info()
{
  INFO("\r\n\r\n[INFO] BOOTUP...\r\n");
  INFO("[INFO] SDK: %s\r\n", system_get_sdk_version());
  INFO("[INFO] Chip ID: %08X\r\n", system_get_chip_id());
  INFO("[INFO] Memory info:\r\n");
  system_print_meminfo();

  INFO("[INFO] -------------------------------------------\n");
  INFO("[INFO] Build time: %s\n", BUID_TIME);
  INFO("[INFO] -------------------------------------------\n");

}


static void ICACHE_FLASH_ATTR app_init(void)
{
  char client_id[128];  
  uart_init(BIT_RATE_115200, BIT_RATE_115200);
  print_info();
  MQTT_InitConnection(&mqttClient, MQTT_HOST, MQTT_PORT, DEFAULT_SECURITY);

  os_sprintf(client_id, "door_%ld", system_get_chip_id());
  if ( !MQTT_InitClient(&mqttClient, client_id, MQTT_USER, MQTT_PASS, MQTT_KEEPALIVE, MQTT_CLEAN_SESSION) )
  {
    INFO("Failed to initialize properly. Check MQTT version.\r\n");
    return;
  }
  MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
  MQTT_OnConnected(&mqttClient, doorOpenConnectedCb);
  MQTT_OnPublished(&mqttClient, doorOpenPublishedCb);

  WIFI_Connect(STA_SSID, STA_PASS, wifiConnectCb);
}

void user_init(void)
{
  // Set GPIO 0 & 2 to GPIO Function
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2); 
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0); 
  // Set GPIO0 high to stop the chip resetting
  GPIO_OUTPUT_SET(hold_pin, 1);
  // Set GPIO2 as input to detect door closing
  GPIO_DIS_OUTPUT(detect_pin);

  os_sprintf(topic, "sensor/%ld/door", system_get_chip_id());

  system_init_done_cb(app_init);
}
/* vim: set ts=2 sw=2: */
