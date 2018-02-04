/*

Header file with imports and definitions

*/

/*
      Libraries
*/
#include "MAX17043.h"
#include "SparkFunBME280.h"
// #include "Si7021_Particle.h"
#include "Adafruit_TSL2591.h"
#include "Adafruit_Sensor.h"
#include "HttpClient_fast.h"
#include "Adafruit_SSD1306.h"

/*
      Definitions
*/
#define minSleepingDuration 20000
#define maxAwakeDuration 10000

// sound detector pins
#define soundDigitalPin D6
// #define soundAudioPin A0
#define soundEnvelopePin  A1

/*
      Variables
*/

// print info to Serial print switch for debug purpose
// #define serial_debug

// run in threads mode (connectivity and main program are separate threads)
SYSTEM_MODE(SEMI_AUTOMATIC);
SYSTEM_THREAD(ENABLED);

// FSM states
enum State {CONNECT_STATE, AWAKE_STATE, INACTIVE_STATE, SLEEP_STATE };
State state;

// energy mode variable
bool save_mode_on = false;
bool save_mode_switched  = false; // flag indicating a recent change

#ifdef serial_debug
// SerialLogHandler logHandler(115200, LOG_LEVEL_INFO);
SerialLogHandler logHandler(115200, LOG_LEVEL_WARN, {
	{ "app", LOG_LEVEL_INFO },
	{ "app.custom", LOG_LEVEL_INFO }
});
#endif

/*
      Variables
*/

//status vars
bool tsl_on = false;
bool batt_on = false;
bool thp_on = false;
bool display_on = false;

char tsl_settings[21];
char sensors_status[41];
char cloud_settings[41];

// default state for cloud services
bool particle_on = false;
bool influxdb_on = true;

// cloud logging status flags
bool particle_logged = false;
bool influxdb_logged = false;

// control vars
uint32_t senseTime;
uint32_t sendTime;

// timers variables
uint32_t lastWakeUpTime;
uint32_t sleepStartTime_s;
uint32_t sleepingDuration;

//sensors vars
double illuminance = 0;
double humidity = 0;
double temperatureC = 0;
double temperatureF = 0;
double atm_pressure = 0; // not logged to InfluxDB
double battery_p = 0;
double voltage = 0;

// Sensors instances
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591); // pass in a number for the sensor identifier (to id the sensor, optional)
BME280 bme;
// Si7021 si7021;
// battery monitor is declared in its library's header file
// Display instance
Adafruit_SSD1306 display(-1);// I2C mode, No reset pin

// DB serevr response counter
char influx_cnt[21] = "0/0";
// buffer for formatted strings
char data_c[191];

// tags variables for influxdb
//playroom, master_br, kitchen, loft, family_r....
char location[20] = "default";
char humidifier[6] = "na";
// struct to store last used tags values in emulated EEPROM (E3)
struct TagsE3 {
  char  location[20];
  char  humidifier[6];
  int   version;
};

//influxdb server requests counters
int meas_sent = 0;
int meas_successful=0;

// InfluxDB http request header
http_header_t influxdb_H[] ={
    { "Accept" , "*/*"},
    { "User-agent", "Particle HttpClient"},
    { NULL, NULL } // NOTE: Always terminate headers will NULL
};

// http request instance
HttpClient http;
http_request_t influxdbRequest;
http_response_t response;
