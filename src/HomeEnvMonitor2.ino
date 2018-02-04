/*
* Home environement monitoring and logging: for jonoryParticle Photon
* Env-monitor-P v2
Jaafar Benabdallah / January '18
*/

// imports, definitions and global variables go here:
#include "HomeEnvMonitor2.h"


/* **********************************************************
*              Main functions
*  ********************************************************** */
Timer screen_off_timer(60000, turn_off_screen, true); // a one time firing timer

void setup()
{
  // cloud variables (max 20)
  Particle.variable("illuminance", illuminance);
  Particle.variable("temperature", temperatureC);
  Particle.variable("humidity", humidity);
  Particle.variable("atm_pressure", atm_pressure);

  Particle.variable("tsl_settings", tsl_settings);
  Particle.variable("sen_status", sensors_status);

  // Particle.variable("voltage",voltage);
  Particle.variable("percentage",battery_p);

  Particle.variable("influx_cnt",influx_cnt);
  Particle.variable("cloud_status",cloud_settings);

  // function on the cloud: change light sensor exposure settings (max 12)
  Particle.function("resetSensors", resetSensors);
  Particle.function("setCloud", setActiveCloud);
  Particle.function("setMeta", setConditions);
  Particle.function("setExposure", setExposure2591);
  Particle.function("getMeta", getConditions);
  Particle.function("setEcoMode", setEnergySaving);

  // initiate cloud connection (it's semi-auto mode)
  Particle.connect();

  // register an event handler for SETUP button click
  System.on(button_click, buttonHandler);

  #ifdef serial_debug
  delay(2000);
  Log.info("Starting up...");
  #endif

  // init. display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.display(); // show splashscreen
  delay(2000);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display_on = true; // display is on by default
  // initialize sensors and get status
  tsl_on = enableTSL2591(tsl_settings);
  batt_on = enableBatt();
  thp_on = enableBME_I2C();
  // thp_on = si7021.begin();

  sprintf(sensors_status,"Ill: %i, HT: %i, Batt: %i", tsl_on, thp_on, batt_on);
  Particle.publish("HomeEnv/status", sensors_status);

  sprintf(cloud_settings,"particle: %i, influx: %i", particle_on, influxdb_on);
  //sprintf(cloud_settings,"free mem= %i",System.freeMemory());

  // influxdb connection params
  influxdbRequest.hostname = "influxdb_server_url";
  influxdbRequest.port = 8086;
  influxdbRequest.path = "/write?db=dbName&precision=s&rp=dbRetPol";

  // uncomment next line only when need to reset EEE
  //EEPROM.clear();

  // read last used tags value from emulated EEPROM
  TagsE3 stored_tags;

  EEPROM.get(0, stored_tags);

  strcpy(location, stored_tags.location);
  strcpy(humidifier, stored_tags.humidifier);

  #ifdef serial_debug
  Serial.begin(115200);
  Serial.print("location from E2:");
  Serial.println(stored_tags.location);
  Serial.print("humidifier from E2:");
  Serial.println(stored_tags.humidifier);
  Serial.print("version from E2:");
  Serial.println(stored_tags.version);
  // get local IP
  Serial.println("local IP:");
  Serial.println(WiFi.localIP());
  Serial.println("resolving influxdb_server_url :");
  Serial.println(WiFi.resolve("influxdb_server_url"));
  // get mac address
  byte mac[6];
  WiFi.macAddress(mac);
  for (int i=0; i<6; i++) {
    if (i) Serial.print(":");
    Serial.print(mac[i], HEX);
  }
  Serial.println();
  Serial.println("Initialization completed");
  Serial.println(sensors_status);
  #endif

  // Initial state
  state = AWAKE_STATE;
  // start timers
  screen_off_timer.start();
  sleepingDuration = minSleepingDuration;
  lastWakeUpTime = millis();
  }//setup


void loop() {
  switch(state) {
    case AWAKE_STATE:
    // AWAKE state actionsr
    readSensors();

    // set logging flags by default if service is disabled (so it can move on)
    if (!particle_on) particle_logged = true;
    if (!influxdb_on) influxdb_logged = true;

    // log only when at least one env sensor is enabled
    if ((tsl_on || thp_on)) {
      if (particle_on && !particle_logged && Particle.connected()) {
        Particle.publish("HomeEnv/data",data_c);
        delay(200);
        particle_logged = true;
      }
      if (influxdb_on && !influxdb_logged && WiFi.ready()) {
        sendInfluxdb();
        influxdb_logged = true;
      }
    }

    // Update Display
    if (display_on) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(60,0);
      if (battery_p >= 99.9) {
        sprintf(data_c, "100%%");
      } else {
        sprintf(data_c, "%.1f%%", battery_p);
      }
      display.print(data_c);
      display.setCursor(0,16);
      sprintf(data_c, "%.1fF/%.1f", temperatureF, temperatureC);
      display.print(data_c);
      sprintf(data_c, "%.1f %%RH", humidity);
      display.println(data_c);
      sprintf(data_c, "%.1f lux", illuminance);
      display.print(data_c);
      display.display();
    } else  {
      display.clearDisplay();
      display.display();
    }

    // AWAKE state transition
    // awake --> inactive
    if (((millis() - lastWakeUpTime) > maxAwakeDuration) || (influxdb_logged && particle_logged)) {
      state = INACTIVE_STATE;
      // reset logging flags
      particle_logged = false;
      influxdb_logged = false;
      // set sleeping duration period (ms)
      sleepingDuration = minSleepingDuration + maxAwakeDuration - (millis() - lastWakeUpTime);
      // start sleep state timer (s)
      sleepStartTime_s = Time.now();

      #ifdef serial_debug
      Log.info("time spent awake: %i ms", millis() - lastWakeUpTime);
      Log.info("sleep duration: %i ms", sleepingDuration);
      Log.info("inactive/sleep start time: %i ", sleepStartTime_s);
      Log.info("eco mode status: %i",save_mode_on);
      #endif
    }
    break;

    case INACTIVE_STATE:
    // inactive --> awake
    if (1000 * (Time.now() - sleepStartTime_s) >  sleepingDuration ) {
      #ifdef serial_debug
      Log.info("switch: inactive -> awake after: %i s", Time.now() - sleepStartTime_s);
      #endif
      lastWakeUpTime = millis();
      state = AWAKE_STATE;
    }

    // inactive --> sleep
    if (save_mode_on) {
      // power save mode recently requested by setup button click or through Cloud function
      // adjust remaining sleeping duration
      sleepingDuration = sleepingDuration - 1000 * (Time.now() - sleepStartTime_s);

      #ifdef serial_debug
      Log.info("switch: inactive -> sleep after: %i s", Time.now() - sleepStartTime_s);
      Log.info("adjusted sleep duration: %i ms", sleepingDuration);
      #endif

      // adjust starting time for SLEEP state
      sleepStartTime_s = Time.now();
      state = SLEEP_STATE;
    }
    break;

    case SLEEP_STATE:
    // SLEEP state actions

    // turn off display when going into power save mode (waking up will turn it back on)
    display_on = false;
    display.clearDisplay();
    display.display();

    #ifdef serial_debug
    Log.info("Going into STOP mode at: %i s for %i s", Time.now(), sleepingDuration / 1000);
    delay(10); // allow serial buffer to be emptied
    #endif

    // simulate sleep when uncommented
    /*
    Particle.disconnect();
    WiFi.off();
    delay(sleepingDuration); // synchronous, blocking PIR event detection
    // WiFi.on();
    // Particle.connect();
    */

    // Turn WiFi off so it doesn't reconnect when exiting STOP mode
    WiFi.off();
    //Go into STOP mode
    System.sleep(soundDigitalPin, RISING, sleepingDuration / 1000);

    // SLEEP state transitions
    // sleep --> awake
    #ifdef serial_debug
    // SerialLogHandler logHandler(115200, LOG_LEVEL_INFO);
    Serial.begin(115200);
    Log.info("exited STOP mode after %i s", Time.now() - sleepStartTime_s);
    #endif

    if ( Time.now() - sleepStartTime_s >=  sleepingDuration / 1000) {
      state = AWAKE_STATE;
      // Now is a good time to reconnect
      WiFi.on();
      Particle.connect();
      lastWakeUpTime = millis();
    }
    break;
  }
  // notification for power save mode switch
  if (save_mode_switched) {
    save_mode_switched = false;
    blink_on_switch();
  }
} //loop


/**************************************************************************
Sensors Initialization and configuration functions
/**************************************************************************/
void configureTSL2591(char *settings)
{
  // You can change the gain on the fly, to adapt to brighter/dimmer light situations
  //tsl.setGain(TSL2591_GAIN_LOW);    // 1x gain (bright light)
  tsl.setGain(TSL2591_GAIN_MED);      // 25x gain
  // tsl.setGain(TSL2591_GAIN_HIGH);   // 428x gain
  //tsl.setGain(TSL2591_GAIN_MAX);   // 9876x gain

  tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);  // shortest integration time (bright light)
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_200MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_300MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_400MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_500MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_600MS);  // longest integration time (dim light)

  // format settings
  tsl2591Gain_t gain = tsl.getGain();
  uint16_t gain_int;
  switch(gain)
  {
    case TSL2591_GAIN_LOW:
    gain_int = 1;
    break;
    case TSL2591_GAIN_MED:
    gain_int = 25;
    break;
    case TSL2591_GAIN_HIGH:
    gain_int = 428;
    break;
    case TSL2591_GAIN_MAX:
    gain_int = 9876;
    break;
  }

  sprintf(settings,"Gain: x%i, IT= %ims",gain_int,(tsl.getTiming()+1)*100);
}

bool enableBatt() {
  lipo.begin();
  lipo.quickStart();
  lipo.setThreshold(10);

  if (lipo.getVoltage()){
    return true;
  }
  else {
    strcpy(sensors_status,"batt start err:%i");
    return false;
  }
}

bool enableBME_I2C() {
  //commInterface can be I2C_MODE or SPI_MODE
  //specify chipSelectPin using arduino pin names
  //specify I2C address.  Can be 0x77(default) or 0x76
  bme.settings.commInterface = I2C_MODE;
  bme.settings.I2CAddress = 0x76;
  // following 2 lines for SPI mode
  // bme.settings.commInterface = SPI_MODE;
  // bme.settings.chipSelectPin = A2;

  bme.settings.runMode = 3; //  3, Normal mode
  bme.settings.tStandby = 0; //  0, 0.5ms
  bme.settings.filter = 1; //  0, FIR filter, 2 coeffs
  //tempOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme.settings.tempOverSample = 4;
  //pressOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme.settings.pressOverSample = 4;
  //humidOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  bme.settings.humidOverSample = 4;

  delay(10);  //Make sure sensor had enough time to turn on. BME280 requires 2ms to start up.
  //Calling .begin() causes the settings to be loaded
  // rcode = bme.readRegister(0xD0);
  if (bme.begin() == 0x60) {
    return  true;
  }
  else {
    return false;
  }
}

bool enableTSL2591(char *tslsettings) {
  if (tsl.begin()) {
    configureTSL2591(tslsettings);
    return true;
  }
  else {
    strcpy(tsl_settings,"tsl enable error");
    return false;
  }
}

/* **********************************************************
*              Sensors Reading Function
*  ********************************************************** */

void readSensors() {
  // ****************** light sensor ******************
  // get illuminance value if sensor is on
  if (tsl_on) {
        uint32_t lum = tsl.getFullLuminosity();
        uint16_t ir, full;
        ir = lum >> 16;
        full = lum & 0xFFFF;
        double _lux = tsl.calculateLux(full, ir);
        illuminance = (_lux > 0) ? _lux : 0.01;

        // get an update on the sensor status
        uint8_t status = tsl.getStatus();
        if (status != 48) {
          tsl_on =  false;
        }
        // sprintf(tsl_settings, "status: %i",status);
      }

  // ****************** Temperature + humidity + pressure sensor ******************
  // get T/H/P values if sensor is on
  if (thp_on){
    //check if there  was I2C comm errors
    uint8_t status = bme.readRegister(0xD0);
    if (status != 0x60) {
      // turn off sensor logical switch
      thp_on = false;

      humidity =  0.01;
      temperatureC = 0.01;
      temperatureF = 0.01;
      atm_pressure = 0.01;
      // display error code
      strcpy(sensors_status,"THP error");
    }
    else {
      temperatureC = bme.readTempC();
      temperatureF = bme.readTempF();
      humidity = bme.readFloatHumidity();
      atm_pressure = bme.readFloatPressure();
    }
  }

  // ****************** battery voltage and state of charge sensor ******************
  // get battery related data. There's no fail recovery option for this sensor yet
  if (batt_on) {
    // get battery data
    voltage = lipo.getVoltage();
    battery_p = lipo.getSOC();
    if (battery_p > 100.0) {
      battery_p = 100.0;
    }
    // low_voltage_alert = lipo.getAlert();
    // check sensor status and update logical switch if it's down
    if (lipo.getVersion() != 3) {
      batt_on = false;
    }
  }

  // update sensors status
  sprintf(sensors_status,"Ill: %i, THP: %i, Batt: %i",tsl_on, thp_on, batt_on);
  // update measurements string
  const char *pattern = "T=%.1f C, RH=%.1f, P=%.1f hPa, Ill=%.1f lux, soc=%.1f, v=%.2f V";
  sprintf(data_c, pattern, temperatureC, humidity, atm_pressure, illuminance, battery_p, voltage);

  #ifdef serial_debug
  Log.info(sensors_status);
  Log.info(data_c);
  #endif
}

/* **********************************************************
*              Cloud Configuration Functions
*  ********************************************************** */

int resetSensors(String command){

  const char *command_c = command.c_str();
  uint8_t exit_code = -1;

  if (strcmp(command_c, "th")==0 || strcmp(command_c, "ht")==0 || strcmp(command_c, "all")==0 ) {
    bme.reset();
    thp_on = enableBME_I2C();
    /* si7021.reset();
    ht_on = si7021.begin(); */
    exit_code = 0;
  }
  if (strcmp(command_c, "i")==0 || strcmp(command_c, "all")==0 ) {
    tsl_on = enableTSL2591(tsl_settings);
    exit_code = 0;
  }

  if (strcmp(command_c, "b")==0 || strcmp(command_c, "all")==0 ) {
    lipo.reset();
    batt_on = enableBatt();
    exit_code = 0;
  }

  // update status
  sprintf(sensors_status,"Status: Ill: %i, HT: %i, Batt: %i",tsl_on,thp_on,batt_on);

  return exit_code;
}

// cloud function to change exposure settings (gain and integration time)
int setExposure2591(String command){
  // expected input: [1,2,3 or 4],[1,2,3,4,5 or 6]
  char _gainInput;
  uint8_t _itSwitchInput;
  uint16_t _gain_int;
  // extract gain as char and integrationTime swithc as byte
  _gainInput = command.charAt(0); //we expect 1 -> 4
  _itSwitchInput = command.charAt(2); //we expect 1 -> 6

  switch(_gainInput)
  {
    case '1':
    tsl.setGain(TSL2591_GAIN_LOW);  // 1x gain (bright light)
    _gain_int = 1;
    break;
    case '2':
    tsl.setGain(TSL2591_GAIN_MED);  // 25x
    _gain_int = 25;
    break;
    case '3':
    tsl.setGain(TSL2591_GAIN_HIGH); // 428x
    _gain_int = 428;
    break;
    case '4':
    tsl.setGain(TSL2591_GAIN_MAX);  // 9876x
    _gain_int = 9876;
    break;
    default:
    return -1;
  }

  switch(_itSwitchInput)
  {
    case '1':
    tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);
    break;
    case '2':
    tsl.setTiming(TSL2591_INTEGRATIONTIME_200MS);
    break;
    case '3':
    tsl.setTiming(TSL2591_INTEGRATIONTIME_300MS);
    break;
    case '4':
    tsl.setTiming(TSL2591_INTEGRATIONTIME_400MS);
    break;
    case '5':
    tsl.setTiming(TSL2591_INTEGRATIONTIME_500MS);
    break;
    case '6':
    tsl.setTiming(TSL2591_INTEGRATIONTIME_600MS);
    break;
    default:
    return -1;
  }
  // if reached here, all went well
  sprintf(tsl_settings,"Gain: x%i, IT= %ims",_gain_int,(tsl.getTiming()+1)*100);
  return 0;
}

//cloud function to control where data is sent
int setActiveCloud(String command) {
  // valid values: "particle", "influx", "all" or any other value for no cloud service
  const char *command_c = command.c_str();

  #ifdef serial_debug
  Serial.println(command_c);
  #endif

  if (strcmp(command_c,"particle") == 0) { particle_on = true;}
  else if (strcmp(command_c,"influx") == 0) { influxdb_on = true;}
  else if (strcmp(command_c,"all") == 0) {
    particle_on = true;
    influxdb_on = true;
  }
  else if (strcmp(command_c,"none") == 0) {
    particle_on = false;
    influxdb_on = false;
  }
  else {return -1; }// invalid command

  // there was sucessful change, update cloud services status
  sprintf(cloud_settings,"particle: %i, influx: %i", particle_on, influxdb_on);

  #ifdef serial_debug
  Serial.println(cloud_settings);
  #endif

  return 0;
}

// set tags for influxdb
int setConditions(String command)
{
  // check how many fields provided
  if (command.indexOf(",") == -1) {
    // no comma --> one value provided for location
    strcpy(location,command.c_str());
  }
  else {
    // split the command at the ,
    int split_index = command.indexOf(",");
    strcpy(location, command.substring(0, split_index));
    strcpy(humidifier, command.substring(split_index+1));
  }

  // if tags were modified, store the new values in emulated EEPROM
  TagsE3 stored_tags;
  EEPROM.get(0,stored_tags);

  #ifdef serial_debug
  Serial.print("\nlocation:");
  Serial.println(location);
  Serial.print("\nhumidifier:");
  Serial.println(humidifier);

  Serial.print("location from E2:");
  Serial.println(stored_tags.location);
  Serial.print("humidifier from E2:");
  Serial.println(stored_tags.humidifier);
  Serial.print("version from E2:");
  Serial.println(stored_tags.version);

  #endif

  // compare to what was submitted and store if changed
  if (strcmp(location, stored_tags.location) != 0) {
    // location tag has changed, write to EEPROM
    strcpy(stored_tags.location, location);
    // increment # edits counter
    stored_tags.version++;
    EEPROM.put(0,stored_tags);
  }

  if (strcmp(humidifier, stored_tags.humidifier) != 0) {
    // location tag has changed, write to EEPROM
    strcpy(stored_tags.humidifier, humidifier);
    // increment # edits counter
    stored_tags.version += 1;
    EEPROM.put(0,stored_tags);
  }

  return stored_tags.version;
}

// get current tags for influxdb
int getConditions(String command) {
  // String tags = String::format("location: %s, humidifier: %s, elev: %.1f", location, humidifier);
  String tags = String::format("location: %s, humidifier: %s", location, humidifier);
  Particle.publish("HomeEnv/tags", tags);
  return 0;
}

// set the energy saving mode (one way: once in, can't remotely get out of it)
int setEnergySaving(String command) {
  if (!save_mode_on) {
    save_mode_on = true;
    save_mode_switched = true;
    blink_on_switch();
    return 1;
  }
  else return 0;
}

/* **********************************************************
*              Send data Cloud Functions
*  ********************************************************** */

void sendInfluxdb() {

  // formatted statement needs to look like this:
  // var_name,tag1=tval1,tag2=tval2 value=value

  if (thp_on){
    influxdbRequest.body = String::format("temperature,loc=%s,humidifier=%s value=%.2f",location, humidifier, temperatureC);
    influxdbRequest.body.concat(String::format("\nhumidity,loc=%s,humidifier=%s value=%.1f",location, humidifier, humidity));
  }
  if (thp_on && tsl_on) {
    influxdbRequest.body.concat(String("\n"));
  }
  if(tsl_on) {
    influxdbRequest.body.concat(String::format("illuminance,loc=%s value=%.1f",location, illuminance));
  }
  if(batt_on) {
    influxdbRequest.body.concat(String::format("\nvoltage value=%.2f", voltage));
    influxdbRequest.body.concat(String::format("\nbattery_p value=%.2f", battery_p));
  }

  //reset response content
  response.body = String("");
  response.status=0;

  http.post(influxdbRequest, response, influxdb_H);

  meas_sent++;

  if (response.status==204){
    meas_successful++;
    sprintf(influx_cnt, "%d/%d",meas_successful,meas_sent);
  }

  // send server response it to
  #ifdef serial_debug
  // let's see this hippo
  char header[128];
  Serial.println("request:");
  sprintf(header,"%s",influxdb_H[0]);
  Serial.println(header);
  sprintf(header,"%s",influxdb_H[1]);
  Serial.println(header);
  Serial.println(influxdbRequest.body);
  // let's see server response
  Serial.println("response:");
  Serial.println(response.body);
  Serial.println("status:");
  Serial.println(response.status);
  #endif
}

/* **********************************************************
*              Mode control Functions
*  ********************************************************** */

void buttonHandler(system_event_t event, int data) {
  // pressing the button stops the SLEEP_STATE...
  if (state == SLEEP_STATE) {
    save_mode_switched = true;
    save_mode_on = false;
  }
  // ..and enables AWAKE_STATE and turn on display
  state = AWAKE_STATE;
  display_on = true;
  screen_off_timer.start();
  lastWakeUpTime = millis();
}

// call back to turn off display
void turn_off_screen() {
  display_on = false;
  display.clearDisplay();
  display.display();
}

void blink_on_switch() {
	// visual indication on tracking state transition
	RGB.control(true);
  if (save_mode_on) RGB.color(51, 255,  51); //lime
  else RGB.color(255, 49, 0); //orange
	delay(100);
	RGB.color(0, 0, 0);
	delay(70);
  if (save_mode_on) RGB.color(51, 255,  51); // lime
  else RGB.color(255, 49, 0); //orange
  delay(100);
	RGB.control(false);
}
