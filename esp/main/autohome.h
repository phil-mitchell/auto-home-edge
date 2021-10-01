#ifndef __AUTOHOME_H__
#define __AUTOHOME_H__

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"


#include "driver/gpio.h"

#include "mqtt_client.h"
}

#include <cJSON.h>
#include <dht.h>
#include <ds18x20.h>
#include <list>

static const uint32_t VALID_DEVICE_PIN_MASK = BIT(0)|BIT(2)|BIT(4)|BIT(5)|BIT(12)|BIT(13)|BIT(14)|BIT(15)|BIT(16);

class OutputToggle
{
    gpio_config_t _config;
    gpio_num_t _pin;
    bool _status;
    bool _invert;

public:
    OutputToggle( gpio_num_t pin, bool status = false, bool invert = false );
    virtual ~OutputToggle();

    esp_err_t init( gpio_num_t pin = (gpio_num_t)-1 );
    void on();
    void off();
    void toggle();
    bool isOn();
};

class Flasher : public OutputToggle
{
    uint32_t _onInterval;
    uint32_t _offInterval;
    TaskHandle_t _task;
public:
    Flasher( gpio_num_t pin );
    virtual ~Flasher();

    void setPattern( uint32_t onInterval, uint32_t offInterval );
    uint32_t nextDelay();
    bool running();
};

class Network
{
    EventGroupHandle_t _event_group;
    int _retries;
    int _max_retries;
    wifi_config_t _wifi_config;
    Flasher &_flasher;
    uint8_t _mac[6];

public:
    Network( Flasher &flasher );
    ~Network();

    void init();
    void connect( const char *ssid, const char *password, int retries );
    void handleEvent( esp_event_base_t event_base, int32_t event_id, void* event_data );
    const uint8_t *getMacAddress() const { return _mac; }
    bool matchesMacAddress( const char *mac ) const;
};

class Zone;
typedef std::list<Zone> ZoneList;

class MQTTData
{
public:
    char *topic;
    char *data;
    size_t data_len;

    MQTTData();
    ~MQTTData();

    void reset();

    void append( esp_mqtt_event_handle_t event );
};

class MQTTClient
{
    Network &_network;
    ZoneList _zones;
    esp_mqtt_client_config_t _mqtt_config;
    esp_mqtt_client_handle_t _client;

    MQTTData _data;

public:
    MQTTClient( Network &network );
    ~MQTTClient();

    void init();
    void connect( const char *brokerUrl );
    void publish( const char *topic, const char *message, int qos = 1, bool retain = true );
    void handleEvent( esp_mqtt_event_handle_t event );

    void addZone( const char *home, const char *zone );
    void removeZone( const char *home, const char *zone );
    Zone *getZone( const char *home, const char *zone ) const;
};

class DeviceChange
{
    char _homeId[37];
    char _zoneId[37];
    char _deviceId[37];
    char _type[16];
    int8_t _direction;

public:
    DeviceChange( cJSON *json, const char *defaultHomeId = NULL, const char *defaultZoneId = NULL );

    bool matches( const char *homeId, const char *zoneId, const char *deviceId, const char *type ) const;
    int8_t getDirection() const { return _direction; }
};

typedef std::list<DeviceChange> DeviceChangeList;

class DeviceValue
{
public:
    union {
        double doubleValue;
        int intValue;
        bool boolValue;
    } value;
    char unit[16];

    DeviceValue() {
        value.doubleValue = 0;
        unit[0] = '\0';
    }
};

class DeviceCalibration
{
    char _type[16];
    DeviceValue _threshold;
    DeviceValue _calibration;

public:
    DeviceCalibration( cJSON *json );

    bool matches( const char *type ) const;

    double adjust( double value ) const;
    int adjust( int value ) const;
    bool adjust( bool value ) const;

    double doubleThreshold() const { return _threshold.value.doubleValue; }
    int intThreshold() const { return _threshold.value.intValue; }
};

typedef std::list<DeviceCalibration> DeviceCalibrationList;

class Device
{
    Zone &_zone;
    char _id[37];
    DeviceChangeList _changes;
    DeviceCalibrationList _calibrations;

public:
    Device( Zone &zone, const char *id );
    virtual ~Device();

    Zone &getZone() const;

    const char *getId() const;
    virtual bool is( const char *deviceType ) const = 0; 
    
    virtual void setInterval( uint32_t interval ) {};

    void addChange( cJSON *json );
    void clearChanges();
    const DeviceChangeList &getChanges() const { return _changes; }

    void addCalibration( cJSON *json );
    void clearCalibrations();
    const DeviceCalibration *findCalibration( const char *type );

    virtual void on() {}
    virtual void off() {}
};

typedef std::list<Device*> DeviceList;

class DeviceTarget
{
    char _homeId[37];
    char _zoneId[37];
    char _deviceId[37];
    char _type[16];
    DeviceValue _value;

public:
    DeviceTarget( cJSON *json, const char *defaultHomeId = NULL, const char *defaultZoneId = NULL );

    bool matches( const char *homeId, const char *zoneId, const char *deviceId, const char *type ) const;

    double doubleValue() const { return _value.value.doubleValue; }
    int intValue() const { return _value.value.intValue; }
    bool boolValue() const { return _value.value.boolValue; }

    const char *unit() const { return _value.unit; }
};

typedef std::list<DeviceTarget> DeviceTargetList;

class Schedule
{
    uint8_t _days;
    uint8_t _hour;
    uint8_t _minute;
    DeviceTargetList _targets;

public:
    Schedule( cJSON *json, const char *defaultHomeId = NULL, const char *defaultZoneId = NULL );
    ~Schedule();

    const DeviceTarget *getTarget( const char *homeId, const char *zoneId, const char *deviceId, const char *type ) const;
    uint8_t getHour() const { return _hour; }
    uint8_t getMinute() const { return _minute; }
    uint8_t getDays() const { return _days; }
};

typedef std::list<Schedule> ScheduleList;

class Override
{
    time_t _start;
    time_t _end;
    DeviceTargetList _targets;

public:
    Override( cJSON *json, const char *defaultHomeId = NULL, const char *defaultZoneId = NULL );
    ~Override();

    const DeviceTarget *getTarget( const char *homeId, const char *zoneId, const char *deviceId, const char *type ) const;
    time_t getStart() const { return _start; }
    time_t getEnd() const { return _end; }
};

typedef std::list<Override> OverrideList;

class Zone
{
    MQTTClient &_client;
    DeviceList _devices;
    ScheduleList _schedules;
    OverrideList _overrides;
    char _homeId[37];
    char _zoneId[37];

    void handleValue( const char *homeId, const char *zoneId, const char *deviceId, const char *type, double value, const char *valueUnit, double target, const char *targetUnit );
    void handleValue( const char *homeId, const char *zoneId, const char *deviceId, const char *type, int value, const char *valueUnit, int target, const char *targetUnit );
    void handleValue( const char *homeId, const char *zoneId, const char *deviceId, const char *type, bool value, const char *valueUnit, bool target, const char *targetUnit );

    const DeviceTarget *findDeviceTarget( const char *deviceId, const char *type ) const;
    const Device *findDeviceForTarget( const char *home, const char *zone, const char *deviceId, const char *type, int8_t direction );

    bool matchesZone( const char **path, size_t pathlen );
    void configureZoneDeviceJSON( const char *deviceId, cJSON *json );
    void sendDeviceReadingJSON( const char *deviceId, const char *type, cJSON *value, cJSON *target=NULL, cJSON *threshold=NULL );
    void setRemoteValueJSON( const char *home, const char *zone, const char *deviceId, const char *type, cJSON *json );

    void takeAction( const char *home, const char *zone, const char *deviceId, const char *type, double value, const char *unit, double targetValue, const char *targetUnit, double threshold = 0 );
    void takeAction( const char *home, const char *zone, const char *deviceId, const char *type, int value, const char *unit, int targetValue, const char *targetUnit, int threshold = 0 );
    void takeAction( const char *home, const char *zone, const char *deviceId, const char *type, bool value, bool targetValue );
    
public:
    Zone( MQTTClient &client, const char *home, const char *zone );
    ~Zone();

    const char *getHomeId() const { return _homeId; }
    const char *getZoneId() const { return _zoneId; }

    bool matches( const char *home, const char *zone ) const;
    void configureZoneJSON( const char **path, size_t pathlen, cJSON *json );

    void setValue( const char *id, const char *type, double value, const char *unit, double threshold=0 );
    void setValue( const char *id, const char *type, int value, const char *unit, int threshold=0 );
    void setValue( const char *id, const char *type, bool value );

    void addDevice( Device *device );
    void removeDevice( const char *deviceId );
    Device * findDevice( const char *deviceId );
    void clearDevices();

    void addSchedule( cJSON *json );
    void clearSchedules();

    void addOverride( cJSON *json );
    void clearOverrides();
    
    Device *getDevice( const char *deviceId );
};

class DHTSensor : public Device
{
    gpio_config_t _config;
    gpio_num_t _pin;
    dht_sensor_type_t _type;
    uint32_t _interval;
    TaskHandle_t _task;

public:
    DHTSensor( Zone &zone, const char *id );
    virtual ~DHTSensor();

    bool is( const char *type ) const {
        return strcmp( "dht11", type ) == 0 || strcmp( "dht22", type ) == 0;
    }

    esp_err_t init( gpio_num_t pin, dht_sensor_type_t type = DHT_TYPE_DHT11, bool pull_up = false );
    void setInterval( uint32_t interval );
    uint32_t getInterval() const;
    void read();
};

class DS18X20Sensor : public Device
{
    gpio_num_t _pin;
    ds18x20_addr_t _addr;
    uint32_t _interval;
    TaskHandle_t _task;

public:
    DS18X20Sensor( Zone &zone, const char *id );
    virtual ~DS18X20Sensor();

    bool is( const char *type ) const {
        return strcmp( "ds18x20", type ) == 0;
    }

    esp_err_t init( gpio_num_t pin, ds18x20_addr_t addr = ds18x20_ANY );
    void setInterval( uint32_t interval );
    uint32_t getInterval() const;
    void read();
};

class Switch : public Device
{
    OutputToggle _toggle;

public:
    Switch( Zone &zone, const char *id );
    virtual ~Switch();

    bool is( const char *type ) const {
        return strcmp( "gpio", type ) == 0;
    }

    esp_err_t init( gpio_num_t pin );
    void on();
    void off();
};

#endif
