var Gpio = require( 'onoff' ).Gpio;
var ds18x20 = require( 'ds18x20' );
var dht = require( 'node-dht-sensor' ).promises;
var config = require( 'config' );
var superagent = require( 'superagent' );
var path = require( 'path' );
var fs = require( 'fs-extra' );
var MQTT = require( 'async-mqtt' );

var client = null;

dht.setMaxRetries( 10 );

async function setGPIO( output, on ) {
    console.log( `Turning ${ on ? 'ON' : 'OFF' } GPIO ${output._gpio}` );

    await new Promise( ( resolve, reject ) => {
        output.write( on ? Gpio.LOW : Gpio.HIGH, ( err, result ) => {
            if( err ) {
                return reject( err );
            }
            return resolve( result );
        });
    });
}

async function readDS18x20( id ) {
    return new Promise( ( resolve, reject ) => {
        ds18x20.get( id, ( err, result ) => {
            if( err ) {
                return reject( err );
            }
            return resolve( result );
        });
    });
}

async function readDHT11( id, type ) {
    let res = await dht.read( 11, id );
    return res[type];
}

async function readDHT22( id, type ) {
    let res = await dht.read( 22, id );
    return res[type];
}

async function readInput( input ) {
    try {
        input.current = {
            value: null,
            unit: ( input.data || {}).unit
        };

        if( !input.current.unit ) {
            switch( input.type ) {
            case'temperature':
                input.current.unit = 'celsius';
                break;
            case'humidity':
                input.current.unit = 'percent';
                break;
            }
        }

        switch( ( input.interface || {}).type ) {
        case'ds18x20':
            input.current.value = ( await readDS18x20( input.interface.address ) ) + ( input.calibrate || 0 );
            break;
        case'dht11':
            input.current.value = ( await readDHT11( input.interface.address, input.type ) ) + ( input.calibrate || 0 );
            break;
        case 'dht22':
            input.current.value = ( await readDHT22( input.interface.address, input.type ) ) + ( input.calibrate || 0 );
            break;
        default:
            console.warn( `Unknown interface type for ${input.name}: ${( input.interface || {}).type}` );
        }
        console.log( `Current value from ${input.name}: ${input.current.value} ${input.current.unit}` );
    } catch( e ) {
        console.error( `Error reading input ${input.name}: ${e.message}` );
    }

    return input.current;
}

async function saveConfig() {
    let configfile = path.join(
        path.dirname( config.util.getConfigSources()[0].name ),
        `local-${process.env.NODE_CONFIG_ENV || process.env.NODE_ENV || 'development'}.json` );

    await fs.writeJson( configfile, config.util.toObject() );
}

async function loadZone( zone ) {
    if( config.autohome ) {
        try {
            let url = `${config.autohome.url}/api/homes/${config.autohome.home}/zones/${zone.id}`;

            let updates = ( await superagent.get( url ).query({ api_key: config.autohome.api_key }) ).body;
            Object.assign( zone, updates );
        } catch( e ) {
            console.error( `Error updating config for zone ${zone.name} from autohome: ${e.stack}` );
        }

        try {
            let url = `${config.autohome.url}/api/homes/${config.autohome.home}/zones/${zone.id}/devices`;

            let updates = ( await superagent.get( url ).query({ api_key: config.autohome.api_key }) ).body;
            for( let deviceUpdates of ( updates || [] ) ) {
                for( let device of ( zone.devices || [] ) ) {
                    if( device.id === deviceUpdates.id ) {
                        if( device.gpio ) {
                            deviceUpdates.gpio = device.gpio;
                            config.util.makeHidden( deviceUpdates, 'gpio' );
                        }
                    }
                }
            }

            zone.devices = updates;
        } catch( e ) {
        }
    }

    for( let device of ( zone.devices || [] ) ) {
        if( ( device.interface || {}).type === 'gpio' ) {
            let pins = ( device.interface.address || '' ).split( ',' );
            let gpio = {};
            for( let pin of pins ) {
                if( ( device.gpio || {})[pin] ) {
                    gpio[pin] = device.gpio[pin];
                } else {
                    console.log( `Constructing GPIO for pin ${pin}` );
                    gpio[pin] = new Gpio( pin, 'out' );
                }
            }
            device.gpio = gpio;
            config.util.makeHidden( device, 'gpio' );
        }
    }

    let changes = {};
    let now = new Date();
    let day = now.getDay();
    let time = now.toTimeString().slice( 0, 5 );

    for( let schedule of ( zone.schedules || [] ) ) {
        if( schedule.days.indexOf( now.getDay() ) > -1 ) {
            if( schedule.start <= time ) {
                for( let change of schedule.changes ) {
                    changes[change.device] = change.value;
                }
            } else {
                break;
            }
        }
    }

    for( let override of ( zone.overrides || [] ) ) {
        if( new Date( override.start ) <= now && new Date( override.end ) >= now ) {
            for( let change of override.changes ) {
                changes[change.device] = change.value;
            }
        }
    }

    for( let device of ( zone.devices || [] ) ) {
        device.target = changes[device.id];
    }

    await saveConfig();
}

async function addSensorReading( zone, device, value, target, data ) {
    if( client ) {
        try {
            await client.publish(
                `homes/${( config.autohome || {}).home || 'local'}/zones/${zone.id}/devices/${device.id}/${device.type}`, JSON.stringify({
                    time: new Date().toISOString(),
                    value,
                    target,
                    data
                }), {
                    qos: 1,
                    retain: true
                });
        } catch( e ) {
            console.error( `Error publishing sensor reading for ${device.name}: ${e.message}` );
            console.error( e );
        }
    }
}

async function update( reset ) {
    if( !client ) {
        try {
            console.log( `Attempting to connect to MQTT broker` );
            client = await MQTT.connectAsync( ( config.mqtt || {}).url, ( config.mqtt || {}).options );
            config.util.makeHidden( config, 'mqtt' );
        } catch( e ) {
            console.error( `Unable to connect to MQTT broker, continuing off-line: ${e.stack}` );
        }
    }

    for( let zone of config.zones ) {
        await loadZone( zone );

        for( let input of( zone.devices || [] ).filter(
            x => x.direction === 'input' || x.direction === 'in/out'
        ) ) {
            await readInput( input );
            if( input.current.value != null ) {
                await addSensorReading( zone, input, input.current, input.target );
            }
        }

        let changes = {};
        for( let device of ( zone.devices || [] ) ) {
            let current = ( device.current || {}).value;
            let target = ( device.target || {}).value;
            let threshold = device.threshold || 0;
            if( current == null || target == null ) {
                // either current reading is unknown or no target is set
                // so we don't need to do anything (or at least we don't know what to do)
                console.log( `No current/target value for ${device.name}` );
                continue;
            }

            if( current <= ( target - threshold ) ) {
                console.log( `Device ${device.name} has value ${current} which is more than ${threshold} below ${target}` );
                changes[device.id] = 'increase';
            } else if( current >= ( target + threshold ) ) {
                console.log( `Device ${device.name} has value ${current} which is more than ${threshold} above ${target}` );
                changes[device.id] = 'decrease';
            } else {
                console.log( `Device ${device.name} has value ${current} which is within ${threshold} of ${target}` );
            }
        }

        for( let output of( zone.devices || [] ).filter(
            x => x.direction === 'output' || x.direction === 'in/out'
        ) ) {
            console.log( `Checking desired state for ${output.name}` );
            let newValue = null;

            for( let change of ( output.changes || [] ) ) {
                if( changes[change.device] ) {
                    console.log( `${output.name} would ${change.direction} ${change.device}` );
                    newValue = newValue || changes[change.device] === change.direction;
                }
            }

            if( !reset && newValue == null ) {
                console.log( `No changes required for ${output.name}` );
                continue;
            }

            console.log( `Desired state for ${output.name} is ${newValue}` );

            if( output.gpio ) {
                await Promise.all( Object.values( output.gpio ).map( gpio => setGPIO( gpio, newValue ) ) );
                await addSensorReading( zone, output, { value: newValue ? 1 : 0 });
            }
        }
    }
}

update( true ).then( () => {
    setInterval( update, 60000 );
});

async function exitHandler( options, exitCode ) {
    for( let zone of config.zones ) {
        for( let output of ( zone.devices || [] ) ) {
            if( output.gpio ) {
                try {
                    await Promise.all( Object.values( output.gpio ).map( gpio => setGPIO( gpio, false ) ) );
                } catch( e ) {
                    console.error( e.stack );
                }
            }
        }
    }

    process.exit( exitCode || 0 );
}

// try to ensure the panel is off when we exit
process.on( 'exit', exitHandler.bind( null, {}) );
process.on( 'SIGINT', exitHandler.bind( null, {}) );
process.on( 'SIGUSR1', exitHandler.bind( null, {}) );
process.on( 'SIGUSR2', exitHandler.bind( null, {}) );
process.on( 'uncaughtException', exitHandler.bind( null, {}) );
