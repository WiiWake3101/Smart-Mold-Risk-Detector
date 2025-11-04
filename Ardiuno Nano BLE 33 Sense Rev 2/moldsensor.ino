#include <uRTCLib.h>
#include <Arduino_LPS22HB.h>
#include <Arduino_HS300x.h>
#include <Wire.h>
#include <TensorFlowLite.h>
#include <tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include <Adafruit_VEML7700.h>
#include "model_data.h"

// RTC module (DS3231 on I2C, 0x68 address)
uRTCLib rtc(0x68);

// Create VEML7700 object
Adafruit_VEML7700 veml = Adafruit_VEML7700();

// TensorFlow Lite globals
constexpr int kTensorArenaSize = 8 * 1024;
uint8_t tensor_arena[kTensorArenaSize];
tflite::MicroErrorReporter micro_error_reporter;
tflite::ErrorReporter* error_reporter = &micro_error_reporter;
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);   // UART TX to ESP32
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

  // TensorFlow Lite setup
  model = tflite::GetModel(tflite_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema version mismatch!");
    while (1);
  }
  static tflite::AllOpsResolver resolver;
  interpreter = new tflite::MicroInterpreter(model, resolver, tensor_arena, kTensorArenaSize);
  interpreter->AllocateTensors();
  input = interpreter->input(0);
  output = interpreter->output(0);
}

void loop() {
  rtc.refresh();  // Refresh RTC time registers

  // Read and round sensor values to 2 decimal points
  float temperature = roundf(HS300x.readTemperature(CELSIUS) * 100) / 100.0;
  float humidity = roundf(HS300x.readHumidity() * 100) / 100.0;
  float pressure = roundf(BARO.readPressure() * 100) / 100.0; // kPa
  float lux = roundf(veml.readLux() * 100) / 100.0;

  // Prepare input for the model
  input->data.f[0] = temperature;
  input->data.f[1] = humidity;
  input->data.f[2] = lux;
  input->data.f[3] = pressure;

  // Run inference
  TfLiteStatus invoke_status = interpreter->Invoke();
  if (invoke_status != kTfLiteOk) {
    Serial.println("Invoke failed!");
    Serial1.println("Invoke failed!");
    return;
  }

  // Get prediction (argmax)
  int predicted_class = 0;
  float max_score = output->data.f[0];
  for (int i = 1; i < output->dims->data[1]; ++i) {
    if (output->data.f[i] > max_score) {
      max_score = output->data.f[i];
      predicted_class = i;
    }
  }

  // Map class index to label: 0=High, 1=Low, 2=Moderate
  const char* risk_labels[] = {"High", "Low", "Moderate"};

  // Print timestamp
  Serial.print("Time: "); Serial1.print("Time: ");
  Serial.print(rtc.hour()); Serial1.print(rtc.hour()); Serial.print(":"); Serial1.print(":");
  Serial.print(rtc.minute()); Serial1.print(rtc.minute()); Serial.print(":"); Serial1.print(":");
  Serial.print(rtc.second()); Serial1.print(rtc.second());
  Serial.print(" | Date: "); Serial1.print(" | Date: ");
  Serial.print(rtc.day()); Serial1.print(rtc.day()); Serial.print("/"); Serial1.print("/");
  Serial.print(rtc.month()); Serial1.print(rtc.month()); Serial.print("/"); Serial1.print("/");
  Serial.println(rtc.year()); Serial1.println(rtc.year());

  // Print sensor data
  Serial.print("Temperature: "); Serial.print(temperature); Serial.println(" °C");
  Serial1.print("Temperature: "); Serial1.print(temperature); Serial1.println(" °C");

  Serial.print("Humidity: "); Serial.print(humidity); Serial.println(" %");
  Serial1.print("Humidity: "); Serial1.print(humidity); Serial1.println(" %");

  Serial.print("Pressure: "); Serial.print(pressure); Serial.println(" kPa");
  Serial1.print("Pressure: "); Serial1.print(pressure); Serial1.println(" kPa");

  Serial.print("Lux: "); Serial.print(lux); Serial.println(" lx");
  Serial1.print("Lux: "); Serial1.print(lux); Serial1.println(" lx");

  // Print prediction
  Serial.print("Predicted Mold Risk Category: ");
  Serial.println(risk_labels[predicted_class]);

  Serial1.print("Predicted Mold Risk Category: ");
  Serial1.println(risk_labels[predicted_class]);

  Serial.println("----------------------");
  Serial1.println("----------------------");

  delay(1000);
}
