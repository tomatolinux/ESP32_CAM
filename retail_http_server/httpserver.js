require('date-utils');


var dt = new Date();
var d = dt.toFormat('YYYYMMDD_HH24MISS');
console.log('[' + d + ']');

http = require('http');
fs = require('fs');
server = http.createServer( function(req, res) {
    //console.dir(req.param);
    if (req.method == 'POST') {
		dt = new Date();
		d = dt.toFormat('YYYYMMDD_HH24MISS');
		console.log('[' + d + ']');
        var f = fs.createWriteStream(d+'.jpg');
        req.on('data', function (data) {			
            f.write(data);
        });
        req.on('end', function () {
			console.log('end');
            f.end();
        });
    }
});

port = 80;
host = '192.168.1.83';
server.listen(port, host);
console.log('Listening at http://' + host + ':' + port);
console.log('TimeOut :'+server.timeout);
server.timeout = 10000;
console.log('TimeOut :'+server.timeout);