"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

/*! This object contains one entry for each div which needs to have its data validated.
What it means: the current wizard step has been completed and we're going to step "funcName",
validate the input and pull it in internal state.
If no error is returned the wizard will go to the specified step and 
updateContents is called. When those are called, previous step is still in the document. New step is not. */
var fetchWizardInput = {
    wzNonsensicalPoolDiffMultiplier: function() {
        var algo = document.getElementById("algoSelect").value;
        if(algo.length === 0) return "An algorithm must be selected.";
        window.wizConfig.algo = algo;
    },
    wzPool: function() {
        var input = document.getElementById("poolDiffMul");
        if(!Math.floor(input.valueAsNumber)) return "Pool difficulty factor must be at least 1.";
        window.wizConfig.feudalDiffOverride = Math.floor(input.valueAsNumber);
    },
    wzHWIntensity: function() {
        var pool = document.getElementById("poolURL").value;
        if(pool.length === 0) return "Pool URL is empty - this is surely wrong!";
        window.wizConfig.poolURL = pool;
        var worker = document.getElementById("login").value;
        if(worker.length === 0) return "No worker login provided!";
        window.wizConfig.workerLogin = worker;
        var pass = document.getElementById("password").value;
        if(pass.length === 0) return "A password must be specified!";
        window.wizConfig.workerPass = pass;
        var pname = document.getElementById("poolName").value;
        if(pname.length === 0) pname = "[p0]";
        window.wizConfig.poolName = pname;
    },
    wzSaveNReboot: function() {
        // this is a percentage of reference intensity
        window.wizConfig.hwScaledIntensity = document.getElementById("hwScaledInt").valueAsNumber / 100.0;
    }
};


var updateContents = {
    wzNonsensicalPoolDiffMultiplier: function() {
        var diffMuls = coinDiffByAlgo(window.wizConfig.algo);
        var input = document.getElementById("poolDiffMul");
        input.value = diffMuls.stratum;
    },
    wzPool: function() {
        document.getElementById("selectedPoolAlgo").textContent = window.wizConfig.algo;
        document.getElementById("poolURL").focus();
    }
};


function wizard_goto(target) {
    var match;
    for(match = 0; match < easyConfigSteps.length; match++) {
        if(target === easyConfigSteps[match].id) break;
    }
    if(match === easyConfigSteps.length) {
        alert("BROKEN! No such wizard step named \"" + target + "\"!");
        return;
    }
    var errorMsg = window.fetchWizardInput[target]();
    if(errorMsg) {
        alert(errorMsg);
        return;
    }
    var container = document.getElementById("administration");
    container.innerHTML = "";
    container.appendChild(easyConfigSteps[match]);
    if(window.updateContents[target]) window.updateContents[target]();
}

function newConfigSaveAndReboot() {
    var container = document.createElement("div");
    document.getElementById("administration").appendChild(container);
    container.innerHTML += "Probing hardware...";
    window.minerMonitor.requestSimple("systemInfo", function(obj) {
        var gpu = eligibleGPUListFromPlatformArray(obj.platforms);
        if(gpu.length) {
            var string = " " + gpu.length + " GPU" + (gpu.length > 1? "s" : "") + " found.";
            container.innerHTML += string;
        }
        else {
            var err = document.createElement("span");
            err.className = "hugeError";
            err.textContent = " no GPUs found!";
            container.appendChild(err);
            container.innerHTML += "<br><strong>Settings not modified.</strong> Operation cancelled. Something must be wrong with your system.";
            return;
        }
        var slowest = 0;
        var lowestPerf = gpu[0].coreClock * gpu[0].clusters;
        for(var test = 1; test < gpu.length; test++) {
            var estimate = gpu[test].coreClock * gpu[test].clusters;
            if(estimate < lowestPerf) {
                slowest = test;
                lowestPerf = estimate;
            }
        }
        
        // The reference is my Radeon 7750 Capeverde.
        // It got 8 clusters @ 850 Mhz
        var reference = reference_parallelGPUClocks();
        var ratio = lowestPerf / reference;
        var msg = "<br>Slowest device: ";
        if(ratio == 1.0) msg += "just as fast as reference!";
        else {
            if(ratio > 1.0) msg += ratio + " faster";
            else msg += (1.0 / ratio) + " slower"; 
            msg += " than reference.";
        }
        container.innerHTML += msg;
        
        var linearIntensity = reference_linearIntensity() * window.wizConfig.hwScaledIntensity;
        linearIntensity = Math.floor(linearIntensity * ratio);
        container.innerHTML += "<br><span class='specificText'>linearIntensity: " + linearIntensity + "</span>";
        
		var implParams = { };
		implParams.linearIntensity = linearIntensity;
		implParams.impl = preferredImplementation[window.wizConfig.algo];
		var algoFamilies = { };
		algoFamilies[window.wizConfig.algo] = [];
		algoFamilies[window.wizConfig.algo][0] = implParams;
        var cmd = {
            command: "saveRawConfig",
            params: {
                destination: window.wizConfig.targetConfigFile,
                configuration: {
                    pools: [],
                    driver: "OCL",
                    algo: window.wizConfig.algo,
                    implParams: algoFamilies
                }
            }
        };
        cmd.params.configuration.pools[0] = {
            url: stripProtocol(window.wizConfig.poolURL),
            user: window.wizConfig.workerLogin,
            pass: window.wizConfig.workerPass,
            diffMultipliers: coinDiffByAlgo(window.wizConfig.algo),
            algo: window.wizConfig.algo,
            protocol: "stratum"
        };
        if(window.wizConfig.feudalDiffOverride !== undefined)
            cmd.params.configuration.pools[0].diffMultipliers.stratum = window.wizConfig.feudalDiffOverride;
        var diffMode = coinDiffModeByAlgo(window.wizConfig.algo);
        if(diffMode) cmd.params.configuration.pools[0].diffMode = diffMode;
        if(window.wizConfig.poolName && window.wizConfig.poolName.length) {
            cmd.params.configuration.pools[0].name = window.wizConfig.poolName;
        }
		document.getElementById('saveAndRebootBTN_new').disabled = true;
        minerMonitor.request(cmd, function(reply) { serverCFGSaved(reply, container); });
    });
}

    
function stripProtocol(string) {
    var away = "stratum+";
    if(string.substr(0, away.length) !== away) return string;
    return string.substr(away.length);
}


function serverCFGSaved(reply, container) {
    if(!reply) {
        var err = "<span class='hugeError'>ERROR: miner did not save the given configuration.</span><br>";
        err += "State not changed. Nothing to do.";
        container.innerHTML += err;
        return;
    }
    minerMonitor.callbacks.close = function() {
        reloadCallback("closed");
    };
    minerMonitor.requestSimple("reload", function(reply) { reloadCallback(reply, container) });
}

    
var reloadCallbackProducedOutput = true;
function reloadCallback(obj, container) {
    if(!reloadCallbackProducedOutput) return;
    var msg;
    if(obj === true) {
        msg = "Miner will restart with open connection.<br>"
        msg += "<em>Please wait a few seconds before attempting to connect again.</em>";
    }
    else if(obj === "closed") {
        msg = "Connection closed with no <span class='specificText'>reload</span> reply.";
        obj = false;
    }
    if(obj === false) {
        msg = "Miner will restart with closed ports.<br>";
        msg += "<em>To connect again, you'll have to open port by using miner menu first.</em>";
    }
    // this could still happen if connection closed before reply mangled
    if(container) {
        var div = document.createElement("div");
        div.innerHTML = "<br><br>" + msg + "<br><br>We're done here.";
        container.appendChild(div);
        reloadCallbackProducedOutput = false;
    }
}

/*! This function returns the "difficulty multipliers" of a coin.
In legacy miners it is a function of the algo itself. In M8M, it is fully decoupled and data driven.

There are three multipliers. The first is a number to be multiplied by the value received by stratum
set_difficulty notification. It is ... unfortunately ... very often overriden by pool operators as they
feel good about it. Those are therefore only default values.
"one" is a multiplier by TRUE_DIFF_ONE. The above makes up a numerator and this value * TRUE_DIFF_ONE
makes the denominator.
The "share" multiplier is used to figure out the "value" of a share. It's currently unused but while we're at it... */
function coinDiffByAlgo(algo) {
    var diffMul = {
        qubit:     { stratum: 256, one:   256, share:   256 },
        fresh:     { stratum:   1, one:   256, share:   256 },
        grsmyr:    { stratum:   1, one:     1, share:     1 },
        neoScrypt: { stratum:   1, one: 65536, share: 65536 },
        lyra2RE:   { stratum:   1, one:   128, share:   128 },
        BSTY_YEScrypt:  { stratum:   1, one: 65536, share: 65536 }
    };            // ^ can be overriden by pool settings
    var diff = diffMul[algo];
    if(diff === undefined) throw "Unrecognized algorithm: \"" + algo + "\".";
    return diff;
}


// Difficulty calculation mode. Neoscrypt had this idea of introducing a new one.
// Maybe it makes more sense but to me it's just a nuisance.
// Needs to be specified if non-null.
function coinDiffModeByAlgo(algo) {
    var diffMode = {
        // Only need to specify nonstandard diff algos here.
        neoScrypt: "neoScrypt"
    };
    var diff = diffMode[algo];
    if(diff === undefined) return null;
    return diff;
}
