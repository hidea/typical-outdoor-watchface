// Calculates sunrise/sunset minutes-since-midnight (local time)
// using NOAA simplified solar position algorithm.
// lat/lon in decimal degrees, date is a JS Date object.
function calcSunriseSunset(lat, lon, date) {
  var RAD = Math.PI / 180;

  var start = new Date(date.getFullYear(), 0, 0);
  var dayOfYear = Math.floor((date - start) / 86400000);

  var gamma = (2 * Math.PI / 365) * (dayOfYear - 1);

  var eqtime = 229.18 * (
    0.000075 +
    0.001868 * Math.cos(gamma) - 0.032077 * Math.sin(gamma) -
    0.014615 * Math.cos(2 * gamma) - 0.04089 * Math.sin(2 * gamma)
  );

  var decl = 0.006918 -
    0.399912 * Math.cos(gamma) + 0.070257 * Math.sin(gamma) -
    0.006758 * Math.cos(2 * gamma) + 0.000907 * Math.sin(2 * gamma) -
    0.002697 * Math.cos(3 * gamma) + 0.00148  * Math.sin(3 * gamma);

  var latRad = lat * RAD;
  var cosHa = Math.cos(90.833 * RAD) / (Math.cos(latRad) * Math.cos(decl)) -
              Math.tan(latRad) * Math.tan(decl);

  if (cosHa < -1 || cosHa > 1) return null;

  var ha = Math.acos(cosHa);
  var tzOffset = -date.getTimezoneOffset();

  var sunrise = Math.round(720 - 4 * (lon + ha / RAD) - eqtime + tzOffset);
  var sunset  = Math.round(720 - 4 * (lon - ha / RAD) - eqtime + tzOffset);

  return {
    sunrise: ((sunrise % 1440) + 1440) % 1440,
    sunset:  ((sunset  % 1440) + 1440) % 1440
  };
}

function sendSunTimes(lat, lon) {
  var today    = new Date();
  var tomorrow = new Date(today);
  tomorrow.setDate(today.getDate() + 1);

  var t = calcSunriseSunset(lat, lon, today);
  var m = calcSunriseSunset(lat, lon, tomorrow);

  if (!t || !m) {
    console.log('Polar day/night — no sunrise/sunset data');
    return;
  }

  Pebble.sendAppMessage({
    'SUNRISE_TODAY':    t.sunrise,
    'SUNSET_TODAY':     t.sunset,
    'SUNRISE_TOMORROW': m.sunrise,
    'SUNSET_TOMORROW':  m.sunset
  }, function() {
    console.log('Sun times sent: rise=' + t.sunrise + ' set=' + t.sunset);
  }, function(e) {
    console.log('sendAppMessage error: ' + JSON.stringify(e));
  });
}

function getLocation() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      sendSunTimes(pos.coords.latitude, pos.coords.longitude);
    },
    function(err) {
      console.log('Geolocation error: ' + err.message);
    },
    { timeout: 15000, maximumAge: 3600000 }
  );
}

Pebble.addEventListener('ready', function() {
  getLocation();
});

// Watch requests refresh at midnight
Pebble.addEventListener('appmessage', function() {
  getLocation();
});
