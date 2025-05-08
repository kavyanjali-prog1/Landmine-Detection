#include <AITINKR_AIOT_V2.h> #include "esp_camera.h"
#include <WiFi.h> #include <ESPmDNS.h>
#include "esp_http_server.h" #include "esp_timer.h" #include "img_converters.h" #include "Arduino.h" #include <TinyGPS++.h> #include <HardwareSerial.h>
//
// Camera Pin Configuration
//
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 21
#define SIOC_GPIO_NUM 14
#define Y2_GPIO_NUM 16

#define Y3_GPIO_NUM 7
#define Y4_GPIO_NUM 6

#define Y5_GPIO_NUM 15
#define Y6_GPIO_NUM 17
#define Y7_GPIO_NUM 8
#define Y8_GPIO_NUM 9
#define Y9_GPIO_NUM 11

#define VSYNC_GPIO_NUM 13
#define HREF_GPIO_NUM 12
#define PCLK_GPIO_NUM 18

//
// Extra Hardware Definitions
//
const int landminePin = 1; // Land mine sensor input const int ledPin = 2;		// LED control pin3ww22 bool ledState = 0;	// LED initial state
//
// WiFi Credentials
//
const char *ssid = "jai9";
const char *password = "a1234567";
//
// GPS Setup (using UART2 on pins 4 (RX) and 47(TX))

//
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // Use UART2 for GPS String latitude = "GPS Data Not Available";
String longitude = "GPS Data Not Available";
 
// Function to update GPS data void readGPS() {
while (gpsSerial.available() > 0) { gps.encode(gpsSerial.read());
}
if (gps.location.isValid()) {
latitude = String(gps.location.lat(), 6); longitude = String(gps.location.lng(), 6);
} else {
latitude = "GPS Data Not Available"; longitude = "GPS Data Not Available";
}
}
//
// Global Objects for Camera Streaming & Motor Control
//
motor motor;

// Structure for JPEG chunking typedef struct {
httpd_req_t *req; size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static	const	char	*_STREAM_CONTENT_TYPE	=	"multipart/x-mixed- replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent- Length: %u\r\n\r\n";

// HTTP server handles
httpd_handle_t stream_httpd = NULL; // For streaming (typically port 81)
httpd_handle_t camera_httpd = NULL; // For control endpoints (typically port 80)

//
// JPEG Streaming Functions
//
static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {

jpg_chunking_t *j = (jpg_chunking_t *)arg; if (index == 0) {
j->len = 0;
}
if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) { return 0;
}
j->len += len; return len;
}
//
// Capture Handler (for still image capture)
//
static esp_err_t capture_handler(httpd_req_t *req) { camera_fb_t *fb = esp_camera_fb_get();
if (!fb) {
Serial.println("Camera capture failed"); httpd_resp_send_500(req);
return ESP_FAIL;
}
httpd_resp_set_type(req, "image/jpeg");
httpd_resp_set_hdr(req,"Content-Disposition","inline; filename=capture.jpg");
esp_err_t res;
if (fb->format == PIXFORMAT_JPEG) {
res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
} else {
jpg_chunking_t jchunk = { req, 0 };
res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
httpd_resp_send_chunk(req, NULL, 0);
}
esp_camera_fb_return(fb); return res;
}
//
// Stream Handler (for continuous video streaming)
//
static esp_err_t stream_handler(httpd_req_t *req) { camera_fb_t *fb = NULL;
esp_err_t res = ESP_OK; size_t _jpg_buf_len = 0; uint8_t *_jpg_buf = NULL;
char part_buf[64]; // 64-byte header buffer

res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE); if (res != ESP_OK) {
return res;
}
while (true) {
fb = esp_camera_fb_get(); if (!fb) {
Serial.println("Camera capture failed"); res = ESP_FAIL;
break;
}
if (fb->format != PIXFORMAT_JPEG) {
bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len); esp_camera_fb_return(fb);
if (!jpeg_converted) {
Serial.println("JPEG compression failed"); res = ESP_FAIL;
break;
}
} else {
_jpg_buf_len = fb->len;
_jpg_buf = fb->buf;
}
size_th len = snprintf(part_buf, sizeof(part_buf), _STREAM_PART,
_jpg_buf_len);
res = httpd_resp_send_chunk(req, part_buf, hlen); if (res != ESP_OK) break;
res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len); if (res != ESP_OK) break;
res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, 
strlen(_STREAM_BOUNDARY));
if (res != ESP_OK) break; if (fb) { esp_camera_fb_return(fb);
}
}
return res;
}


//
// Extra Feature: Status Handler
// Returns JSON with land mine status, GPS coordinates, and LED state
//
static esp_err_t status_handler(httpd_req_t *req) { int pinState = !digitalRead(landminePin);
String landmineStatus = (pinState == LOW) ? "⚠ Land Mine Detected!" : "✅ No Land Mine";
String ledText = ledState ? "ON" : "OFF"; readGPS(); // Update GPS data

String	jsonResponse	=	"{\"landmine\":\""	+	landmineStatus	+	"\",
\"latitude\":\"" + latitude + "\", \"longitude\":\"" + longitude + "\", \"ledState\":\""
+ ledText + "\"}";
httpd_resp_set_type(req, "application/json");
return httpd_resp_send(req, jsonResponse.c_str(), jsonResponse.length());
}
	
//
// Extra Feature: Toggle LED Handler
//
static esp_err_t toggleLED_handler(httpd_req_t *req) { ledState = !ledState;
digitalWrite(ledPin, ledState ? LOW : HIGH); httpd_resp_set_type(req, "text/plain");
return httpd_resp_send(req, ledState ? "LED OFF" : "LED ON", -1);
}

//
// HTML Index Handler (Unified Control Page)
//
static esp_err_t index_handler(httpd_req_t *req) { httpd_resp_set_type(req, "text/html");
String page = "";
page += "<html><head>";
page	+=	"<meta	name='viewport'	content='width=device-width,	initial- scale=1.0, maximum-scale=1.0, user-scalable=0'>";
page += "<style>";
page += "body { font-family: Arial, sans-serif; text-align: center; background- color: #f4f4f4; }";
page += ".container { background: white; padding: 20px; border-radius: 10px; box-shadow: 0px 4px 8px rgba(0,0,0,0.2); max-width: 400px; margin: auto; }";
page += ".button { background: #28a745; color: white; border: none; padding: 10px 20px; font-size: 18px; border-radius: 5px; cursor: pointer; margin:5px; }";
page += ".button:hover { background: #218838; }"; page += "</style>";
page += "<script>";
page += "function sendCommand(cmd){";
page += " var xhttp = new XMLHttpRequest();";
page += " xhttp.open('GET', cmd+'?'+new Date().getTime(), true);"; page += " xhttp.send();";
page += "}";
page += "function updateStatus(){";
page += " var xhttp = new XMLHttpRequest();"; page += " xhttp.onreadystatechange = function(){";
page += "if(this.readyState==4 && this.status==200){"; page += “var resp = JSON.parse(this.responseText);";
page	+=	"document.getElementById('landmine').innerHTML = resp.landmine;";
page += "	document.getElementById('latitude').innerHTML = resp.latitude;";
page	+=	"document.getElementById('longitude').innerHTML= resp.longitude;";
page	+=	"document.getElementById('mapsLink').href = 'https://www.google.com/maps?q=' + resp.latitude + ',' + resp.longitude;";
page	+=	"document.getElementById('ledState').innerHTML= resp.ledState;";
page += "	}";
page += " };";
page += " xhttp.open('GET', '/status?'+new Date().getTime(), true);";
page += " xhttp.send();"; page += "}";
page += "function toggleLED(){";
page += " var xhttp = new XMLHttpRequest();"; page += " xhttp.open('GET', '/toggleLED', true);"; page += " xhttp.send();";
page += "}";
page += "setInterval(updateStatus, 1000);"; page += "</script></head><body>";
page += "<div class='container'>";
page += "<h2>GMAC LANDMINE ROBOT </h2>";
// Camera Streaming
page	+=	"<img src='http://robot.local:81/stream' style='width:300px; transform:rotate(180deg);'><br><br>"
// Motor Controls
page	+=	"<button class='button' onmousedown=\"sendCommand('/go')\" onmouseup=\"sendCommand('/stop')\">Forward</button><br>";
page	+=	"<button class='button' onmousedown=\"sendCommand('/left')\" onmouseup=\"sendCommand('/stop')\">Left</button>";
page +="<button class='button' onclick=\"sendCommand('/stop')\">Stop</button>";
page += "<button class='button' onmousedown=\"sendCommand('/right')\" onmouseup=\"sendCommand('/stop')\">Right</button><br>";
page += "<button class='button' onmousedown=\"sendCommand('/back')\" onmouseup=\"sendCommand('/stop')\">Backward</button><br><br>";
// Extra Features
page +="<p><b>LandMineStatus:</b><span id='landmine'>Checking...</span></p>"
 page += "<p><b>GPS Location:</b> <span id='latitude'>Loading...</span>,
<span id='longitude'>Loading...</span></p>";
page += "<p><a id='mapsLink' href='#' target='_blank' class='button'>View on Google Maps</a></p>";
page += "<p><b>LED Status:</b> <span id='ledState'>OFF</span></p>";
page	+=	"<button	class='button'	onclick='toggleLED()'>Toggle LED</button>";
page += "</div></body></html>";
return httpd_resp_send(req, page.c_str(), page.length());
}
//
// Motor Control Handlers
//
static esp_err_t go_handler(httpd_req_t *req) { motor.forword();
Serial.println("Go"); httpd_resp_set_type(req, "text/html"); return httpd_resp_send(req, "OK", 2);
}
static esp_err_t back_handler(httpd_req_t *req) { motor.backword();
Serial.println("Back"); httpd_resp_set_type(req, "text/html"); return httpd_resp_send(req, "OK", 2);
}
static esp_err_t left_handler(httpd_req_t *req) { motor.left();
Serial.println("Left"); httpd_resp_set_type(req, "text/html"); return httpd_resp_send(req, "OK", 2);
}
static esp_err_t right_handler(httpd_req_t *req) { motor.right();
Serial.println("Right"); httpd_resp_set_type(req, "text/html"); return httpd_resp_send(req, "OK", 2);
}
static esp_err_t stop_handler(httpd_req_t *req) { motor.stop();
Serial.println("Stop"); httpd_resp_set_type(req, "text/html"); return httpd_resp_send(req, "OK", 2);
}
//
// Start Camera & Control Server
//
void startCameraServer() { motor.init(); motor.setSpeed(180); motor.setOffset(0);
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
// Endpoints for the control server (port 80)
httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
httpd_uri_t go_uri = { .uri = "/go", .method = HTTP_GET, .handler = go_handler, .user_ctx = NULL };
httpd_uri_t back_uri = { .uri = "/back", .method = HTTP_GET, .handler = back_handler, .user_ctx = NULL };
httpd_uri_t left_uri = { .uri = "/left", .method = HTTP_GET, .handler = left_handler, .user_ctx = NULL };
httpd_uri_t right_uri = { .uri = "/right", .method = HTTP_GET, .handler = right_handler, .user_ctx = NULL };
httpd_uri_t stop_uri = { .uri = "/stop", .method = HTTP_GET, .handler = stop_handler, .user_ctx = NULL };
// Extra endpoints for status and LED toggle
httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL };
httpd_uri_t toggleLED_uri = { .uri = "/toggleLED", .method = HTTP_GET,
.handler = toggleLED_handler, .user_ctx = NULL };
// Initialize (dummy) filtering; not used for diagnostics in this simple version.
// (You may remove the following line if filtering is not needed.)
// ra_filter_init(&ra_filter, 20);
Serial.printf("Starting control server on port: '%d'\n", config.server_port); if (httpd_start(&camera_httpd, &config) == ESP_OK) { httpd_register_uri_handler(camera_httpd, &index_uri); httpd_register_uri_handler(camera_httpd, &go_uri); httpd_register_uri_handler(camera_httpd, &back_uri); httpd_register_uri_handler(camera_httpd, &left_uri); httpd_register_uri_handler(camera_httpd, &right_uri); httpd_register_uri_handler(camera_httpd, &stop_uri); httpd_register_uri_handler(camera_httpd, &status_uri); httpd_register_uri_handler(camera_httpd, &toggleLED_uri);
}
// Start the streaming server on the next port (usually port 81) config.server_port += 1;
config.ctrl_port += 1;
Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
if (httpd_start(&stream_httpd, &config) == ESP_OK) { httpd_register_uri_handler(stream_httpd, &stream_uri);
}
}
//
// Setup and Loop
//
void setup() { Serial.begin(115200); Serial.setDebugOutput(true); Serial.println();
// Initialize extra hardware pins pinMode(landminePin, INPUT); pinMode(ledPin, OUTPUT); digitalWrite(ledPin, HIGH);
// Start GPS on UART2 (RX=GPIO4, TX=GPIO47)
gpsSerial.begin(9600, SERIAL_8N1, 4, 47);
// Configure camera for streaming camera_config_t config;
config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0; config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM; config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM; config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM; config.xclk_freq_hz = 20000000; config.frame_size = FRAMESIZE_UXGA;
config.pixel_format = PIXFORMAT_JPEG; // for streaming
config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; config.fb_location = CAMERA_FB_IN_PSRAM; config.jpeg_quality = 12;
config.fb_count = 1;
if (config.pixel_format == PIXFORMAT_JPEG) { if (psramFound()) {
config.jpeg_quality = 10;
config.fb_count = 2;
config.grab_mode = CAMERA_GRAB_LATEST;
} else {
config.frame_size = FRAMESIZE_SVGA; config.fb_location = CAMERA_FB_IN_DRAM;
}
} else {
config.frame_size = FRAMESIZE_240X240; #if CONFIG_IDF_TARGET_ESP32S3
config.fb_count = 2; #endif
}
esp_err_t err = esp_camera_init(&config); if (err != ESP_OK) {
Serial.printf("Camera init failed with error 0x%x", err); return;
}
sensor_t *s = esp_camera_sensor_get(); s->set_framesize(s, FRAMESIZE_CIF);
// Connect to WiFi using provided credentials Serial.println("Connecting to WiFi..."); WiFi.begin("jai9", "a1234567");
int attempts = 0;
while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500);
Serial. Print("."); attempts++;
}
if (WiFi.status() == WL_CONNECTED) {
Serial.println("\nWiFi Connected! IP Address: " + WiFi.localIP().toString());
} else {
Serial.println("\nWiFi Connection Failed!");
}
startCameraServer();
if (MDNS.begin("robot")) {
Serial.println("mDNS responder started. Open http://robot.local");
} else {
Serial.println("Error setting up mDNS responder");
}
}
void loop() {
// All tasks are handled by HTTP server callbacks.
}