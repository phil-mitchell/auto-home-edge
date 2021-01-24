var Gpio = require( 'onoff' ).Gpio;
var ds18x20 = require( 'ds18x20' );
var config = require( 'config' );
var superagent = require( 'superagent' );
var YAML = require( 'yaml' );

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

async function readInput( input ) {
    try {
        input.current = {
            value: null,
            unit: input.data.unit
        };
        if( input.data.interface === 'ds18x20' ) {
            input.current.value = ( await readDS18x20( input.data.address ) ) + ( input.data.calibrate || 0 );
        }
        console.log( `Current value from ${input.name}: ${input.current.value} ${input.current.unit}` );
    } catch( e ) {
        console.error( `Error reading input ${input.name}: ${e.message}` );
    }

    return input.current;
}

async function loadZone( zone ) {
    if( config.autohome ) {
        try {
            let url = `${config.autohome.url}/api/homes/${config.autohome.home}/zones/${zone.id}`;

            let updates = ( await superagent.get( url ).query({ api_key: config.autohome.api_key }) ).body;
            for( let deviceUpdates of updates.devices ) {
                deviceUpdates.data = YAML.parse( deviceUpdates.data || '{}' );
                for( let device of zone.devices ) {
                    if( device.name === deviceUpdates.name ) {
                        deviceUpdates.data = Object.assign( device.data, deviceUpdates.data );
                    }
                }
            }

            Object.assign( zone, updates );
        } catch( e ) {
            console.error( `Error updating config for zone ${zone.name} from autohome: ${e.message}` );
        }
    }

    for( let device of zone.devices ) {
        if( !device.gpio && device.data.interface === 'gpio' ) {
            device.gpio = new Gpio( device.data.address, 'out' );
        }
    }

    let changes = {};
    let now = new Date();
    let day = now.getDay();
    let time = now.toTimeString().slice( 0, 5 );

    for( let schedule of zone.schedules ) {
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

    for( let override of zone.overrides ) {
        if( new Date( override.start ) <= now && new Date( override.end ) >= now ) {
            for( let change of override.changes ) {
                changes[change.device] = change.value;
            }
        }
    }

    for( let device of zone.devices ) {
        device.target = changes[device.name] || {};
    }
}

async function addSensorReading( zone, sensor, type, value, data ) {
    if( config.autohome ) {
        try {
            let url = `${config.autohome.url}/api/homes/${config.autohome.home}/zones/${zone.id}/addSensorReading`;

            await superagent.post( url ).query({ api_key: config.autohome.api_key }).send({
                time: new Date().toISOString(),
                sensor,
                type,
                value: value.value,
                unit: value.unit,
                data
            });
        } catch( e ) {
            console.error( `Error uploading sensor reading for ${sensor}: ${e.message}` );
        }
    }
}


async function update( reset ) {
    for( let zone of config.zones ) {
        await loadZone( zone );

        for( let input of( zone.devices || [] ).filter(
            x => x.direction === 'input' || x.direction === 'in/out'
        ) ) {
            await readInput( input );
            if( input.current.value != null ) {
                await addSensorReading( zone, input.name, input.type, input.current );
            }
        }

        let changes = {};
        for( let device of ( zone.devices || [] ) ) {
            let current = ( device.current || {}).value;
            let target = ( device.target || {}).value;
            let threshold = ( device.data || {}).threshold || 0;
            if( current == null || target == null ) {
                // either current reading is unknown or no target is set
                // so we don't need to do anything (or at least we don't know what to do)
                console.log( `No current/target value for ${device.name}` );
                continue;
            }

            if( current <= ( target - threshold ) ) {
                console.log( `Device ${device.name} has value ${current} which is more than ${threshold} below ${target}` );
                changes[device.name] = 'increase';
            } else if( current >= ( target + threshold ) ) {
                console.log( `Device ${device.name} has value ${current} which is more than ${threshold} above ${target}` );
                changes[device.name] = 'decrease';
            } else {
                console.log( `Device ${device.name} has value ${current} which is within ${threshold} of ${target}` );
            }
        }

        for( let output of( zone.devices || [] ).filter(
            x => x.direction === 'output' || x.direction === 'in/out'
        ) ) {
            console.log( `Checking desired state for ${output.name}` );
            let newValue = null;

            for( let change of ( output.data.changes || [] ) ) {
                if( changes[change.device] ) {
                    console.log( `${output.name} will ${change.direction} ${change.device}` );
                    newValue = newValue || changes[change.device] === change.direction;
                }
            }

            if( !reset && newValue == null ) {
                console.log( `No changes required for ${output.name}` );
                continue;
            }

            console.log( `Desired state for ${output.name} is ${newValue}` );

            if( output.gpio ) {
                await setGPIO( output.gpio, newValue );
                await addSensorReading( zone, output.name, 'on-off', { value: newValue ? 1 : 0 });
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
                    await setGPIO( output.gpio, false );
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
