#include <uRTCLib.h>
#include <Arduino_LPS22HB.h>
#include <Arduino_HS300x.h>
#include <Wire.h>
#include <Adafruit_VEML7700.h>

// RTC module (DS3231 on I2C, 0x68 address)
uRTCLib rtc(0x68);

// Create VEML7700 object
Adafruit_VEML7700 veml = Adafruit_VEML7700();

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Initialize I2C
  URTCLIB_WIRE.begin();

  // Initialize Humidity/Temp Sensor
  HS300x.begin();

  // Initialize Pressure Sensor
  if (!BARO.begin()) {
    Serial.println("Failed to initialize pressure sensor!");
    while (1);
  }

  // Initialize Lux Sensor (VEML7700)
  if (!veml.begin()) {
    Serial.println("Failed to initialize VEML7700!");
    while (1);
  }
  Serial.println("VEML7700 found!");

  // Optional configuration
  veml.setGain(VEML7700_GAIN_1);
  veml.setIntegrationTime(VEML7700_IT_100MS);
  veml.powerSaveEnable(false);
  veml.setPowerSaveMode(VEML7700_POWERSAVE_MODE1);
}

void loop() {
  // Get current time
  rtc.refresh();  // Refresh RTC time registers

  // Read HS300x values
  float temperatureC = HS300x.readTemperature(CELSIUS);
  float temperatureF = HS300x.readTemperature(FAHRENHEIT);
  float humidity = HS300x.readHumidity();

  // Read LPS22HB values
  float pressure = BARO.readPressure();  // in kPa
  float altitude = 44330 * (1 - pow(pressure / 101.325, 1 / 5.255));  // in meters

  // Read VEML7700 lux
  float lux = veml.readLux();

  // Print timestamp
  Serial.print("Time: ");
  Serial.print(rtc.hour());
  Serial.print(":");
  Serial.print(rtc.minute());
  Serial.print(":");
  Serial.print(rtc.second());
  Serial.print(" | Date: ");
  Serial.print(rtc.day());
  Serial.print("/");
  Serial.print(rtc.month());
  Serial.print("/");
  Serial.println(rtc.year());

  // Print sensor data
  Serial.print("Temperature: ");
  Serial.print(temperatureC);
  Serial.print(" °C / ");
  Serial.print(temperatureF);
  Serial.println(" °F");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("Pressure: ");
  Serial.print(pressure);
  Serial.println(" kPa");

  Serial.print("Altitude: ");
  Serial.print(altitude);
  Serial.println(" m");

  Serial.print("Lux: ");
  Serial.print(lux);
  Serial.println(" lx");

  Serial.println("----------------------");

  delay(1000);
}
