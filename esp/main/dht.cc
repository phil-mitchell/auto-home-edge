#include "autohome.h"

#include <math.h>

static const char *TAG = "sensor";
static const float DEFAULT_TEMPERATURE_THRESHOLD = 0;
static const float DEFAULT_HUMIDITY_THRESHOLD = 5;
static const float DEFAULT_HUMIDEX_THRESHOLD = 0;

static void dhtTask( void *arg )
{
    DHTSensor *sensor = (DHTSensor*)arg;
    uint32_t notifiedValue = ulTaskNotifyTake( pdFALSE, 0 );

    while( !notifiedValue ) {
        sensor->read();
        notifiedValue = ulTaskNotifyTake( pdFALSE, sensor->getInterval() / portTICK_RATE_MS );
    }

    ESP_LOGI( TAG, "DHT task finished\n" );
    vTaskDelete( NULL );
}

DHTSensor::DHTSensor( Zone &zone, const char *id )
    : Device( zone, id ), _pin( (gpio_num_t)-1 ), _interval( 0 ), _task( NULL )
{
}

DHTSensor::~DHTSensor()
{
    setInterval( 0 );
}
 
esp_err_t DHTSensor::init( gpio_num_t pin, dht_sensor_type_t type, bool pull_up )
{
    _type = type;
    _pin = pin;

    _config.pin_bit_mask = BIT( _pin );
    _config.mode = GPIO_MODE_OUTPUT_OD;
    _config.pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    _config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    _config.intr_type = GPIO_INTR_DISABLE;

    ESP_LOGI( TAG, "DHTSensor::init %d: %x", _pin, _config.pin_bit_mask );

    if( !( _config.pin_bit_mask & VALID_DEVICE_PIN_MASK ) ) {
        ESP_LOGE( TAG, "Pin not available for device communication" );
        return ESP_FAIL;
    }

    esp_err_t res = gpio_config( &_config );
    if( res != ESP_OK ) {
        return res;
    }

    gpio_set_level( _pin, 1 );
    return ESP_OK;
}

void DHTSensor::setInterval( uint32_t interval )
{
    ESP_LOGI( TAG, "DHTSensor::setInterval %d (existing task %p)", interval, _task );

    if( _task != NULL ) {
        ESP_LOGI( TAG, "Sending notification\n" );
        xTaskNotifyGive( _task );
        ESP_LOGI( TAG, "Sent notification\n" );
        _task = NULL;
    }

    _interval = interval;
    if( _interval > 0 ) {
        xTaskCreate( &dhtTask, "dhtmonitor", 4096, this, 5, &_task );
        ESP_LOGI( TAG, "DHTSensor::setInterval %d (created task %p)", interval, _task );
    }
}

uint32_t DHTSensor::getInterval() const
{
    return _interval;
}

void DHTSensor::read()
{
    float temperature;
    float humidity;
    float threshold;
    Zone &zone = getZone();

    esp_err_t err = dht_read_float_data( _type, _pin, &humidity, &temperature );
    if( !err ) {
        const DeviceCalibration *calibration = findCalibration( "temperature" );
        if( calibration ) {
            temperature = calibration->adjust( temperature );
            threshold = calibration->doubleThreshold();
        } else {
            threshold = DEFAULT_TEMPERATURE_THRESHOLD;
        }
        
        zone.setValue( getId(), "temperature", temperature, "celsius", threshold );
    }

    if( !err ) {
        const DeviceCalibration *calibration = findCalibration( "humidity" );
        if( calibration ) {
            humidity = calibration->adjust( humidity );
            threshold = calibration->doubleThreshold();
        } else {
            threshold = DEFAULT_HUMIDITY_THRESHOLD;
        }
        
        zone.setValue( getId(), "humidity", humidity, "percent", threshold );
    }

    if( !err ) {
        float humidex = temperature;
        float t = 7.5 * temperature / ( 237.7 + temperature );
        float et = pow( 10, t );
        float e = 6.112 * et * ( humidity / 100 );

        if( e > 10 ) {
            humidex = round( ( temperature + ( ( e - 10 ) * 5.0 / 9.0 ) ) * 10.0 ) / 10.0;
        }
        
        const DeviceCalibration *calibration = findCalibration( "humidex" );
        if( calibration ) {
            humidex = calibration->adjust( humidex );
            threshold = calibration->doubleThreshold();
        } else {
            threshold = DEFAULT_HUMIDEX_THRESHOLD;
        }
        
        zone.setValue( getId(), "humidex", humidex, "", threshold );
    }
}
