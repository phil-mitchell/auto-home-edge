#include "autohome.h"
#include <string.h>

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static void event_handler( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
    Network *network = (Network*)arg;
    network->handleEvent( event_base, event_id, event_data );
}

Network::Network( Flasher &flasher )
    : _event_group( xEventGroupCreate() ), _flasher( flasher )
{
}

Network::~Network()
{
    ESP_ERROR_CHECK( esp_event_handler_unregister( IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler ) );
    ESP_ERROR_CHECK( esp_event_handler_unregister( WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler ) );
    vEventGroupDelete( _event_group );
}

void Network::init()
{
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    tcpip_adapter_init();

    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    if( esp_base_mac_addr_get( _mac ) == ESP_ERR_INVALID_MAC ) {
        esp_efuse_mac_get_default( _mac );
    }

    ESP_LOGI(TAG, "Got MAC Address %02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5]);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init( &cfg ) );

    ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_STA ) );

    ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this ) );
    ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this ) );
}

bool Network::matchesMacAddress( const char *mac ) const
{
    char macstr[18];
    snprintf( macstr, sizeof( macstr ), "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", _mac[0], _mac[1], _mac[2], _mac[3], _mac[4], _mac[5] );
    macstr[ sizeof(macstr) - 1 ] = '\0';

    return strcasecmp( macstr, mac ) == 0;
}

void Network::connect( const char *ssid, const char *password, int retries )
{
    _max_retries = retries;
    _retries = _max_retries;

    strncpy( (char*)_wifi_config.sta.ssid, ssid, sizeof( _wifi_config.sta.ssid ) - 1 );
    strncpy( (char*)_wifi_config.sta.password, password, sizeof( _wifi_config.sta.password ) - 1 );

    if( strlen( (char *)_wifi_config.sta.password ) ) {
        _wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK( esp_wifi_set_config( ESP_IF_WIFI_STA, &_wifi_config ) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    ESP_LOGI(TAG, "Network::connect finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(
        _event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY );

    if( bits & WIFI_CONNECTED_BIT ) {
        ESP_LOGI( TAG, "connected to ap SSID:%s password:%s", ssid, password);
    } else if( bits & WIFI_FAIL_BIT ) {
        ESP_LOGI( TAG, "Failed to connect to SSID:%s, password:%s", ssid, password );
    } else {
        ESP_LOGE( TAG, "UNEXPECTED EVENT");
    }
}

void Network::handleEvent( esp_event_base_t event_base, int32_t event_id, void* event_data )
{
    if( event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START ) {
        _flasher.setPattern( 100, 200 );
        esp_wifi_connect();
    } else if( event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED ) {
        _flasher.setPattern( 100, 200 );
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGI( TAG, "disconnected from %s: %d", event->ssid, event->reason );
        if( _retries > 0 ) {
            esp_wifi_connect();
            _retries--;
            ESP_LOGI( TAG, "retry to connect to the AP" );
        } else {
            xEventGroupSetBits( _event_group, WIFI_FAIL_BIT );
        }
    } else if( event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP ) {
        _flasher.setPattern( 1, 0 );
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI( TAG, "got ip:%s", ip4addr_ntoa( &event->ip_info.ip ) );
        _retries = _max_retries;
        xEventGroupSetBits( _event_group, WIFI_CONNECTED_BIT );
    }
}
