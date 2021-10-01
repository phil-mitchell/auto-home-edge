#include "autohome.h"
#include <string.h>
#include <algorithm>

static const char *TAG = "mqtt client";

static void event_handler( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
    MQTTClient *client = (MQTTClient*)arg;
    client->handleEvent( (esp_mqtt_event_handle_t)event_data );
}

MQTTData::MQTTData()
    : topic( NULL ), data( NULL ), data_len( 0 )
{
}

MQTTData::~MQTTData()
{
    reset();
}

void MQTTData::reset()
{
    if( topic ) {
        free( topic );
        topic = NULL;
    }

    if( data ) {
        free( data );
        data = NULL;
    }

    data_len = 0;
}

void MQTTData::append( esp_mqtt_event_handle_t event )
{
    if( !data ) {
        topic = strndup( event->topic, event->topic_len );
        data = (char *)malloc( event->total_data_len );
        data_len = 0;
    }
    memcpy( data + data_len, event->data, event->data_len );
    data_len += event->data_len;
}

MQTTClient::MQTTClient( Network &network )
    : _network( network ), _client( NULL )
{
}

MQTTClient::~MQTTClient()
{
    if( _client != NULL ) {
        esp_mqtt_client_disconnect( _client );
        esp_mqtt_client_stop( _client );
        esp_mqtt_client_destroy( _client );
    }
}

void MQTTClient::addZone( const char *homeId, const char *zoneId )
{
    ZoneList::iterator it = std::find_if(
        _zones.begin(), _zones.end(),
        [homeId, zoneId](const Zone &zone) {
            return zone.matches( (const char*)homeId, (const char*)zoneId );
        });

    if( it == _zones.end() ) {
        _zones.emplace_front( *this, homeId, zoneId );

        char subscription[128];
        snprintf( subscription, sizeof( subscription ), "homes/%s/zones/%s/devices/+/config", homeId, zoneId );
        int msg_id = esp_mqtt_client_subscribe( _client, subscription, 1 );
        ESP_LOGI( TAG, "sent subscribe successful, msg_id=%d", msg_id );
    }
}

void MQTTClient::removeZone( const char *homeId, const char *zoneId )
{
    ZoneList::iterator it = std::find_if(
        _zones.begin(), _zones.end(),
        [homeId, zoneId](const Zone &zone) {
            return zone.matches( (const char*)homeId, (const char*)zoneId );
        });

    if( it != _zones.end() ) {
        _zones.erase( it );

        char subscription[128];
        snprintf( subscription, sizeof( subscription ), "homes/%s/zones/%s/devices/config", homeId, zoneId );
        int msg_id = esp_mqtt_client_unsubscribe( _client, subscription );
        ESP_LOGI( TAG, "sent subscribe successful, msg_id=%d", msg_id );
    }
}

void MQTTClient::publish( const char *topic, const char *message, int qos, bool retain )
{
    ESP_LOGI( TAG, "publish to %s => %s", topic, message );
    esp_mqtt_client_publish( _client, topic, message, 0, qos, retain ? 1 : 0 );
}
    
void MQTTClient::connect( const char *brokerUrl )
{
    if( brokerUrl ) {
        _mqtt_config.uri = brokerUrl;
    }

    if( _client == NULL ) {
        _client = esp_mqtt_client_init( &_mqtt_config );
        esp_mqtt_client_register_event( _client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, event_handler, this );
        esp_mqtt_client_start( _client );

    } else {
        esp_mqtt_client_reconnect( _client );
    }

    ESP_LOGI(TAG, "MQTTClient::connect finished.");
}

void MQTTClient::handleEvent( esp_mqtt_event_handle_t event )
{
    int msg_id;
    char *topic = NULL;
    char *data = NULL;
    char *topicParts[7];
    size_t numTopicParts = 0;
    memset( topicParts, 0, sizeof( topicParts ) );

    cJSON *json = NULL;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

            msg_id = esp_mqtt_client_subscribe( _client, "homes/+/config", 1 );
            ESP_LOGI(TAG, "sent subscribe to home config successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe( _client, "homes/+/zones/+/config", 1 );
            ESP_LOGI(TAG, "sent subscribe to zone config successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe( _client, "homes/+/zones/+/devices/+/+", 0 );
            ESP_LOGI(TAG, "sent subscribe to device events successful, msg_id=%d", msg_id);

            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);

            _data.append( event );

            if( _data.data_len == event->total_data_len ) {
                data = _data.data;

                if( data != NULL ) {
                    json = cJSON_Parse( data );
                }
            
                if( json != NULL ) {
                    ESP_LOGI( TAG, "Received JSON data" );

                    topic = _data.topic;
                    if( topic != NULL ) {
                        for( numTopicParts = 0; numTopicParts < 7 && topic != NULL; ++numTopicParts ) {
                            topicParts[numTopicParts] = strsep( &topic, "/" );
                        }

                        if( numTopicParts == 5 && strcmp( topicParts[0], "homes" ) == 0 &&
                            strcmp( topicParts[2], "zones" ) == 0 && strcmp( topicParts[4], "config" ) == 0 ) {
                            cJSON *controller = cJSON_GetObjectItemCaseSensitive( json, "controller" );
                            ESP_LOGI("CONTROLLER=[%s]", controller->valuestring );
                            if( controller && cJSON_IsString( controller ) && _network.matchesMacAddress( controller->valuestring ) ) {
                                ESP_LOGI("Adding zone %s/%s", topicParts[1], topicParts[3] );
                                addZone( topicParts[1], topicParts[3] );
                            } else {
                                ESP_LOGI("Removing zone %s/%s", topicParts[1], topicParts[3] );
                                removeZone( topicParts[1], topicParts[3] );
                            }
                        }
                        
                        for( ZoneList::iterator zone = _zones.begin(); zone != _zones.end(); ++zone ) {
                            zone->configureZoneJSON( (const char **)topicParts, numTopicParts, json );
                        }
                    }
                    cJSON_Delete( json );
                }
                _data.reset();
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}
