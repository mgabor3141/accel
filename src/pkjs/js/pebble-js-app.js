var data = [];
var recid = 0;
var recnum = 0;

var freq_stored = 1;

var MAX_SAMPLES = 1000;

// ### SEND DATA
/*	
 *	uid, freq is sent with every msg, every blob
 *  freq is 0 for last blob
 *	data is the current recording, up to 7000*75 samples
 *	id is 0 in first recording, returned by php after
 *	recnum is blob number
 */
function sendData(freq) {
	var datacopy = data;
	data = [];
	
	var toSend = {
		freq: freq_stored,
		uid: Pebble.getAccountToken(),
		data: datacopy,
		id: recid,
		recnum: recnum
	};
	
	var client = new XMLHttpRequest();
	client.open("POST", "http://mgabor.hu/accel/post.php");
	client.setRequestHeader("Content-Type", "application/json;charset=UTF-8");
	client.send(JSON.stringify(toSend));
	
	client.onreadystatechange = function() {
		if (this.readyState === 4) {
			if (this.status === 200) {
				console.log("PHP Response (DB id): " + client.responseText);
				recid = parseInt(client.responseText);
				
				datacopy = [];
				recnum++;
				
				if (freq === 0) {
					recid = 0;
					recnum = 0;
				}
			}
		}
	};
}

Pebble.addEventListener("ready", function(e) {
	// ### PREPARE UID STRING ###
	var uid = Pebble.getAccountToken();
	var formattedUID =	uid.substring(0,  4)  + " " + uid.substring(4,  8)  + " " + uid.substring(8,  12) + " " + uid.substring(12, 16) + "\n" +
						uid.substring(16, 20) + " " + uid.substring(20, 24) + " " + uid.substring(24, 28) + " " + uid.substring(28, 32);

	Pebble.sendAppMessage( { "0": formattedUID },
		function(e) {
			console.log("JS initialized, UID string sent (id=" + e.data.transactionId+")");
			},
		function(e) {
			console.log("Unable to deliver message with transactionId=" + e.data.transactionId + " Error is: " + e.error.message);
		}
	);

	// ### MESSAGE RECIEVE HANDLER
	Pebble.addEventListener("appmessage",
		function(e) {
			//console.log("Received message: " + JSON.stringify(e.payload));
			
			var inbox = e.payload["2"];
			for (var i = 0; i < inbox.length; i += 6) {
				data[data.length] = [inbox[i+0]+inbox[i+1]*256-5000, inbox[i+2]+inbox[i+3]*256-5000, inbox[i+4]+inbox[i+5]*256-5000];
			}
			
			if (e.payload["3"] == 10 || e.payload["3"] == 25 || e.payload["3"] == 50 || e.payload["3"] == 100)
				freq_stored = e.payload["3"];
			
			if (data.length >= MAX_SAMPLES || e.payload["3"] === 0) {
				sendData(freq_stored);
			}
		}
	);
});
