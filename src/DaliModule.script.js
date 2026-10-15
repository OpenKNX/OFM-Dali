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
var FUNC_READ_INFO = 15;

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
var ERR_DIMMCURVE = 32;
var ERR_GROUPS_LOW = 64;
var ERR_GROUPS_HIGH = 128;
var DIMMING_CURVE_UNSUPPORTED = 255;

// Assign response codes - must match DaliAddressing::AssigningResponse in DaliAddressing.h
var ASSIGNING_RESPONSE_SUCCESS = 0;
var ASSIGNING_RESPONSE_NOT_FREE = 1;
var ASSIGNING_RESPONSE_NO_RESPONSE = 2;
var ASSIGNING_RESPONSE_NO_RESPONSE_LONG = 3;
// Note: AssigningResponse only spans 0-4, so a response of 12 can never
// actually occur - kept here only to document the unreachable case below.
var ASSIGNING_RESPONSE_CONFIRM_FAILED = 12;

// Raw DALI device type (as resolved by funcHandleType/FUNC_READ_DEVICETYPE) - index matches the value.
// Wording must stay in sync with the DT0-DT8 Enumeration texts of PT-deviceType in DaliModule.share.xml
// (that enum is offset by +1 because it also has a "Deaktiviert" = 0 entry).
var DEVICE_TYPE_LABELS = [
    "DT0 - Fluoreszierend",
    "DT1 - Eigenst\u00e4ndige Notallbeleuchtung",
    "DT2 - Entladungslampe",
    "DT3 - Niedervolt Halogen",
    "DT4 - Gl\u00fchlampe",
    "DT5 - DC Converter (1-10V, 0-10V)",
    "DT6 - LED",
    "DT7 - Schaltrelais",
    "DT8 - Farbe"
];

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

// Device info read error bits - must match funcHandleReadInfo in DaliModule.cpp
var ERR_INFO_MEMORY_READ = 1;
var ERR_INFO_LEGACY_BANK0 = 2;
var ERR_INFO_PHYSMIN = 4;

// Splits a decimal GTIN into vendor name (or "Unknown (<country>)") and article/model
// number (item reference, only known once the vendor's GS1 company prefix matches).
// A GTIN-14 is 1 indicator digit + a 13-digit body (GS1 prefix + item reference + check
// digit); a plain GTIN-13/EAN-13 is the same body with an implicit indicator digit "0".
function parseGtin(gtinDecimal) {
    var padded = gtinDecimal;
    while (padded.length < 14)
        padded = "0" + padded;
    var body = padded.substr(1);

    for (var i = 0; i < VENDOR_BY_GTIN_PREFIX.length; i++) {
        var prefix = VENDOR_BY_GTIN_PREFIX[i].prefix;
        if (body.indexOf(prefix) == 0) {
            return {
                vendor: VENDOR_BY_GTIN_PREFIX[i].name,
                articleNumber: body.substring(prefix.length, body.length - 1)
            };
        }
    }

    var countryPrefix = parseInt(body.substr(0, 3), 10);
    for (var j = 0; j < GTIN_COUNTRY_PREFIXES.length; j++) {
        if (countryPrefix >= GTIN_COUNTRY_PREFIXES[j].min && countryPrefix <= GTIN_COUNTRY_PREFIXES[j].max)
            return { vendor: "Unknown (" + GTIN_COUNTRY_PREFIXES[j].name + ")", articleNumber: "-" };
    }
    return { vendor: "Unknown", articleNumber: "-" };
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

function bytesToHex(data, start, end) {
    var hex = "";
    for (var i = start; i < end; i++) {
        var b = data[i].toString(16).toUpperCase();
        if (b.length < 2)
            b = "0" + b;
        hex += b;
    }
    return hex;
}

function bytesToDecimal(data, start, end) {
    var value = 0;
    for (var i = start; i < end; i++)
        value = value * 256 + data[i];
    return value.toString();
}

function clearDeviceInfoParas(device, prefix) {
    setPara(device, prefix + "gtin", "-");
    setPara(device, prefix + "vendor", "Unknown");
    setPara(device, prefix + "articleNumber", "-");
    setPara(device, prefix + "firmwareVersion", "-");
    setPara(device, prefix + "serialNumber", "-");
    setPara(device, prefix + "hardwareVersion", "-");
    setPara(device, prefix + "physMinLevel", "-");
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
    if (data[9] & ERR_DIMMCURVE)
        errors += "Dimmkurve, ";
    else
        setPara(device, prefix + "dimmingCurve", data[6]);
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
    data[index++] = getParaInt(device, prefix + "dimmingCurve");
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
        device.getParameterByName("DGW_ballastLong" + i).value = "";
        device.getParameterByName("DGW_ballastShort" + i).value = "";
        device.getParameterByName("DGW_ballastType" + i).value = "";
        device.getParameterByName("DGW_ballastVendor" + i).value = "";
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
            var longAddr = bytesToHex(resp, 1, 4);
            var shortAddr = resp[4];

            device.getParameterByName("DGW_ballastLong" + counter).value = longAddr;

            if (shortAddr < CHANNEL_COUNT) {
                device.getParameterByName("DGW_ballastShort" + counter).value = shortAddr.toString();

                try {
                    var typeResp = online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, [FUNC_READ_DEVICETYPE, shortAddr]);
                    if (typeResp[0] == 0 && typeResp[1] < DEVICE_TYPE_LABELS.length)
                        device.getParameterByName("DGW_ballastType" + counter).value = DEVICE_TYPE_LABELS[typeResp[1]];
                    else
                        device.getParameterByName("DGW_ballastType" + counter).value = "Unknown";
                } catch (e) {
                    device.getParameterByName("DGW_ballastType" + counter).value = "Unknown";
                }

                try {
                    var infoResp = online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, [FUNC_READ_INFO, shortAddr]);
                    if (infoResp[0] == 0 && !(infoResp[21] & ERR_INFO_MEMORY_READ))
                        device.getParameterByName("DGW_ballastVendor" + counter).value = parseGtin(bytesToDecimal(infoResp, 1, 7)).vendor;
                    else
                        device.getParameterByName("DGW_ballastVendor" + counter).value = "Unknown";
                } catch (e) {
                    device.getParameterByName("DGW_ballastVendor" + counter).value = "Unknown";
                }
            } else {
                device.getParameterByName("DGW_ballastShort" + counter).value = "-";
                device.getParameterByName("DGW_ballastType" + counter).value = "-";
                device.getParameterByName("DGW_ballastVendor" + counter).value = "-";
            }

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

function dali_readInfo(device, online, progress, context) {
    // Reads GTIN, firmware version, serial number, hardware version and the
    // physical minimum arc-power level from Memory Bank 0 (IEC 62386-102).
    // Note: DALI does not expose a manufacturer name, article number or device
    // name as readable text - only these numeric identity fields are available.
    var prefix = "DGW" + "_" + context.Channel;

    try {
        progress.setText("Reading device info");

        var data = [FUNC_READ_INFO, context.Channel - 1];
        online.connect();
        var resp = online.invokeFunctionProperty(DALI_OBJECT_INDEX, DALI_PROPERTY_ID, data);

        if (resp[0] != 0)
            throw new Error("Dali Error: " + resp[0]);

        var errorByte = resp[21];
        if (errorByte & ERR_INFO_MEMORY_READ)
            throw new Error("Gear did not respond to Memory Bank 0 read");

        var gtin = bytesToDecimal(resp, 1, 7);
        var gtinInfo = parseGtin(gtin);
        setPara(device, prefix + "gtin", gtin);
        setPara(device, prefix + "vendor", gtinInfo.vendor);
        setPara(device, prefix + "articleNumber", gtinInfo.articleNumber);
        setPara(device, prefix + "firmwareVersion", resp[7] + "." + resp[8]);
        setPara(device, prefix + "serialNumber", bytesToDecimal(resp, 9, 17));
        setPara(device, prefix + "hardwareVersion", resp[17] + "." + resp[18]);

        if (errorByte & ERR_INFO_PHYSMIN)
            setPara(device, prefix + "physMinLevel", "0");
        else
            setPara(device, prefix + "physMinLevel", arcToPercent(resp[20]).toFixed(2).replace(".", ","));

        if (errorByte & ERR_INFO_LEGACY_BANK0)
            progress.setText("Read (legacy DALI gear, serial number may be incomplete)");
        else
            progress.setText("Device info read successfully");
    } catch (e) {
        clearDeviceInfoParas(device, prefix);
        progress.setText("Could not read device info: " + e.message);
    }
}