var data = [];
var freq = 100;

function sendData() {
	var client = new XMLHttpRequest();
	client.open("POST", "http://mgabor.hu/accel/post.php");
	client.setRequestHeader("Content-Type", "application/x-www-form-urlencoded charset=UTF-8");
	client.send("uid="+Pebble.getAccountToken()+"&freq="+freq+"&data="+JSON.stringify(data));
	//console.log(Pebble.getAccountToken());
	client.onreadystatechange = function() {
		if (this.readyState === 4) {
			if (this.status === 200) { 
				console.log(client.responseText);
				//Pebble.showSimpleNotificationOnPebble("UID", "To view your graphs, enter the following code at http://mgabor.hu/accel/\n"+Pebble.getAccountToken());
			}
		}
	};
	data = [];
}

Pebble.addEventListener("ready", function(e) {
	console.log("Javascript initialized.");

	var uid = Pebble.getAccountToken();
	var formattedUID =	uid.substring(0,  4)  + " " + uid.substring(4,  8)  + " " + uid.substring(8,  12) + " " + uid.substring(12, 16) + "\n" +
						uid.substring(16, 20) + " " + uid.substring(20, 24) + " " + uid.substring(24, 28) + " " + uid.substring(28, 32);

	Pebble.sendAppMessage( { "0": formattedUID },
		function(e) {
			console.log("Successfully delivered message with transactionId=" + e.data.transactionId);
			},
		function(e) {
			console.log("Unable to deliver message with transactionId=" + e.data.transactionId + " Error is: " + e.error.message);
		}
	);

	Pebble.addEventListener("appmessage",
		function(e) {
			console.log("Received message: " + JSON.stringify(e.payload));
			if (e.payload["1"] != -1) {
				var inbox = e.payload["2"];
				for (var i = 0; i < inbox.length; i += 6) {
					data[data.length] = [inbox[i+0]+inbox[i+1]*256-5000, inbox[i+2]+inbox[i+3]*256-5000, inbox[i+4]+inbox[i+5]*256-5000];
				}
			} else {
				console.log("Received EOF, uploading data...");
				//console.log("Received EOF, data: " + JSON.stringify(data).substring(0,400));
				sendData();
			}
		}
	);
});
