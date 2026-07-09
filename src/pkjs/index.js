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

var THEME_KEY = 'colorTheme';
var REVERSE_KEY = 'colorReverse';
var DEFAULT_THEME = 0;

var THEMES = [
  { value: 0, label: 'Graphite' },
  { value: 1, label: 'Blueberry' },
  { value: 2, label: 'Grape' },
  { value: 3, label: 'Tangerine' },
  { value: 4, label: 'Lime' },
  { value: 5, label: 'Strawberry' }
];

function getTheme() {
  var saved = parseInt(localStorage.getItem(THEME_KEY), 10);
  return isNaN(saved) ? DEFAULT_THEME : saved;
}

function getReverse() {
  return localStorage.getItem(REVERSE_KEY) === '1';
}

function sendTheme() {
  Pebble.sendAppMessage({
    'COLOR_THEME': getTheme(),
    'COLOR_REVERSE': getReverse() ? 1 : 0
  }, function() {
    console.log('Color theme sent: ' + getTheme() + ', reverse=' + getReverse());
  }, function(e) {
    console.log('send color theme error: ' + JSON.stringify(e));
  });
}

function configurationHtml() {
  var current = getTheme();
  var reverseChecked = getReverse() ? ' checked' : '';
  var options = THEMES.map(function(theme) {
    var selected = theme.value === current ? ' selected' : '';
    return '<option value="' + theme.value + '"' + selected + '>' + theme.label + '</option>';
  }).join('');

  return '<!doctype html>' +
    '<html><head><meta name="viewport" content="width=device-width, initial-scale=1">' +
    '<title>Typical Outdoor</title>' +
    '<style>' +
    'body{margin:0;background:#111;color:#eee;font-family:-apple-system,BlinkMacSystemFont,Helvetica,Arial,sans-serif}' +
    'main{padding:24px;max-width:480px;margin:0 auto}' +
    'h1{font-size:22px;margin:0 0 24px}' +
    'label{display:block;font-size:13px;color:#aaa;margin-bottom:8px;text-transform:uppercase;letter-spacing:.04em}' +
    'select,button{box-sizing:border-box;width:100%;font-size:18px;border-radius:8px;border:1px solid #444;padding:12px;background:#1f1f1f;color:#fff}' +
    '.checkbox{display:flex;align-items:center;gap:10px;margin-top:18px;color:#eee;font-size:18px}' +
    '.checkbox input{width:22px;height:22px}' +
    'button{margin-top:20px;background:#00aaff;border-color:#00aaff;color:#001018;font-weight:700}' +
    '</style></head><body><main>' +
    '<h1>Typical Outdoor</h1>' +
    '<label for="theme">Color Theme</label>' +
    '<select id="theme">' + options + '</select>' +
    '<label class="checkbox"><input id="reverse" type="checkbox"' + reverseChecked + '>Reverse</label>' +
    '<button id="save">Save</button>' +
    '<script>' +
    'document.getElementById("save").addEventListener("click",function(){' +
    'var theme=document.getElementById("theme").value;' +
    'var reverse=document.getElementById("reverse").checked;' +
    'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify({colorTheme:parseInt(theme,10),colorReverse:reverse}));' +
    '});' +
    '</script></main></body></html>';
}

Pebble.addEventListener('ready', function() {
  getLocation();
  sendTheme();
});

// Watch requests refresh at midnight
Pebble.addEventListener('appmessage', function() {
  getLocation();
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL('data:text/html,' + encodeURIComponent(configurationHtml()));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;

  try {
    var settings = JSON.parse(decodeURIComponent(e.response));
    if (typeof settings.colorTheme === 'number') {
      localStorage.setItem(THEME_KEY, settings.colorTheme);
    }
    if (typeof settings.colorReverse === 'boolean') {
      localStorage.setItem(REVERSE_KEY, settings.colorReverse ? '1' : '0');
    }
    if (typeof settings.colorTheme === 'number' || typeof settings.colorReverse === 'boolean') {
      sendTheme();
    }
  } catch (err) {
    console.log('Config parse error: ' + err.message);
  }
});
