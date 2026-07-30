// Function Property addressing - must match DaliModule::processFunctionProperty / processFunctionPropertyState in DaliModule.cpp
var DALI_OBJECT_INDEX = 160;
var DALI_PROPERTY_ID = 1;

// Function codes - must match the data[0] switch in DaliModule.cpp
var FUNC_READ_DEVICETYPE = 2;
var FUNC_SCAN = 3;
var FUNC_ASSIGN_ADDRESS = 4;
var FUNC_EVG_WRITE = 10;
var FUNC_EVG_READ = 11;
var FUNC_SET_SCENE = 12;
var FUNC_GET_SCENE = 13;
var FUNC_IDENTIFY = 14;
var FUNC_FOUND_EVGS = 7;

var CHANNEL_COUNT = 64;
var SCENE_COUNT = 16;
var DISABLED_VALUE = 255;
var STOP_READING_RESULTS = 254;

// EVG read error bits - must match funcHandleEvgRead in DaliModule.cpp
var ERR_MIN = 1;
var ERR_MAX = 2;
var ERR_POWER = 4;
var ERR_FAILURE = 8;
var ERR_FADE = 16;
var ERR_GROUPS_LOW = 64;
var ERR_GROUPS_HIGH = 128;

// Assign response codes - must match DaliAddressing::AssigningResponse in DaliAddressing.h
var ASSIGNING_RESPONSE_SUCCESS = 0;
var ASSIGNING_RESPONSE_NOT_FREE = 1;
var ASSIGNING_RESPONSE_NO_RESPONSE = 2;
var ASSIGNING_RESPONSE_NO_RESPONSE_LONG = 3;
// Note: AssigningResponse only spans 0-4, so a response of 12 can never
// actually occur - kept here only to document the unreachable case below.
var ASSIGNING_RESPONSE_CONFIRM_FAILED = 12;

function getFloat(data, offset) {
    var level = (data[offset] << 8 | data[offset + 1]);
    return level / 65534;
}

function getBytes(data) {
    return data * 65534;
}

function getParaInt(device, paraName) {
    return parseInt(device.getParameterByName(paraName).value, 10);
}

function getParaFloat(device, paraName) {
    var value_out = device.getParameterByName(paraName).value;
    if (typeof value_out === 'number')
        return value_out;
    return parseFloat(value_out.replace(",", "."));
}

function getParaBool(device, paraName) {
    return device.getParameterByName(paraName).value == "1";
}

function setPara(device, paraName, value) {
    device.getParameterByName(paraName).value = value;
}

function arcToPercent(arc) {
    if (arc == 0) return 0;
    return Math.pow(10, ((arc - 1) / (253 / 3)) - 1);
}

// Unpacks 8 group membership bits (g<startIndex> .. g<startIndex+7>) from a
// single byte read from the device into the matching ETS parameters.
function unpackGroupByte(device, prefix, byteValue, startIndex) {
    for (var bit = 0; bit < 8; bit++) {
        setPara(device, prefix + "g" + (startIndex + bit), (byteValue >> bit) & 1);
    }
}

// Packs 8 group membership parameters (g<startIndex> .. g<startIndex+7>) from
// ETS into a single byte to send to the device.
function packGroupByte(device, prefix, startIndex) {
    var value = 0;
    for (var bit = 0; bit < 8; bit++) {
        value |= getParaInt(device, prefix + "g" + (startIndex + bit)) << bit;
    }
    return value;
}

function dali_read(device, online, progress, context) {
    // Start read devicetype
    progress.setText(device.getMessage(calcMessage("DGW_devicetype_read")));

    var data = [FUNC_READ_DEVICETYPE, context.Channel - 1];
    online.connect();
    var resp = online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, data); //invoke readdevicetype

    if (resp[0] != 0) {
        // Dali Error:
        throw new Error(device.getMessage(calcMessage("DGW_dali_error")) + String(resp[0]));
    }

    var prefix = "DGW" + "_" + context.Channel;

    var para = device.getParameterByName(prefix + "deviceType");
    if (resp[1] == DISABLED_VALUE) {
        para.value = 0;
        throw new Error(device.getMessage(calcMessage("DGW_devicetype_unknown"))) // Unknown DeviceType
    }

    para.value = (resp[1] + 1).toString();

    if (resp[1] == 6 || resp[1] == 8) {
        var byte = resp[2];

        para = device.getParameterByName(prefix + "colorSpace");
        para.value = (byte & 1) ?"1" : "0";


        para = device.getParameterByName(prefix + "colorType");
        para.value = (byte & 2) ?"2" : "1";
    }

    // Read Successfully devicetype
    progress.setText(device.getMessage(calcMessage("DGW_devicetype_success")));
}

function dali_settingsRead(device, online, progress, context) {
    var prefix = "DGW" + "_" + context.Channel;
    Log.info("Start reading settings from EVG");
    Log.info("Prefix: " + prefix);
    progress.setText(device.getMessage(calcMessage("DGW_evgReadStart"))); // Start reading Data from EVG
    online.connect();
    var data = online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, [FUNC_EVG_READ, context.Channel - 1]);
    progress.setProgress(10);
    if (data[0] != 0)
        throw new Error("Dali Error: " + data[0]);
    var errors = "";

    if (data[9] & ERR_MIN) 
        errors += "Min Level, ";
    else {
        setPara(device, prefix + "min", arcToPercent(data[1]).toFixed(2).replace(".", ","));
    }
    if (data[9] & ERR_MAX)
        errors += "Max Level, ";
    else {
        setPara(device, prefix + "max", arcToPercent(data[2]).toFixed(2).replace(".", ","));
    }
    if (data[9] & ERR_POWER) 
        errors += "Power On, ";
    else {
        setPara(device, prefix + "poweron", data[3] == DISABLED_VALUE);
        if (data[3] != DISABLED_VALUE)
            setPara(device, prefix + "poweronlevel", arcToPercent(data[3]).toFixed(2).replace(".", ","));
    }
    if (data[9] & ERR_FAILURE)
        errors += "Failure On, ";
    else {
        setPara(device, prefix + "failureon", data[4] == DISABLED_VALUE);
        if (data[4] != DISABLED_VALUE)
            setPara(device, prefix + "failureonlevel", arcToPercent(data[4]).toFixed(2).replace(".", ","));
    }
    if (data[9] & ERR_FADE)
        errors += "FadeTime/Rate, ";
    else {
        setPara(device, prefix + "fadeTime", (data[5] >> 4).toString());
        setPara(device, prefix + "fadeRate", (data[5] & 15).toString());
    }
    //1 byte free
    if (data[9] & ERR_GROUPS_LOW)
        errors += "Groups 0-7, ";
    else
        unpackGroupByte(device, prefix, data[7], 0);
    if (data[9] & ERR_GROUPS_HIGH)
        errors += "Groups 8-15, ";
    else
        unpackGroupByte(device, prefix, data[8], 8);

    progress.setProgress(20);

    data = [
        FUNC_GET_SCENE,
        context.Channel - 1,
        0, //scene number
        getParaInt(device, prefix + "deviceType"),
        getParaInt(device, prefix + "colorType")
    ];

    for (var i = 0; i < SCENE_COUNT; i++)
    {
        progress.setText(device.getMessage(calcMessage("DGW_evgReadScene")) + i.toString()); // Parsing data
        data[2] = i;

        resp = online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, data);

        setPara(device, prefix + "s" + i + "t", resp[0] != DISABLED_VALUE);

        if (resp[0] != DISABLED_VALUE) {
            setPara(device, prefix + "s" + i + "v", arcToPercent(resp[0]).toFixed(2).replace(".", ","));
            //deviceType is Color
            if (data[3] == 9) {
                //colorType is TunableWhite
                if (data[4] == 2) {
                    var kelvin = parseInt((resp[1] << 8) | resp[2]);
                    setPara(device, prefix + "s" + i + "ct", kelvin);
                } else { //it is RGB
                    var color = (resp[1] << 16) | (resp[2] << 8) | resp[3];
                    var hexString = "#" + ("000000" + color.toString(16).toUpperCase()).slice(-6);
                    progress.setText(hexString);
                    setPara(device, prefix + "s" + i + "cc", hexString);
                }
            }
        }

        progress.setProgress(i * 5 + 25);
    }

    if (errors != "")
        progress.setText(device.getMessage(calcMessage("DGW_evgReadError")) + errors); // following couldnt be read
    else
        progress.setText(device.getMessage(calcMessage("DGW_evgReadFin"))); // reading successfull
}

function dali_settingsWrite(device, online, progress, context) {
    var prefix = "DGW" + "_" + context.Channel;
    Log.info("Start writing settings to EVG");
    Log.info("Prefix: " + prefix);
    progress.setText(device.getMessage(calcMessage("DGW_evgWriteStart"))); // start

    var index = 0;
    var data = [];
    data[index++] = FUNC_EVG_WRITE;
    data[index++] = context.Channel - 1;
    var temp = getBytes(getParaFloat(device, prefix + "min") / 100.0);
    data[index++] = temp >> 8;
    data[index++] = temp & 255;
    temp = getBytes(getParaFloat(device, prefix + "max") / 100.0);
    data[index++] = temp >> 8;
    data[index++] = temp & 255;
    if (!getParaBool(device, prefix + "poweron")) {
        temp = getBytes(getParaFloat(device, prefix + "poweronlevel") / 100.0);
        data[index++] = temp >> 8;
        data[index++] = temp & 255;
    } else {
        data[index++] = DISABLED_VALUE;
        data[index++] = DISABLED_VALUE;
    }
    if (!getParaBool(device, prefix + "failureon")) {
        temp = getBytes(getParaFloat(device, prefix + "failureonlevel") / 100.0);
        data[index++] = temp >> 8;
        data[index++] = temp & 255;
    } else {
        data[index++] = DISABLED_VALUE;
        data[index++] = DISABLED_VALUE;
    }

    var fade = getParaInt(device, prefix + "fadeTime");
    fade = fade << 4;
    fade |= getParaInt(device, prefix + "fadeRate");
    data[index++] = fade;
    data[index++] = 0;//1byte free
    data[index++] = packGroupByte(device, prefix, 0);
    data[index++] = packGroupByte(device, prefix, 8);

    progress.setText(device.getMessage(calcMessage("DGW_evgWriteTransmit"))); // transmit
    online.connect();
    var resp = online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, data);
    progress.setProgress(20);

    data = [
        FUNC_SET_SCENE,
        context.Channel - 1, // + 128; only for group!
        0, //scene number
        0, //enabled
        getParaInt(device, prefix + "deviceType"),
        getParaInt(device, prefix + "colorType"),
        0, 0, 0, 0 //will be filled later
    ];

    for (var i = 0; i < SCENE_COUNT; i++)
    {
        data[2] = i;
        var isEnabled = getParaBool(device, prefix + "s" + i + "t");
        data[3] = isEnabled;
        if (isEnabled) {
            temp = getBytes(getParaFloat(device, prefix + "s" + i + "v") / 100.0);
            data[6] = temp >> 8;
            data[7] = temp & 255;
        } else {
            data[6] = DISABLED_VALUE;
            data[7] = DISABLED_VALUE;
        }

        Log.info("Writing scene " + i);
        Log.info("isEnabled: " + (isEnabled ? "yes" : "no"));
        Log.info("deviceType: " + data[4]);
        Log.info("colorType: " + data[5]);
        //deviceType is Color
        if (isEnabled && data[4] == 9)
        {
            //colorType is TunableWhite
            if (data[5] == 2) {
                var kelvin = getParaInt(device, prefix + "s" + i + "ct");
                Log.info(kelvin);
                data[8] = kelvin >> 8;
                data[9] = kelvin & 255;
            } else { //it is RGB
                var etsval = device.getParameterByName(prefix + "s" + i + "cc").value;
                if (etsval < 0)
                etsval = etsval + 4294967296;
                data[8] = (etsval >> 16) & 255;
                data[9] = (etsval >> 8) & 255;
                data[10] = etsval & 255;
            }
        }

        progress.setText(device.getMessage(calcMessage("DGW_evgWriteScene")) + i); // Transmitting scene i
        resp = online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, data);
        progress.setProgress(i * 5 + 25);
    }

    online.disconnect();
    progress.setText(device.getMessage(calcMessage("DGW_evgWriteFin"))); // fin
}

function dali_assingAddr(device, online, progress, context) {
    var along = device.getParameterByName("DGW_longAddr");
    var ashort = device.getParameterByName("DGW_shortAddr");

    //assign address to device
    progress.setText(device.getMessage(calcMessage("DGW_addr_start")));

    var bytes = [FUNC_ASSIGN_ADDRESS, parseInt(ashort.value)];
    for (var c = 0; c < along.value.length; c += 2)
    bytes.push(parseInt(along.value.substr(c, 2), 16));

    online.connect();

    online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, bytes);

    bytes = [];
    while (true) {
        if (progress.isCanceled()) {
            online.readFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, [FUNC_ASSIGN_ADDRESS, DISABLED_VALUE]);
            return;
        }

        var resp = online.readFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, [FUNC_ASSIGN_ADDRESS]);

        if (resp[0] == 0)
            continue;

        switch (resp[1]) {
            case ASSIGNING_RESPONSE_SUCCESS:
                progress.setText(device.getMessage(calcMessage("DGW_addr_success")));
                return;

            case ASSIGNING_RESPONSE_NOT_FREE:
                throw new Error(device.getMessage(calcMessage("DGW_addr_double")));

            case ASSIGNING_RESPONSE_NO_RESPONSE:
                throw new Error(device.getMessage(calcMessage("DGW_response_timeout")));

            case ASSIGNING_RESPONSE_NO_RESPONSE_LONG:
                throw new Error(device.getMessage(calcMessage("DGW_addr_long_dont_exists")));

            // TODO NOTE: stateHandleAssign() in DaliModule.cpp only ever returns
            // resp[1] in range 0-4 (AssigningResponse enum), so this case is
            // currently unreachable - kept as-is, not touched by this refactor.
            case ASSIGNING_RESPONSE_CONFIRM_FAILED:
                //short address confirm failed
                throw new Error(device.getMessage(calcMessage("DGW_addr_confirm_failed")));

            default:
                progress.setText(device.getMessage(calcMessage("DGW_dali_error")));
                return;

        }
    }
}

function dali_scan(device, online, progress, context) {
    online.connect();

    var data = [FUNC_SCAN];
    var para2 = device.getParameterByName("DGW_onlyUnaddressed");
    data.push(parseInt(para2.value));
    para2 = device.getParameterByName("DGW_dontRandomize");
    data.push(parseInt(para2.value));
    para2 = device.getParameterByName("DGW_deleteAll");
    data.push(parseInt(para2.value));
    para2 = device.getParameterByName("DGW_assignNew");
    data.push(parseInt(para2.value));

    //start addressing
    online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, data);
    progress.setText(device.getMessage(calcMessage("DGW_scanStart")));

    for (var i = 0; i < CHANNEL_COUNT; i++)
    {
        var para = device.getParameterByName("DGW_ballast" + i);
        para.value = "";
    }

    var counter = 0;

    while (true) {
        if (progress.isCanceled()) {
            //get State with max devicecount so device will stop and delete variables
            online.readFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, [FUNC_FOUND_EVGS, STOP_READING_RESULTS]);
            return;
        }

        var resp = online.readFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, [FUNC_SCAN]);
        if (resp[0]) break;
        progress.setText(resp[1] + " " + device.getMessage(calcMessage("DGW_scanFound")));
        progress.setProgress((100.0 / CHANNEL_COUNT) * resp[1]);

        //just skip some time so we dont overkill the 
        //~2s depends on device
        var start = new Date();
        var count = 0;
        var millis = 2000;
        // busy waiting, as there is no other known possibility in ETS
        while (new Date() - start < millis) {
            count++;
        }
    }

    while (true) {
        if (progress.isCanceled()) {
            //get State with max devicecount so device will stop and delete variables
            online.readFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, [FUNC_FOUND_EVGS, STOP_READING_RESULTS]);
            return;
        }

        var resp = online.readFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, [FUNC_FOUND_EVGS, counter]);

        if (resp[0]) {
            //found ballast
            var high = "";
            if (resp[1] < 16) high = "0";
            high += resp[1].toString(16);
            if (resp[2] < 16) high += "0";
            high += resp[2].toString(16);
            if (resp[3] < 16) high += "0";
            high += resp[3].toString(16);
            var para = device.getParameterByName("DGW_ballast" + counter);
            high = "0x" + high;
            if (resp[4] < 99)
            high += " -> " + resp[4];
            para.value = high;
            counter++;
        } else {
            break;
        }
    }

    progress.setText(counter + " " + device.getMessage(calcMessage("DGW_scanFound"))); // finished
}

function dali_identify(device, online, progress, context) {
    var channel = context.Channel - 1;
    if (context.stop) {
        progress.setText("stop identify");
    } else {
        if (context.group) {
            progress.setText("identify group: " + channel);
        } else {
            progress.setText("identify address: " + channel);
        }
    }
    online.connect();
    var data = [FUNC_IDENTIFY];
    data.push(channel);
    data.push(context.group);
    data.push(context.stop);
    online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, data);
}