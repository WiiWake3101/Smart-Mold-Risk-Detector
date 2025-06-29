#include <Arduino_LPS22HB.h>
#include <Arduino_HS300x.h>
void setup() {
  // put your setup code here, to run once:
  HS300x.begin();
  Serial.begin(115200);
  while (!Serial);
  if (!BARO.begin()) {
    Serial.println("Failed to initialize pressure sensor!");
    while (1);
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  float pressure = BARO.readPressure();
  float altitude = 44330 * ( 1 - pow(pressure/101.325, 1/5.255) );
  Serial.println(HS300x.readTemperature(CELSIUS)); // Celsius reading
  Serial.println(HS300x.readTemperature(FAHRENHEIT)); // Fahrenheit reading
  Serial.println(HS300x.readHumidity());
  Serial.println("Altitude according to kPa is = ");
  Serial.print(altitude);
  Serial.print(" m");
  Serial.println();
  delay(1000);
}
