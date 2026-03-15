#include <RTClib.h>
#include <Arduino_LPS22HB.h>
#include <Arduino_HS300x.h>
#include <Wire.h>
#include <TensorFlowLite.h>
#include <tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include <Adafruit_VEML7700.h>
#include <math.h>
#include "model_data.h"

// RTC module (DS3231 on I2C, 0x68 address)
RTC_DS3231 rtc;

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

bool hasSensorFailure(float temperature, float humidity, float pressure, float lux) {
  if (!isfinite(temperature) || !isfinite(humidity) || !isfinite(pressure) || !isfinite(lux)) {
    return true;
  }

  // Basic physical sanity checks to catch disconnected or malfunctioning sensors.
  if (temperature < -40.0f || temperature > 125.0f) return true;
  if (humidity < 0.0f || humidity > 100.0f) return true;
  if (pressure < 10.0f || pressure > 1200.0f) return true;
  if (lux < 0.0f || lux > 120000.0f) return true;

  return false;
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);   // UART TX to ESP32
  while (!Serial);

  // Initialize I2C
  Wire.begin();
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC!");
    while (1);
  }

  // Snippet: set RTC from system (compile/upload) time after initialization.
  // DateTime systemTime = DateTime(F(__DATE__), F(__TIME__));
  // rtc.adjust(systemTime);

  // Set RTC to the provided current time.
  Serial.println("Setting RTC to provided current time...");
  rtc.adjust(DateTime(2026, 3, 12, 20, 7, 0));

  // Print current time
  DateTime now = rtc.now();
  Serial.print("RTC Time: ");
  Serial.print(now.year(), DEC);
  Serial.print('-');
  Serial.print(now.month(), DEC);
  Serial.print('-');
  Serial.print(now.day(), DEC);
  Serial.print(" ");
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.print(now.second(), DEC);
  Serial.println();

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
  DateTime now = rtc.now();

  float temperature = roundf(HS300x.readTemperature(CELSIUS) * 100) / 100.0;
  float humidity = roundf(HS300x.readHumidity() * 100) / 100.0;
  float pressure = roundf(BARO.readPressure() * 100) / 100.0;
  float lux = roundf(veml.readLux() * 100) / 100.0;

  const char* mold_risk = "Critical";
  const char* risk_labels[] = {"High", "Low", "Moderate"};

  bool sensor_failure = hasSensorFailure(temperature, humidity, pressure, lux);
  if (sensor_failure) {
    Serial.println("Sensor failure detected. Reporting mold risk as Critical.");
  } else {
    input->data.f[0] = temperature;
    input->data.f[1] = humidity;
    input->data.f[2] = lux;
    input->data.f[3] = pressure;

    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
      Serial.println("Invoke failed! Reporting mold risk as Critical.");
      Serial1.println("Invoke failed! Reporting mold risk as Critical.");
    } else {
      int predicted_class = 0;
      float max_score = output->data.f[0];
      for (int i = 1; i < output->dims->data[1]; ++i) {
        if (output->data.f[i] > max_score) {
          max_score = output->data.f[i];
          predicted_class = i;
        }
      }
      mold_risk = risk_labels[predicted_class];
    }
  }

  // Format as CSV: date,time,temperature,humidity,pressure,light,mold_risk
  char csv[128];
  snprintf(csv, sizeof(csv), "%02d/%02d/%04d,%02d:%02d:%02d,%.2f,%.2f,%.2f,%.2f,%s",
    now.day(), now.month(), now.year(),
    now.hour(), now.minute(), now.second(),
    temperature, humidity, pressure, lux,
    mold_risk
  );

  Serial.println(csv);    // Print to Serial Monitor
  Serial1.println(csv);   // Send to ESP32

  delay(1000);
}