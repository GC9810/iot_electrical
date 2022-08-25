double Voltage_data, Current_data, kWh_data, Power_data, Pf_data, CO2_data, Temperature_data, Freq_data; //Variable for metering parameters
#define WIFI_SSID "CHANGE_TO_YOUR_WIFI_SSID"                                                             //(Max: 11 character)
#define WIFI_PASSWORD "CHANGE_TO_YOUR_WIFI_PW"
char thingsboardServer[] = "192.168.1.149"; //Server IP
#define TOKEN "METER"

#define refresh_interval 1000         //Refresh interval of measurement
#define refresh_display_interval 1000 //Refresh interval of display refresh

// To send Email using Gmail use port 465 (SSL) and SMTP Server smtp.gmail.com
#define emailSenderAccount "CHANGE_TO_YOUR_SENDER_EMAIL"
#define emailSenderPassword "CHANGE_TO_YOUR_SENDER_PW"
#define emailRecipient "CHANGE_TO_YOUR_RECIPIENT_EMAIL"
#define smtpServer "smtp.gmail.com"
#define smtpServerPort 465 //Port 465 for GMAIL SMTP server
#define emailSubject "Message from ESP32 Smart Meter"

#include <LiquidCrystal_I2C.h>
#include "ESP32_MailClient.h"
#include <ArduinoJson.h> //Wrap data to json format
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>   //Wi-Fi connection
#include <PubSubClient.h> //MQTT connection

unsigned long last_retrieve_Millis = 0; //Variable for sensor timer
unsigned long last_MQTT_upload = 0;
unsigned long last_refresh_display_millis = 0;
long last_email_millis = 0; //timer for email transfer interval, avoid spamming when things go wrong

int menu_status = 0;   //Default menu is basic parameter menu
bool email_flag = 1;   // 1 to enable single email sending, 0 to disable
bool tamper_state = 0; /// Status of tamper condition, default is 0.
bool summary_email_flag = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2); // set LCD address, number of columns and rows
SMTPData smtpData;
WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Callback function to get the Email sending status
void sendCallback(SendStatus info);
void serialFlush();
void refresh_display();
String get_params();
void MQTT_upload();
void refreshswitch_state();
void refreshtamper_state();
void send_email(int type_of_email);
void refresh_email_state();

void setup()
{
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
  Serial2.begin(4800);

  // initialize LCD
  lcd.init(21, 22);
  lcd.begin(16, 2);
  // turn on LCD backlight
  lcd.backlight();

  lcd.setCursor(0, 0);
  Serial.print("Connecting");
  lcd.print("Connecting");

  pinMode(12, INPUT_PULLUP);
  pinMode(13, INPUT_PULLUP); //Declare the switch button to PULLUP mode
  pinMode(14, INPUT_PULLUP); //Enable pull up resistor for tamper switch

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lcd.setCursor(0, 1);

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    lcd.print(".");
    delay(200);
  }

  Serial.println("\nWiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi connected");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(1000);
  client.setServer(thingsboardServer, 1883); //to connect to the thingsboard server with specific IP and port number
  client.connect(thingsboardServer, TOKEN, NULL);
  Serial.println("MQTT connected.");
  Serial.print("IP address: ");
  Serial.println(thingsboardServer);
  lcd.clear();
}

//Retrieve latest reading from IM1281B sensor
void refresh_reading()
{
  if (millis() - last_retrieve_Millis > refresh_interval) //Only retrieve the reading after 1 refresh interval
  {
    serialFlush();
    byte message[] = {0x01, 0x03, 0x00, 0x48, 0x00, 0x08, 0xC4, 0x1A}; //Predefined HEX string to get the reading
    byte RX_Buffer[37];                                                //Receive 37 of HEX value from the module
    Serial2.write(message, sizeof(message));                           //Send to HEX string to the module by UART2 interface
    Serial.print("\nQuery sent @");                                    //Print debug message on PC
    Serial.print(millis());                                            //Display the refresh millis (for debug)
    Serial.print("\nReturn : ");

    // for loop to get 37 bytes from the reply
    for (int i = 0; i < 37; i++)
    {
      while (!Serial2.available())
        ; // wait for a character
      int incomingByte = Serial2.read();
      RX_Buffer[i] = incomingByte; //Assign the incoming Byte from UART to the RX_Buffer array
      Serial.print(incomingByte, HEX);
      Serial.print(" ");
    }
    //Perform bit shift and bit concatenation, multiply the reading to units stated in datasheet. Assign meaningful value to the variables
    Voltage_data = 0.0001 * ((((unsigned long)(RX_Buffer[3])) << 24) | (((unsigned long)(RX_Buffer[4])) << 16) | (((unsigned long)(RX_Buffer[5])) << 8) | RX_Buffer[6]);
    Current_data = 0.0001 * ((((unsigned long)(RX_Buffer[7])) << 24) | (((unsigned long)(RX_Buffer[8])) << 16) | (((unsigned long)(RX_Buffer[9])) << 8) | RX_Buffer[10]);
    Power_data = 0.0000001 * ((((unsigned long)(RX_Buffer[11])) << 24) | (((unsigned long)(RX_Buffer[12])) << 16) | (((unsigned long)(RX_Buffer[13])) << 8) | RX_Buffer[14]);
    kWh_data = 0.0001 * ((((unsigned long)(RX_Buffer[15])) << 24) | (((unsigned long)(RX_Buffer[16])) << 16) | (((unsigned long)(RX_Buffer[17])) << 8) | RX_Buffer[18]);
    Pf_data = 0.001 * ((((unsigned long)(RX_Buffer[19])) << 24) | (((unsigned long)(RX_Buffer[20])) << 16) | (((unsigned long)(RX_Buffer[21])) << 8) | RX_Buffer[22]);
    CO2_data = 0.0001 * ((((unsigned long)(RX_Buffer[23])) << 24) | (((unsigned long)(RX_Buffer[24])) << 16) | (((unsigned long)(RX_Buffer[25])) << 8) | RX_Buffer[26]);
    Temperature_data = 0.01 * ((((unsigned long)(RX_Buffer[27])) << 24) | (((unsigned long)(RX_Buffer[28])) << 16) | (((unsigned long)(RX_Buffer[29])) << 8) | RX_Buffer[30]);
    Freq_data = 0.01 * ((((unsigned long)(RX_Buffer[31])) << 24) | (((unsigned long)(RX_Buffer[32])) << 16) | (((unsigned long)(RX_Buffer[33])) << 8) | RX_Buffer[34]);

    //Print out parameter in serial to PC
    Serial.print("\nVoltage :");
    Serial.print(Voltage_data, 2);
    Serial.print("\tCurrent: ");
    Serial.print(Current_data, 2);
    Serial.print("\tkWh: ");
    Serial.print(kWh_data, 2);
    Serial.print("\tkW: ");
    Serial.print(Power_data, 2);
    Serial.print("\tPF: ");
    Serial.print(Pf_data, 2);
    Serial.print("\tCO2: ");
    Serial.print(CO2_data, 2);
    Serial.print("\tTemperature: ");
    Serial.print(Temperature_data, 1);
    Serial.print("\tFreq: ");
    Serial.print(Freq_data, 2);
    Serial.println();

    last_retrieve_Millis = millis();
    refresh_display();
  }
}

//Refresh  lastest reading to LCD
void refresh_display()
{
  lcd.begin(16, 02);

  switch (menu_status)
  {
  case 0:

    lcd.setCursor(0, 0);
    lcd.print(Voltage_data, 1); // print message
    lcd.setCursor(5, 0);
    lcd.print("V");
    lcd.setCursor(7, 0);
    lcd.print(Current_data, 2);
    lcd.setCursor(12, 0);
    lcd.print("A");
    lcd.setCursor(0, 1);
    lcd.print(kWh_data, 2);
    lcd.setCursor(12, 1);
    lcd.print("kWh");
    break;

  case 1:

    lcd.setCursor(0, 0);
    lcd.print("PF:");      // print message
    lcd.print(Pf_data, 2); //Display PF to 2 decimal places(d.p.)
    lcd.setCursor(9, 0);
    lcd.print(Freq_data, 2); //Display Frequency to 2 decimal places(d.p.)
    lcd.print("Hz");
    lcd.setCursor(0, 1);
    lcd.print(Power_data, 2); //Display kW to 2 decimal places(d.p.)
    lcd.setCursor(5, 1);
    lcd.print("kW");
    lcd.setCursor(9, 1);
    lcd.print(Temperature_data, 1); //Display Temperature to 1 decimal places(d.p.)
    lcd.print((char)223);           //Print degree symbol(°)
    lcd.print("C");
    break;

  case 2:

    lcd.setCursor(0, 0);
    lcd.print("SSID:"); // print message
    lcd.print(WIFI_SSID);
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    break;
  }
}
void refresh_email_state()
{
  if (email_flag == true && millis() - last_email_millis > 5000)
  {
    if (summary_email_flag == true)
    {
      send_email(1); //Send a notification email for summary reading
    }
    else if (tamper_state == true)
    {
      send_email(2); //Send a notification email for tamper activity
    }
  }
}

void send_email(int type_of_email)
{
  // Set the SMTP Server Email host, port, account and password
  smtpData.setLogin(smtpServer, smtpServerPort, emailSenderAccount, emailSenderPassword);

  // Set the sender name and Email
  smtpData.setSender("ESP32", emailSenderAccount);

  // Set Email priority or importance High, Normal, Low or 1 to 5 (1 is highest)
  smtpData.setPriority("High");

  // Set the subject
  smtpData.setSubject(emailSubject);

  //Convert the curreunt time to time string
  MailClient.Time.setClock(8, 0);
  int d = MailClient.Time.getDay();
  int m = MailClient.Time.getMonth();
  int y = MailClient.Time.getYear();
  int hr = MailClient.Time.getHour();
  int sec = MailClient.Time.getSec();
  int min = MailClient.Time.getMin();
  char time_string[32]; //Buffer string to store the date and time
  snprintf(time_string, sizeof(time_string), "%04d-%02d-%02d %02d:%02d:%02d", y, m, d, hr, min, sec); //Format time_string with delimiter and date, time variable

  lcd.setCursor(0, 1);
  lcd.print("Email preparing");
  Serial.println("Preparing to send email");
  Serial.println();

  switch (type_of_email)
  {
  case 1:
  {
    //Format the message for output
    char email_messageStr[1500]; //Buffer string for the payload message
    char Voltage_str[10];
    char Current_str[10];
    char kWh_str[30]; //Expanded size for kWh string
    char PF_str[10];
    char Freq_str[10];
    char kW_str[10];
    char Temp_str[10];

    //Convert double datatype to string datatype of the reading
    dtostrf(Voltage_data, 6, 1, Voltage_str);
    dtostrf(Current_data, 5, 2, Current_str);
    dtostrf(kWh_data, 25, 2, kWh_str);
    dtostrf(Pf_data, 3, 2, PF_str);
    dtostrf(Freq_data, 4, 2, Freq_str);
    dtostrf(Power_data, 4, 2, kW_str);
    dtostrf(Temperature_data, 4, 1, Temp_str);

    snprintf(email_messageStr, 700,
             "<div style=\"color:#2f4468;\">\
          <h1>Latest reading : </h1>\
          <p>Voltage: %s V</p>\
          <p>Current: %s A</p>\
          <p>kWh: %s kWh</p>\
          <p>PF: %s </p>\
          <p>Frequency: %s Hz</p>\
          <p>kW: %s kW</p>\
          <p>Temperature: %s &deg;C</p>\
          <p>SSID: %s</p>\
          <p>IP: %s</p>\
          <p>@%s</p>\
          <p>- Sent from ESP32 board</p>\
          </div>",
             Voltage_str, Current_str, kWh_str, PF_str, Freq_str, kW_str, Temp_str, WIFI_SSID, WiFi.localIP().toString().c_str()),
        time_string;
    // Set the message with HTML format
    smtpData.setMessage(email_messageStr, true);
    break;
  }

  case 2:
  {
    char email_messageStr[300]; //Create string to construct the message
    snprintf(email_messageStr, 700,
             "<div style=\"color:#2f4468;\">\
        <h1>Alert: Case tampered</h1>\
        <p>The case of meter has been opened @ %s HKT</p>\
        <p>- Sent from ESP32 board</p>\
        </div>",
             time_string);
    smtpData.setMessage(email_messageStr, true); //Send the message with HTML format
  }

  default:
    break;
  }

  // Add recipients, you can add more than one recipient
  smtpData.addRecipient(emailRecipient);

  smtpData.setSendCallback(sendCallback);

  //Start sending Email, can be set callback function to track the status
  if (!MailClient.sendMail(smtpData))
  {
    Serial.println("Error sending Email, " + MailClient.smtpErrorReason());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("EMAIL ERROR!");
    delay(500);
  }
  //Clear all data from Email object to free memory
  smtpData.empty();
  email_flag = false; //toggle the email status back to inactive
}

void loop() // run over and over
{
  refresh_reading();     //Retrieve reading from IM2581B Module and display to LCD
  refresh_email_state(); //Refresh email activity
  MQTT_upload();
  refreshswitch_state();
  refreshtamper_state();
}

// Callback function to get the Email sending status
void sendCallback(SendStatus msg)
{
  // Print the current status
  Serial.println(msg.info());

  // Print message on LCD
  if (msg.success())
  {
    Serial.println("Email Sent!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Email Sent!");
    delay(500);
  }
}

void serialFlush()
{
  while (Serial2.available() > 0)
  {
    char t = Serial2.read();
  }
}

void MQTT_upload()
{
  if (!client.connected())
  {
    if (client.connect(thingsboardServer, TOKEN, NULL))
    {
      Serial.println("[MQTT RETRY DONE]");
    }
    else
    {
      Serial.print("[MQTT RETRY FAILED]");
    }
  }
  if (millis() - last_MQTT_upload > 1000)
  { // Update and send only after 1 seconds
    String data_fetch = get_params();
    if (client.publish("v1/devices/me/telemetry", data_fetch.c_str()))
      Serial.println("MQTT upload success");
    else
      Serial.println("MQTT failed");
    last_MQTT_upload = millis();
  }
}

String get_params()
{
  // Prepare JSON payload string
  StaticJsonBuffer<400> jsonBuffer;
  JsonObject &data = jsonBuffer.createObject();

  // To wrap the information into json format
  data["voltage"] = Voltage_data;
  data["current"] = Current_data;
  data["kWh"] = kWh_data;
  data["power"] = Power_data;
  data["pf"] = Pf_data;
  data["temperature"] = Temperature_data;
  data["co2"] = CO2_data;
  data["frequency"] = Freq_data;

  // To convert the json data back to a String
  char payload[400];
  data.printTo(payload, sizeof(payload));
  String strPayload = String(payload);
  Serial.print("JSON payload: ");
  Serial.println(strPayload);
  return strPayload;
}

void refreshswitch_state()
{
  if (digitalRead(13) == LOW)
    menu_status = 1;
  else if (digitalRead(12) == LOW)
    menu_status = 2;
  else
    menu_status = 0;
}

void refreshtamper_state()
{
  if (digitalRead(14) == HIGH)
  {
    tamper_state = true;
  }
}