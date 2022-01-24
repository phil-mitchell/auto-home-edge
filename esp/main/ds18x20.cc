#include "autohome.h"

static const char *TAG = "sensor";
static const float DEFAULT_TEMPERATURE_THRESHOLD = 0.2;

static void dsTask( void *arg )
{
    DS18X20Sensor *sensor = (DS18X20Sensor*)arg;
    uint32_t notifiedValue = ulTaskNotifyTake( pdFALSE, 0 );

    while( !notifiedValue ) {
        sensor->read();
        notifiedValue = ulTaskNotifyTake( pdFALSE, sensor->getInterval() / portTICK_RATE_MS );
    }

    ESP_LOGI( TAG, "DS18X20 task finished\n" );
    vTaskDelete( NULL );
}

DS18X20Sensor::DS18X20Sensor( Zone &zone, const char *id )
    : Device( zone, id ), _pin( (gpio_num_t)-1 ), _addr( ds18x20_ANY ), _interval( 0 ), _task( NULL )
{
}

DS18X20Sensor::~DS18X20Sensor()
{
    setInterval( 0 );
}
 
esp_err_t DS18X20Sensor::init( gpio_num_t pin, ds18x20_addr_t addr )
{
    _pin = pin;
    _addr = addr;
    getZone().sendZoneLog( ESP_LOG_INFO, TAG, "DS18X20Sensor::init %d: %llx", _pin, _addr );

    if( !( BIT( _pin ) & VALID_DEVICE_PIN_MASK ) ) {
        getZone().sendZoneLog( ESP_LOG_ERROR, TAG, "Pin not available for device communication" );
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    if( _addr == ds18x20_ANY ) {
        ds18x20_addr_t addrs[8];
        for( int retries = 0; ret != ESP_OK && retries < 3; ++retries ) { 
            int count = ds18x20_scan_devices( _pin, addrs, 8 );
            if( count < 1 ) {
                getZone().sendZoneLog( ESP_LOG_ERROR, TAG, "Could not find any DS18X20 sensor on pin %d", _pin );
            } else if( count > 1 ) {
                getZone().sendZoneLog( ESP_LOG_ERROR, TAG, "Found multiple DS18X20 sensors on pin %d; specify a sensor address", _pin );
                for( int i = 0; i < count && i < 8; i++ ) {
                    getZone().sendZoneLog( ESP_LOG_ERROR, TAG, " - %llx", addrs[i] );
                }
            } else {
                ret = ESP_OK;
                _addr = addrs[0];
            }
        }
    } else {
        ret = ESP_OK;
    }

    return ret;
}

void DS18X20Sensor::setInterval( uint32_t interval )
{
    getZone().sendZoneLog( ESP_LOG_INFO, TAG, "DS18X20Sensor::setInterval %d (existing task %p)", interval, _task );

    if( _task != NULL ) {
        getZone().sendZoneLog( ESP_LOG_DEBUG, TAG, "Sending notification" );
        xTaskNotifyGive( _task );
        getZone().sendZoneLog( ESP_LOG_DEBUG, TAG, "Sent notification" );
        _task = NULL;
    }

    _interval = interval;
    if( _interval > 0 ) {
        xTaskCreate( &dsTask, "dsmonitor", 4096, this, 5, &_task );
        getZone().sendZoneLog( ESP_LOG_INFO, TAG, "DS18X20Sensor::setInterval %d (created task %p)", interval, _task );
    }
}

uint32_t DS18X20Sensor::getInterval() const
{
    return _interval;
}

void DS18X20Sensor::read()
{
    float temperature;
    float threshold = DEFAULT_TEMPERATURE_THRESHOLD;
    Zone &zone = getZone();

    getZone().sendZoneLog( ESP_LOG_INFO, TAG, "DS18X20Sensor::read %s starting", getId() );

    for( int i = 0; i < 3; ++i ) {
        esp_err_t err = ds18x20_measure_and_read( _pin, _addr, &temperature );
        if( !err ) {
            const DeviceCalibration *calibration = findCalibration( "temperature" );
            if( calibration ) {
                calibration->adjust( temperature );
                threshold = calibration->doubleThreshold();
            }
        
            getZone().sendZoneLog( ESP_LOG_INFO, TAG, "DS18X20Sensor::read %s got value %0.1f", getId(), temperature );

            zone.setValue( getId(), "temperature", temperature, "celsius", threshold );
            break;
        } else {
            getZone().sendZoneLog( ESP_LOG_ERROR, TAG, "DS18X20Sensor::read %s got error %d: %s", getId(), err, esp_err_to_name( err ) );
        }
    }
}

