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

function dali_read(device, online, progress, context) {
    // Start read devicetype
    progress.setText(device.getMessage(calcMessage("DGW_devicetype_read")));

    var data = [2, context.Channel - 1];
    online.connect();
    var resp = online.invokeFunctionProperty(160, 1, data); //invoke readdevicetype

    if (resp[0] != 0) {
        // Dali Error:
        throw new Error(device.getMessage(calcMessage("DGW_dali_error")) + String(resp[0]));
    }

    var prefix = "DGW" + "_" + context.Channel;

    var para = device.getParameterByName(prefix + "deviceType");
    if (resp[1] == 255) {
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
    var data = online.invokeFunctionProperty(160, 1, [11, context.Channel - 1]);
    progress.setProgress(10);
    if (data[0] != 0)
        throw new Error("Dali Error: " + data[0]);
    var errors = "";

    if (data[9] & 1) 
        errors += "Min Level, ";
    else {
        setPara(device, prefix + "min", arcToPercent(data[1]).toFixed(2).replace(".", ","));
    }
    if (data[9] & 2)
        errors += "Max Level, ";
    else {
        setPara(device, prefix + "max", arcToPercent(data[2]).toFixed(2).replace(".", ","));
    }
    if (data[9] & 4) 
        errors += "Power On, ";
    else {
        setPara(device, prefix + "poweron", data[3] == 255);
        if (data[3] != 255)
            setPara(device, prefix + "poweronlevel", arcToPercent(data[3]).toFixed(2).replace(".", ","));
    }
    if (data[9] & 8)
        errors += "Failure On, ";
    else {
        setPara(device, prefix + "failureon", data[4] == 255);
        if (data[4] != 255)
            setPara(device, prefix + "failureonlevel", arcToPercent(data[4]).toFixed(2).replace(".", ","));
    }
    if (data[9] & 16)
        errors += "FadeTime/Rate, ";
    else {
        setPara(device, prefix + "fadeTime", (data[5] >> 4).toString());
        setPara(device, prefix + "fadeRate", (data[5] & 15).toString());
    }
    //1 byte free
    if (data[9] & 64)
        errors += "Groups 0-7, ";
    else {
        setPara(device, prefix + "g0", (data[7] & 1));
        setPara(device, prefix + "g1", ((data[7] >> 1) & 1));
        setPara(device, prefix + "g2", ((data[7] >> 2) & 1));
        setPara(device, prefix + "g3", ((data[7] >> 3) & 1));
        setPara(device, prefix + "g4", ((data[7] >> 4) & 1));
        setPara(device, prefix + "g5", ((data[7] >> 5) & 1));
        setPara(device, prefix + "g6", ((data[7] >> 6) & 1));
        setPara(device, prefix + "g7", ((data[7] >> 7) & 1));
    }
    if (data[9] & 128)
        errors += "Groups 8-15, ";
    else {
        setPara(device, prefix + "g8", (data[8] & 1));
        setPara(device, prefix + "g9", ((data[8] >> 1) & 1));
        setPara(device, prefix + "g10", ((data[8] >> 2) & 1));
        setPara(device, prefix + "g11", ((data[8] >> 3) & 1));
        setPara(device, prefix + "g12", ((data[8] >> 4) & 1));
        setPara(device, prefix + "g13", ((data[8] >> 5) & 1));
        setPara(device, prefix + "g14", ((data[8] >> 6) & 1));
        setPara(device, prefix + "g15", ((data[8] >> 7) & 1));
    }

    progress.setProgress(20);

    data = [
        13,
        context.Channel - 1,
        0, //scene number
        getParaInt(device, prefix + "deviceType"),
        getParaInt(device, prefix + "colorType")
    ];

    for (var i = 0; i < 16; i++)
    {
        progress.setText(device.getMessage(calcMessage("DGW_evgReadScene")) + i.toString()); // Parsing data
        data[2] = i;

        resp = online.invokeFunctionProperty(160, 1, data);

        setPara(device, prefix + "s" + i + "t", resp[0] != 255);

        if (resp[0] != 255) {
            setPara(device, prefix + "s" + i + "v", arcToPercent(resp[0]).toFixed(2).replace(".", ","));
            //deviceType is Color
            if (data[3] == 9) {
                //colorType is TunableWhite
                if (data[4] == 2) {
                    var kelvin = parseInt((resp[1] << 8) | resp[2]);
                    setPara(device, prefix + "s" + i + "ct", kelvin);
                    // data[7] = kelvin &gt;&gt; 8;
                    // data[8] = kelvin &amp; 256;
                } else { //it is RGB
                    var color = (resp[1] << 16) | color;
                    color = (resp[2] << 8) | color;
                    color = resp[3];
                    setPara(device, prefix + "s" + i + "cc", color.toString());
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
    // if(getParaInt("fadeTime") != "0" &amp;&amp; getParaInt("fadeTimeExtendedMultiplier") != "0")
    //     throw new Error(device.getMessage(4000018)); // error 
    progress.setText(device.getMessage(calcMessage("DGW_evgWriteStart"))); // start

    var index = 0;
    var data = [];
    data[index++] = 10;
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
        data[index++] = 255;
        data[index++] = 255;
    }
    if (!getParaBool(device, prefix + "failureon")) {
        temp = getBytes(getParaFloat(device, prefix + "failureonlevel") / 100.0);
        data[index++] = temp >> 8;
        data[index++] = temp & 255;
    } else {
        data[index++] = 255;
        data[index++] = 255;
    }

    var fade = getParaInt(device, prefix + "fadeTime");
    fade = fade << 4;
    fade |= getParaInt(device, prefix + "fadeRate");
    data[index++] = fade;
    data[index++] = 0;//1byte free
    var groups = getParaInt(device, prefix + "g0");
    groups |= getParaInt(device, prefix + "g1") << 1;
    groups |= getParaInt(device, prefix + "g2") << 2;
    groups |= getParaInt(device, prefix + "g3") << 3;
    groups |= getParaInt(device, prefix + "g4") << 4;
    groups |= getParaInt(device, prefix + "g5") << 5;
    groups |= getParaInt(device, prefix + "g6") << 6;
    groups |= getParaInt(device, prefix + "g7") << 7;
    data[index++] = groups;
    groups = getParaInt(device, prefix + "g8");
    groups |= getParaInt(device, prefix + "g9") << 1;
    groups |= getParaInt(device, prefix + "g10") << 2;
    groups |= getParaInt(device, prefix + "g11") << 3;
    groups |= getParaInt(device, prefix + "g12") << 4;
    groups |= getParaInt(device, prefix + "g13") << 5;
    groups |= getParaInt(device, prefix + "g14") << 6;
    groups |= getParaInt(device, prefix + "g15") << 7;
    data[index++] = groups;

    progress.setText(device.getMessage(calcMessage("DGW_evgWriteTransmit"))); // transmit
    online.connect();
    var resp = online.invokeFunctionProperty(160, 1, data);
    progress.setText("after");
    progress.setProgress(20);

    data = [
        12,
        context.Channel - 1, // + 128; only for group!
        0, //scene number
        0, //enabled
        getParaInt(device, prefix + "deviceType"),
        getParaInt(device, prefix + "colorType"),
        0, 0, 0, 0 //will be filled later
    ];

    for (var i = 0; i < 16; i++)
    {
        data[2] = i;
        var isEnabled = getParaBool(device, prefix + "s" + i + "t");
        data[3] = isEnabled;
        if (isEnabled) {
            temp = getBytes(getParaFloat(device, prefix + "s" + i + "v") / 100.0);
            data[6] = temp >> 8;
            data[7] = temp & 255;
        } else {
            data[6] = 255;
            data[7] = 255;
        }

        Log.info("Schreibe Szene " + i);
        Log.info("isEnabled: " + (isEnabled ? "ja" : "nein"));
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

        progress.setText(device.getMessage(calcMessage("DGW_evgWriteScene")) + i); // Übertrage Szene i
        resp = online.invokeFunctionProperty(160, 1, data);
        progress.setProgress(i * 5 + 25);
    }

    online.disconnect();
    progress.setText(device.getMessage(calcMessage("DGW_evgWriteFin"))); // fin
}

function dali_assingAddr(device, online, progress, context) {
    var along = device.getParameterByName("longAddr");
    var ashort = device.getParameterByName("shortAddr");

    //assign address to device
    progress.setText(device.getMessage(calcMessage("DGW_addr_start")));

    var bytes = [4, parseInt(ashort.value)];
    for (var c = 0; c < along.value.length; c += 2)
    bytes.push(parseInt(along.value.substr(c, 2), 16));

    online.connect();

    online.invokeFunctionProperty(160, 1, bytes);

    bytes = [];
    while (true) {
        if (progress.isCanceled()) {
            online.readFunctionProperty(160, 1, [4, 255]);
            return;
        }

        var resp = online.readFunctionProperty(160, 1, [4]);

        if (resp[0] == 0)
            continue; // we are not finished yet

        switch (resp[1]) {
            case 0:
                //address set successfully
                progress.setText(device.getMessage(calcMessage("DGW_addr_success")));
                return;

            case 1:
                //address is already in use
                throw new Error(device.getMessage(calcMessage("DGW_addr_double")));

            case 2:
                //device wont answer
                throw new Error(device.getMessage(calcMessage("DGW_response_timeout")));

            case 3:
                //long address dont exists
                throw new Error(device.getMessage(calcMessage("DGW_addr_long_dont_exists")));

            case 12:
                //short address confirm failed
                throw new Error(device.getMessage(calcMessage("DGW_addr_confirm_failed")));

            default:
                //dali error
                progress.setText(device.getMessage(calcMessage("DGW_dali_error")));
                return;

        }
    }
}

function dali_scan(device, online, progress, context) {
    online.connect();

    var data = [3];
    var para2 = device.getParameterByName("DGW_onlyUnaddressed");
    data.push(parseInt(para2.value));
    para2 = device.getParameterByName("DGW_dontRandomize");
    data.push(parseInt(para2.value));
    para2 = device.getParameterByName("DGW_deleteAll");
    data.push(parseInt(para2.value));
    para2 = device.getParameterByName("DGW_assignNew");
    data.push(parseInt(para2.value));

    //start addressing
    online.invokeFunctionProperty(160, 1, data);
    progress.setText(device.getMessage(calcMessage("DGW_scanStart")));

    for (var i = 0; i < 64; i++)
    {
        var para = device.getParameterByName("DGW_ballast" + i);
        para.value = "";
    }

    var counter = 0;

    while (true) {
        if (progress.isCanceled()) {
            //get State with max devicecount so device will stop and delete variables
            online.readFunctionProperty(160, 1, [7, 254]);
            return;
        }

        var resp = online.readFunctionProperty(160, 1, [3]);
        if (resp[0]) break;
        progress.setText(resp[1] + " " + device.getMessage(calcMessage("DGW_scanFound")));
        progress.setProgress((100.0 / 64) * resp[1]);

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
            online.readFunctionProperty(160, 1, [7, 254]);
            return;
        }

        var resp = online.readFunctionProperty(160, 1, [7, counter]);

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