#include "Particle.h"

SYSTEM_MODE(AUTOMATIC);//AUTOMATIC manages cell connections differently than SEMI_AUTOMATIC

// Global objects
FuelGauge batteryMonitor;
PMIC pmic; //PowerManagement Chip (Handles charging and power supplies)

// Forward declarations
void parseSeapHOx(); // parse response from SeapHOx of "ts" "gdata" or "glast" command from Electron

// This is the name of the Particle event to publish for battery or movement detection events
// It is a private event.
const char *eventName = "CpHOx1";

// Various timing constants
const unsigned long MAX_TIME_TO_PUBLISH_MS = 60000; // Only stay awake for 60 seconds trying to connect to the cloud and publish
const unsigned long TIME_AFTER_PUBLISH_S = 4; // After publish, wait 4 seconds for data to go out (Now written as sec, not ms- Aidan 2-19-19)
const unsigned long TIME_AFTER_BOOT_MS = 5000; // At boot, wait 5 seconds before going to sleep again (after coming online)
const unsigned long SLEEP_TIME_SEC = 10; // Deep sleep time- 1800ms
const unsigned long TIMEOUT_SEAPHOX_MS = 5000; // Max wait time for SeapHOx response

// SeapHOx struct for response variables
String s;
struct SeapHOx {
  int Sample_Number;     //0
  char *Board_Date;       //1
  char *Board_Time;         //2
  
  float Main_Batt_Volt;   //3 yes V
  float V_Therm;          //4
  float V_FET_INT;        //5
  float V_FET_EXT;        //6
  float isolated_power_Volt ;//7
  float Controller_Temp;//8
  float Durafet_Temp;   //9 yes C
  float V_Pressure;     //10
  float pHINT;          //11 yes pH
  float pHEXT;            //12 yes pH
  float Counter_Leak;   //13
  float Substrate_Leak;   //14
  float Optode_Model;   //15
  float Optode_SN;      //16
  float O2uM ;          //17 yes [µM]
  float O2_Saturation ; //18
  float Optode_Temp;    //19
  float Dphase;         //20
  float Bphase;         //21
  float Rphase;         //22
  float Bamp;           //23
  float Bpot;           //24
  float Ramp;           //25
  float Raw_Temp;       //26
  float SBE37_Temp;     //27 yes C
  float SBE37_Cond;     //28 yes cm^^-1
  float SBE37_Salinity; //29 yes s
  float SBE37_Date;     //30
  float SBE37_Time;     //31
};
SeapHOx SeapHOx_Cell;

// For the finite state machine
enum State { PUBLISH_STATE, SLEEP_STATE };
State state = PUBLISH_STATE;

unsigned long timer = 0;
unsigned long lastSerial = 0;
//unsigned long count = 0;

 // Start here when waking up out of SLEEP_MODE_DEEP
void setup() {
  //set charging current to 1024mA (512 + 512 offset) (charge faster!):
  pmic.setChargeCurrent(0,0,1,0,0,0);

  // Turn on USB comms
  Serial.begin(9600);
  Serial.println("Awake. Turn cell on.");

  state = PUBLISH_STATE;

  // SeapHOx serial; wait TIMEOUT_SEAPHOX_MS for a line to arrive
  Serial1.setTimeout(TIMEOUT_SEAPHOX_MS);
}


void loop() {

  //////////////////////////////////////////////////////////////////////////////
  // Enter state machine
  switch(state) {

  //////////////////////////////////////////////////////////////////////////////
  /*** PUBLISH_STATE ***/
  /*** Get here from PUBLISH_STATE. Ensure that we're connected to Particle Cloud.
  If so, poll SeapHOx, parse response, and send that and GPS info to cloud then
  go to SLEEP_STATE

  If not connected, still poll SeapHOx and print out response over USB serial then go to SLEEP_STATE.
  ***/
  case PUBLISH_STATE: {
    Serial.println("Publish_State");
    //After this, it attempts to connect to Cellular 
    Serial.println("Connecting...");
    if (Particle.connected() == false && Time.local()-timer <= MAX_TIME_TO_PUBLISH_MS) { // This rewrites the Particle.connect function with a breakout condition
        Particle.connect();
        Serial.println("Still connecting...");
        delay(1000);
    } // 2-19-19 Davin and Aidan 
 
    Serial.println("Particle Connected1"); //2-19-19 Aidan Lucas
    // Poll SeapHOx:
    // Clean out any residual junk in buffer and restart serial port
    Serial1.end();
    delay(1000);
    Serial1.begin(115200); //Baud rate for SeapHOx 2-12-19 Aidan Lucas
    Serial.println("Restarted Serial port"); //2-19-19 Aidan Lucas
    delay(500);

    // Get data in file after current file pointer
    Serial1.println("glast");

    // Read SeapHOx response
    s = Serial1.readString();			// read response
    String s2 = s.replace("Error.txt f_read error: FR_OK\r\n", "");
    const char* s_args = s2.c_str();
    char* each_var = strtok(strdup(s_args), "\t");

    Serial.println(s2);

    // Parse SeapHOx response
    parseSeapHOx(each_var);

    // Put Electron, SeapHOx, and GPS data into data buffer and print to screen
    char data[120];
    float cellVoltage = batteryMonitor.getVCell();
    float stateOfCharge = batteryMonitor.getSoC();
    snprintf(data, sizeof(data), "%s,%s,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.02f,%.02f",
              SeapHOx_Cell.Board_Date, SeapHOx_Cell.Board_Time,
              SeapHOx_Cell.Main_Batt_Volt, SeapHOx_Cell.V_FET_INT,
              SeapHOx_Cell.V_FET_EXT, SeapHOx_Cell.V_Pressure,
              SeapHOx_Cell.pHINT, SeapHOx_Cell.O2uM,
              SeapHOx_Cell.SBE37_Temp, SeapHOx_Cell.SBE37_Salinity,
              cellVoltage, stateOfCharge
            );
    Serial.println(data);

    // If connected, publish data buffer
    if (Particle.connected() == true) { //Connection to the Particle Cloud 
      
      Serial.println("Particle Connected2"); //2-19-19
      //start time track HERE
      timer=Time.local(); //grabs the local onboard time Note: this is subject to DST
      Particle.publish(eventName, data, 60, PRIVATE);

      // Wait for the publish to go out by spinning here till enough time has elapsed
      //stateTime = millis(); // Maybe move out of the loop, b/c this may be getting reset continously 
      //Millis tracks milliseconds since the program began. Resets every 49 days
      while (Time.local()-timer < TIME_AFTER_PUBLISH_S) { //Is this statement just always zero?, and thus true? -Aidan 2-05-19
        Serial.println("Spinning"); //2-19-19
        Serial.println(Time.local()-timer);
        delay(1000);
      }
      state = SLEEP_STATE; 
    }

    // If not connected after certain amount of time, go to sleep to save battery
    else {
      Serial.println("Failed to connect");
      // Took too long to publish, just go to sleep
      if (Time.local()-timer >= MAX_TIME_TO_PUBLISH_MS) { //Changed so this is evaluated compared to 60s
        state = SLEEP_STATE;
      }
    }
  } 
  break; 

  //////////////////////////////////////////////////////////////////////////////
  /*** SLEEP_STATE ***/
  /*** Get here from PUBLISH_STATE and go to GPS_WAIT_STATE (if code makes it that far)
  or SLEEP_MODE_DEEP after calculating a wakeup time based off of the current time from the cloud.
  ***/
  case SLEEP_STATE: {
    //Particle.disconnect(); //2-19-19 Davin and Aidan: Have to explicitly disconnect, otherwise it will call particle.connect
    Serial.println("going to sleep");
    //count = 0; //Reset counter 2-19-19
    delay(500);

    // Calculate sleep time
  	int nextSampleMin = 5; // sample at 5 past the hour
  	int currentHour = Time.hour();
  	int currentSecond = Time.now()%86400; // in UTC

  	// Calculate seconds since midnight of next sample
  	int nextSampleSec = (currentHour+1)*60*60+nextSampleMin*60; // sample at this time
   	int secondsToSleep = 10;//nextSampleSec - currentSecond; We have set it to 10 sec for testing purposes
  	Serial.printf("Sleep for %d seconds\n", secondsToSleep);
   	System.sleep(SLEEP_MODE_DEEP, secondsToSleep); /* Resolved issue 2-26-19: WKP pin was held to high, jumper cable fixed the problem
*/
    // It'll only make it here if the sleep call doesn't work for some reason
    Serial.println("Sleep Call failed");
    timer = Time.local();
    state = PUBLISH_STATE;
  }
  break;

  } //Closing bracket for switch state -Aidan 02-05-19
}


void parseSeapHOx(char* new_var){
 	// Parse SeapHOx response
 	int count = 0;
 	char* parsed[300];

 	while (new_var != NULL) {
 		parsed[count] = new_var;
 		count++;
 		// Serial.println(new_var);
 		new_var = strtok(NULL, " \t");
 	}

 	if (parsed[0][8] == '#') {
 		SeapHOx_Cell.Board_Date 			= parsed[1];
 		SeapHOx_Cell.Board_Time 			= parsed[2];
 		SeapHOx_Cell.Main_Batt_Volt   = strtof(parsed[3], NULL);
 		SeapHOx_Cell.V_Therm          = strtof(parsed[4], NULL);
 		SeapHOx_Cell.V_FET_INT        = strtof(parsed[5], NULL);
 		SeapHOx_Cell.V_FET_EXT        = strtof(parsed[6], NULL);
 		SeapHOx_Cell.Durafet_Temp     = strtof(parsed[9], NULL);
 		SeapHOx_Cell.V_Pressure       = strtof(parsed[10], NULL);
 		SeapHOx_Cell.pHINT            = strtof(parsed[11], NULL);
 		SeapHOx_Cell.pHEXT            = strtof(parsed[12], NULL);
 		SeapHOx_Cell.O2uM             = strtof(parsed[17], NULL);
 		SeapHOx_Cell.O2_Saturation    = strtof(parsed[18], NULL);
 		SeapHOx_Cell.Optode_Temp      = strtof(parsed[19], NULL);
 		SeapHOx_Cell.SBE37_Temp       = strtof(parsed[20], NULL);
 		SeapHOx_Cell.SBE37_Cond       = strtof(parsed[21], NULL);
 		SeapHOx_Cell.SBE37_Salinity   = strtof(parsed[22], NULL);

    Serial.printf("\nParsed SeapHOX \n Date-time %s-%s\n Main_Batt_Volt %.5f\n V_Therm %.5f\n V_FET_INT %.5f\n V_FET_EXT %.5f\n Durafet_Temp %2.5f\n V_Pressure %.5f\n pHINT %.5f\n pHEXT %.5f\n O2 %.5f\n O2_Saturation  %.5f\n Optode_Temp %.5f\n SBE37_Temp %.5f\n SBE37_Cond %.5f\n SBE37_Salinity %.5f\n",
    							SeapHOx_Cell.Board_Date, SeapHOx_Cell.Board_Time,
    							SeapHOx_Cell.Main_Batt_Volt, SeapHOx_Cell.V_Therm ,
                  SeapHOx_Cell.V_FET_INT, SeapHOx_Cell.V_FET_EXT ,
                  SeapHOx_Cell.Durafet_Temp, SeapHOx_Cell.V_Pressure,
                  SeapHOx_Cell.pHINT, SeapHOx_Cell.pHEXT,
                  SeapHOx_Cell.O2uM, SeapHOx_Cell.O2_Saturation,
                  SeapHOx_Cell.Optode_Temp, SeapHOx_Cell.SBE37_Temp,
                  SeapHOx_Cell.SBE37_Cond, SeapHOx_Cell.SBE37_Salinity
                );
  }
}
