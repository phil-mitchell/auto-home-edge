/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "autohome.h"

#define AUTOHOME_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define AUTOHOME_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define AUTOHOME_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

static Flasher flasher( GPIO_NUM_2 );
static Network network( flasher );
static MQTTClient mqttClient( network );

extern "C" {
    void app_main();
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());

    flasher.init();
    flasher.setPattern( 500, 500 );

    network.init();
    network.connect( CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD, CONFIG_ESP_MAXIMUM_RETRY );

    setenv( "TZ", "EST5EDT", 1 );
    tzset();

    sntp_setoperatingmode( SNTP_OPMODE_POLL );
    sntp_setservername( 0, "pool.ntp.org" );
    sntp_init();

    mqttClient.connect( CONFIG_MQTT_BROKER_URL );
}
