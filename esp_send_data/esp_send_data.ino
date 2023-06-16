#define configUSE_TIME_SLICING 1
#define configTICK_RATE_HZ 10

#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb

// LilyGO T-SIM7000G Pinout
#define UART_BAUD   115200
#define PIN_DTR     25
#define PIN_TX      27
#define PIN_RX      26
#define PWR_PIN     4

#define LED_PIN     12

#include <TinyGSM.h>

// Set serial for debug console (to Serial Monitor, default speed 115200)
#define SerialMon Serial
// Set serial for AT commands
#define SerialAT  Serial1

TinyGsm modem(SerialAT);

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "FS.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <INA.h> // INA Library (by Zanshin)
#include <SD.h>
#include <SPI.h>
#include <math.h>

//-----------------------------------Replace with your network credentials----------------------------------------
// Replace with your network credentials
const char* ssid     = "Galaxy S93292";
const char* password = "idrq1358";

// REPLACE with your Domain name and URL path or IP address with path
const char* serverName = "http://telemetria.milhagemufmg.com/post_data.php";

// Keep this API Key value to be compatible with the PHP code provided in the server.
String apiKeyValue = "tPmAT5Ab3j7F9";
//----------------------------------------------------------------------------------------------------------------

// Vamos calibrar o divisor de tensão e o sensor de corrente para o INA 226
// R5 = 20 kΩ
// R6 = 68 kΩ
// Divisor de tensão INA = R6/(R5 + R6) = 0,7727272... kΩ
// Divisor de tensão INA^-1 = (R5 + R6)/R6 = 1,29411764706
// fatorCorreçãoV = 1.01626

// Resistor do shunt = 1 mΩ
// FatorCorreçãoC = 0,482625

//#define R5 20000
//#define R6 68000
//#define valorShunt 0.001
//#define fatorCorrecaoV 1.01626
//#define fatorCorrecaoC 0.482625

#define fatorMili 0.001
#define fatorMicro 0.000001

#define ADC_VREF_mV    3300.0  // in millivolt
#define ADC_RESOLUTION 4096.0
#define AMPOP_OUT        35    // ESP32 pin connected to LM358P
#define GAIN              3.89 // AMPOP_OUT = LM35 output * GAIN

 // ---------- Energy data  ----------
  // Variaveis do sensor INA226
  INA_Class INA; // Construct a power monitor object names "ina"
  int32_t rawCurrent = -666;
  float current_motor;
  uint16_t rawVoltage = 666;
  float voltage_battery;
  int32_t rawShunt = -666;
  float shunt_motor_INA;
  float power = 0;
  float consumption = 0;
  float consumoParcial = 0;

  //Definir pino CS para o modulo do cartao SD
  #define SD_CS 5

  //Variaveis de tempo para verificar tempo de execucao de tasks
  long tempoAtual = 0;
  long tempoAnterior = 0;
  long tempoDelta = 0;
  
  long tempoGPSAtual = 0;
  long tempoGPSAnterior = 0;
  long tempoGPSDelta = 0;
  
  long tempoINAAtual = 0;
  long tempoINAAnterior = 0;
  long tempoINADelta = 0;
  
  long tempoLMAtual = 0;
  long tempoLMAnterior = 0;
  long tempoLMDelta = 0;
  
  long tempoENVIOAtual = 0;
  long tempoENVIOAnterior = 0;
  long tempoENVIODelta = 0;
 
 // ---------- Encoder data ----------
 #define MEASURE_PIN 25
 #define WHEEL_CIRCUMFERANCE 1.596/12
 #define SAMPLES 25

 typedef struct {
   uint16_t ms;
   uint16_t revolutions;
 } sample_t;

 volatile uint8_t nextSample = 0;
 volatile sample_t samples[SAMPLES] = {};

 // temp vars for computing totals
 volatile sample_t* pSample;
 uint8_t n;
 uint8_t totalRevolutions;
 uint8_t totalSamples;
 uint16_t minMs;
 uint16_t maxMs;
 double rps;
 double rpm;
 double wheel_diameter; 

 double tempo_Inicio = 0;
 double tempo_speedVelho = 0;
 double tempo_speedAtual = 0;
 double tempo_dif = 0;
 double tempo_total = 0;
 double speed = 0;
 double speed_mpm = 0;
 double speed_velho = 0;
 double average_speed = 0;
 double distancia_trecho = 0;
 double distancia_total = 0;
 double ms_to_min =0.00001666666;
 bool andou = 0;
 
 // ---------- GPS data ----------
  float lat      = 0;
  float lng      = 0;
  float velocidade   = 0;
  float alt      = 0;
  int   vsat     = 0;
  int   usat     = 0;
  float accuracy = 0;
  int   year     = 0;
  int   month    = 0;
  int   day      = 0;
  int   hour     = 0;
  int   minutos  = 0;
  int   sec      = 0;
  String reading_time = "1-1-1900";

 // ---------- Temperature data ----------
  int adcValAmpOp;
  int adcVal;
  float milliVolt;
  int celcius;
  int farenheits;

SemaphoreHandle_t SemaphoreBuffer;
SemaphoreHandle_t displayMutex;       // Lock access to buffer and Serial
//----------------------------------------------------------------------------------------------------------------
// interrupt handler is triggered when magnet enters or leaves field
void magnet_detection_changed() {
  // check if magnet is nearby
  if (digitalRead(MEASURE_PIN) == HIGH) {
    // increase revolutions
    samples[nextSample].revolutions++;
  }
}

//----------------------------------------------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // Wait a moment to start (so we don't miss Serial output)
  vTaskDelay(1000 / portTICK_PERIOD_MS);
 
  attachInterrupt(MEASURE_PIN, magnet_detection_changed, CHANGE);

  INA.begin(80, 1000, 0x40);             // Begin calibration for an expected 80 Amps maximum current and for a 0.0O1hm resistor
  INA.setAveraging(10);                  // Average each reading n-times
  INA.setBusConversion(10000);           // Maximum conversion time 8.244ms
  INA.setShuntConversion(10000);         // Maximum conversion time 8.244ms
  INA.setMode(INA_MODE_CONTINUOUS_BOTH); // Bus/shunt measured continuously

  SerialMon.println("Place your board outside to catch satelite signal");

  // Set LED OFF
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  //Turn on the modem
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(300);
  digitalWrite(PWR_PIN, LOW);

  delay(1000);

  // Set module baud rate and UART pins
  SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);

  // Restart takes quite some time
  // To skip it, call init() instead of restart()
  SerialMon.println("Initializing modem...");
  if (!modem.restart()) {
    Serial.println("Failed to restart modem, attempting to continue without restarting");
  }//end_if

  // Print modem info
  String modemName = modem.getModemName();
  delay(500);
  SerialMon.println("Modem Name: " + modemName);

  String modemInfo = modem.getModemInfo();
  delay(500);
  SerialMon.println("Modem Info: " + modemInfo);

  Serial.println("All done with GPS! Starting HTTP config...");

  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("WL_NOT_CONNECTED");
  }//end_while
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  // Create the semaphore
  SemaphoreBuffer = xSemaphoreCreateBinary();
  displayMutex = xSemaphoreCreateMutex();

  //Set the semaphore
  xSemaphoreGive(SemaphoreBuffer);
  xSemaphoreGive(displayMutex);

  // Create the tasks
  xTaskCreatePinnedToCore(EnvioDeDadosTask, "Envio De Dados Task", 10000, NULL, 1, NULL, PRO_CPU_NUM);
  xTaskCreatePinnedToCore(INATask, "INA Task", 10000, NULL, 1, NULL, APP_CPU_NUM);
  xTaskCreatePinnedToCore(TemperaturaTask, "Temperatura Task", 10000, NULL, 1, NULL, APP_CPU_NUM);
  xTaskCreatePinnedToCore(
    GPSTask,          // Task function
    "GPS Task",       // Task name
    10000,           // Stack size
    NULL,            // Task parameters
    1,               // Priority
    NULL,            // Task handle
    APP_CPU_NUM               // Core number (0 or 1)
  );

  // Notify that all tasks have been created (lock Serial with mutex)
  xSemaphoreTake(displayMutex, portMAX_DELAY);
  Serial.println("All tasks created");
  xSemaphoreGive(displayMutex);

  //Inicializando cartao SD
  SD.begin(SD_CS);  
  if(!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR - SD card initialization failed!");
    return;    // init failed
  }

  // If the data.txt file doesn't exist
  // Create a file on the SD card and write the data labels
  File file = SD.open("/data.txt");
  if(!file) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/data.txt", "Reading ID, Date, Hour, Temperature \r\n");
  }
  else {
    Serial.println("File already exists");  
  }
  file.close();

}//end setup
//----------------------------------------------------------------------------------------------------------------

void TemperaturaTask(void *pvParameters) {
  while (true) {
    tempoLMAnterior = tempoLMAtual;
    Serial.print("Tempo Anterior LM :");
    Serial.println(tempoLMAnterior);
 
   xSemaphoreTake(SemaphoreBuffer, portMAX_DELAY);
    // read the ADC value from the temperature OpAmp
    adcValAmpOp = analogRead(AMPOP_OUT);
    // converts de ADC value read from the OpAmp into the LM35 original value
    adcVal = adcValAmpOp / GAIN;
    // convert the ADC value to voltage in millivolt
    milliVolt = adcVal * (ADC_VREF_mV / ADC_RESOLUTION);
    // convert the voltage to the temperature in °C
    celcius = milliVolt / 10;
    Serial.print("Celsius :");
    Serial.println(celcius);
    
    // convert the °C to °F
    farenheits = celcius * 9 / 5 + 32;
    tempoLMAtual = millis();
    tempoLMDelta = tempoLMAtual - tempoLMAnterior;
    Serial.print("Tempo task LM :");
    Serial.println(tempoLMDelta);
   xSemaphoreGive(SemaphoreBuffer);
  }//while
}//end Tempertura Task
//----------------------------------------------------------------------------------------------------------------

void INATask(void *pvParameters) {
  while (true) {
    // ______Sensor de Tensão INA__________ /
    tempoINAAnterior = tempoINAAtual;
    Serial.print("Tempo Anterior INA :");
    Serial.println(tempoINAAnterior);
 

    float R5 = 20; // Resistência em kΩ
    float R6 = 68; // Resistência em kΩ
    double valorShunt = 0.001;
    double fatorCorrecaoV = 1.01626;
    double fatorCorrecaoC = 0.97000;
    
    xSemaphoreTake(SemaphoreBuffer, portMAX_DELAY);
     rawVoltage = INA.getBusMilliVolts();
     voltage_battery = (float)rawVoltage * ((R5 + R6) / R6) * fatorCorrecaoV * fatorMili; // Esse valor que está sendo multiplicado pelo valor da tensão tem a função de calibrar o sensor.
    
    xSemaphoreTake(displayMutex, portMAX_DELAY);
     Serial.print("Tensão Bateria :");
     Serial.println(voltage_battery);
    xSemaphoreGive(displayMutex);

    // voltage_battery = (float)rawVoltage * 0.00131516;
    //  Vbat = Vina *      1.29411764706       *    1.01626    *   0.001
    //  Vbat = Vina * fatorDivisorTensao * fatorCorreçãoV * fatorMili

    // _______Sensor de Corrente INA___________ /
    
     rawCurrent = INA.getShuntMicroVolts();
     current_motor = (float)rawCurrent * (fatorMicro / valorShunt) * fatorCorrecaoC;
    
    xSemaphoreTake(displayMutex, portMAX_DELAY);
     Serial.print("Corrente Bateria :");
     Serial.println(current_motor);
    xSemaphoreGive(displayMutex);

    // _______Medição de potência e consumo INA___________ /

     power = (float)INA.getBusMicroWatts() * fatorMicro;
     power *= fatorCorrecaoV;
    
    xSemaphoreTake(displayMutex, portMAX_DELAY);
     Serial.print("Potencia Instantanea :");
     Serial.println(power);
    xSemaphoreGive(displayMutex);
    

     tempoAnterior = tempoAtual;
     tempoAtual = millis();
     tempoDelta = tempoAtual - tempoAnterior;
     consumoParcial = power * (tempoDelta) * fatorMili;
     consumption = consumption + consumoParcial;
     
    tempoINAAtual = millis();
    tempoINADelta = tempoINAAtual - tempoINAAnterior;
    Serial.print("Tempo task INA :");
    Serial.println(tempoINADelta);
    xSemaphoreGive(SemaphoreBuffer);
    
    xSemaphoreTake(displayMutex, portMAX_DELAY);
     Serial.print("Consumo :");
     Serial.println(consumption);
    xSemaphoreGive(displayMutex);
  }//end_while
  // Imotor = (Iina * 10^-6 / 0.001) * 0,482625
  // Imotor = (Iina * fatorMicroVolt / valorShunt) * fatorCorreçãoC
  // ________________________ /
  // Serial.println(current_motor);
  /*
    if (current_motor >= corrente_limite)
    {
      digitalWrite(sinal_corrente, HIGH); // A variável sinal_corrente representa uma saída que vai para no mega/due e atua como um dispositivo de segurança que permite ou proíbe o acionamento
    }
    else
    {
      digitalWrite(sinal_corrente, LOW);
    }
    }
  */
}//end_INATask
//----------------------------------------------------------------------------------------------------------------


void GPSTask(void *pvParameters) {
  while (true) {
    tempoGPSAnterior = tempoGPSAtual;
    Serial.print("Tempo Anterior GPS :");
    Serial.println(tempoGPSAnterior);
    
    // Set SIM7000G GPIO4 HIGH ,turn on GPS power
    // CMD:AT+SGPIO=0,4,1,1
    // Only in version 20200415 is there a function to control GPS power
    modem.sendAT("+SGPIO=0,4,1,1");
    if (modem.waitResponse(10000L) != 1) {
      xSemaphoreTake(displayMutex, portMAX_DELAY);
       SerialMon.println(" SGPIO=0,4,1,1 false ");
      xSemaphoreGive(displayMutex);
    }//end_if
    modem.enableGPS();

    // Take the semaphore to access the shared resource
    xSemaphoreTake(SemaphoreBuffer, portMAX_DELAY);
    xSemaphoreTake(displayMutex, portMAX_DELAY);
    
   
     for (int8_t i = 15; i; i--) {
      //SerialMon.println("Requesting current GPS/GNSS/GLONASS location");
       if (modem.getGPS(&lat, &lng, &velocidade, &alt, &vsat, &usat, &accuracy,
                        &year, &month, &day, &hour, &minutos, &sec)) {
         String reading_time = String(year) + "-" + String(month) + "-" + String(day) + " " + String(hour) + ":" + String(minutos) + ":" + String(sec);
         SerialMon.println("Latitude: " + String(lat, 8) + "\tLongitude: " + String(lng, 8));
         SerialMon.println("Year: " + String(year) + "\tMonth: " + String(month) + "\tDay: " + String(day));
         SerialMon.println("Hour: " + String(hour) + "\tMinute: " + String(minutos) + "\tSecond: " + String(sec));
         SerialMon.println("Reading_time: " + reading_time);

         break;
       }//end_if
       else {
         // SerialMon.println("Couldn't get GPS/GNSS/GLONASS location, retrying in 1ms.");
         vTaskDelay(1);
       }//end_else
     }//end_for

    tempoGPSAtual = millis();
    tempoGPSDelta = tempoGPSAtual - tempoGPSAnterior;
    Serial.print("Tempo task GPS :");
    Serial.println(tempoGPSDelta);
    // Release the semaphore
    xSemaphoreGive(displayMutex);
    xSemaphoreGive(SemaphoreBuffer);

    // Delay for some time
    // vTaskDelay(100);
  }//end while
}//end GPS task
//----------------------------------------------------------------------------------------------------------------

void EnvioDeDadosTask(void *pvParameters) {
  while (true) {
    //Check WiFi connection status
    tempoENVIOAnterior = tempoENVIOAtual;
    Serial.print("Tempo Anterior ENVIO :");
    Serial.println(tempoENVIOAnterior);
    
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      HTTPClient http;

      // Your Domain name with URL path or IP address with path
      http.begin(client, serverName);

      // Specify content-type header
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");

      // Prepare your HTTP POST request data
      xSemaphoreTake(SemaphoreBuffer, portMAX_DELAY);
      String httpRequestData = "api_key=" + apiKeyValue +"&rpm=" + String(rpm,1) + "&speed=" + String(speed, 2) + "&average_speed=" + String(average_speed, 2) + "&wheel_diameter=" + String(wheel_diameter, 2) +
                              "&lat=" + String(lat, 8) + "&lng=" + String(lng, 8) + 
                              "&celcius=" + String(celcius) + "&farenheits=" + String(farenheits) + 
                              "&voltage_battery=" + String(voltage_battery, 1) + "&current_motor=" + String(current_motor, 1) + "&power=" + String(power, 1) + "&consumption=" + String(consumption, 1) + 
                              "&reading_time=" + reading_time + "";
      tempoENVIOAtual = millis();
      tempoENVIODelta = tempoENVIOAtual - tempoENVIOAnterior;
    
      Serial.print("Tempo task ENVIO :");
      Serial.println(tempoENVIODelta);
      
      xSemaphoreGive(SemaphoreBuffer);

      xSemaphoreTake(displayMutex, portMAX_DELAY);
       Serial.print("httpRequestData: ");
       Serial.println(httpRequestData);
      xSemaphoreGive(displayMutex);

      // Send HTTP POST request
      int httpResponseCode = http.POST(httpRequestData);

      if (httpResponseCode > 0) {
        xSemaphoreTake(displayMutex, portMAX_DELAY);
         Serial.print("HTTP Response code: ");
         Serial.println(httpResponseCode);
        xSemaphoreGive(displayMutex);
      }//end_if
      else {
        xSemaphoreTake(displayMutex, portMAX_DELAY);
         Serial.print("Error code: ");
         Serial.println(httpResponseCode);
        xSemaphoreGive(displayMutex);
      }//end_else
      // Free resources
      http.end();
    }//end_if
    else {
      xSemaphoreTake(displayMutex, portMAX_DELAY);
       Serial.println("WiFi Disconnected. Attempting to connect again");
       WiFi.begin(ssid, password);
       Serial.println("Connecting");
      xSemaphoreGive(displayMutex);
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
       xSemaphoreTake(displayMutex, portMAX_DELAY);
        Serial.println("WL_NOT_CONNECTED");
       xSemaphoreGive(displayMutex);
      }//end_while
      xSemaphoreTake(displayMutex, portMAX_DELAY);
       Serial.println("");
       Serial.print("Connected to WiFi network with IP Address: ");
       Serial.println(WiFi.localIP());;
      xSemaphoreGive(displayMutex);
    }//end_else
    // Delay for some time
    // vTaskDelay(100);
  }//end while
}//end EnvioDeDados
//----------------------------------------------------------------------------------------------------------------

//Escrever os dados do sensor no cartao SD
void logSDCard() {
  dataMessage = String(readingID) + "," + String(dayStamp) + "," + String(timeStamp) + "," + 
                String(temperature) + "\r\n";
  Serial.print("Save data: ");
  Serial.println(dataMessage);
  appendFile(SD, "/data.txt", dataMessage.c_str());
}

//
void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

// Append data to the SD card (DON'T MODIFY THIS FUNCTION)
void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if(!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void loop() {
  // compute total revolutions and revolutions per second
  totalRevolutions = 0;
  totalSamples = 0;
  minMs = 0;
  maxMs = 0;
  rps = 0;

  // iterate thru samples
  for (int i=0;i<SAMPLES;i++) {
    n = (nextSample+i) % SAMPLES;
    pSample = &samples[n];

    if (pSample->ms > 0) {
      // good sample
      totalSamples++;
      totalRevolutions += pSample->revolutions;

      minMs = std::min(minMs, (uint16_t)pSample->ms);
      maxMs = std::max(maxMs, (uint16_t)pSample->ms);
    }
  }

  // compute rps
  if (totalSamples > 0) {
    rps = totalRevolutions * 1.0 / totalSamples;
  }

  // prepare for a new sample
  if (nextSample >= SAMPLES-1) {
    nextSample = 0;
  } else {
    nextSample++;
  }

  // clear old sample data
  samples[nextSample].ms = millis();
  samples[nextSample].revolutions = 0;
  rpm = rps * 60 * 0.333;
  speed_mpm = rps * 60 * 0.333 * WHEEL_CIRCUMFERANCE; // em metros por minuto
  speed = (speed_mpm*(60/1000)); // Velocidade instantanea em km/h
  Serial.print("speed_atual :");
  Serial.println(speed);
  if(speed!=0 && speed_velho==0){
    Serial.println("Começou a andar");
    tempo_Inicio = millis();
    tempo_speedVelho = millis();
    tempo_speedAtual= millis();
    speed_velho = speed_mpm;
    andou = 1;
  }
  else if(andou == 1){
    tempo_speedAtual = millis();
    tempo_total = tempo_speedAtual - tempo_Inicio;
    tempo_dif = tempo_speedAtual - tempo_speedVelho;
    distancia_trecho = speed_mpm * (tempo_dif * ms_to_min); // em metros
    distancia_total = (distancia_trecho + distancia_total)/1000; // em km
    average_speed = distancia_total/ ((tempo_total * ms_to_min)/60); //em km/h
    speed_velho = speed_mpm;
    tempo_speedVelho = tempo_speedAtual;
  }
  Serial.print("velocidade media :");
  Serial.println(average_speed);
  Serial.print("distancia_total :");
  Serial.println(distancia_total);
  Serial.print("tempo_total :");
  Serial.println(tempo_total/1000);
  wheel_diameter = WHEEL_CIRCUMFERANCE;

  // Do nothing but allow yielding to lower-priority tasks
  delay(500);

}
