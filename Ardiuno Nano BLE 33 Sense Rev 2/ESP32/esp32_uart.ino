#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

#define RXD2 16
#define TXD2 17

// Supabase upload interval (1 minute = 60000 ms)
#define SUPABASE_INTERVAL 60000
unsigned long lastSupabaseUpload = 0;
String latestData = "";
bool firstUploadDone = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Connected!");
}

void sendToSupabase(String data) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(SUPABASE_URL) + "/rest/v1/sensor_data";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    http.addHeader("Prefer", "return=representation");  // Changed to see actual response

    // Parse 7 values from CSV
    String values[7];
    int idx = 0, lastIdx = 0, commaIdx = 0;
    while (commaIdx != -1 && idx < 7) {
      commaIdx = data.indexOf(',', lastIdx);
      if (commaIdx != -1)
        values[idx] = data.substring(lastIdx, commaIdx);
      else
        values[idx] = data.substring(lastIdx);
      lastIdx = commaIdx + 1;
      idx++;
    }

    // Debug: Print all values
    for(int i = 0; i < 7; i++) {
      Serial.print("Value["); Serial.print(i); Serial.print("]: '");
      Serial.print(values[i]); Serial.print("' (len=");
      Serial.print(values[i].length()); Serial.println(")");
    }

    // Validate we got 7 values
    if (idx != 7) {
      Serial.print("ERROR: Only got "); Serial.print(idx); 
      Serial.println(" values, expected 7!");
      http.end();
      return;
    }

    // Convert date from dd/mm/yyyy or dd-mm-yyyy to yyyy/mm/dd
    String originalDate = values[0];
    String convertedDate = "";
    
    // Find separators (could be / or -)
    int firstSep = originalDate.indexOf('/');
    if (firstSep == -1) firstSep = originalDate.indexOf('-');
    
    int secondSep = originalDate.indexOf('/', firstSep + 1);
    if (secondSep == -1) secondSep = originalDate.indexOf('-', firstSep + 1);
    
    if (firstSep > 0 && secondSep > firstSep) {
      String day = originalDate.substring(0, firstSep);
      String month = originalDate.substring(firstSep + 1, secondSep);
      String year = originalDate.substring(secondSep + 1);
      convertedDate = year + "-" + month + "-" + day;
      Serial.println("Date converted: " + originalDate + " -> " + convertedDate);
    } else {
      convertedDate = originalDate; // Use as-is if parsing fails
      Serial.println("Date conversion failed, using original: " + originalDate);
    }

    // Build JSON
    StaticJsonDocument<300> doc;
    doc["UID"] = "cd1ab19b-80fe-4807-a445-8f3b6e24bfdc";
    doc["date"] = convertedDate;
    doc["time"] = values[1];
    doc["temperature"] = values[2].toFloat();
    doc["humidity"] = values[3].toFloat();
    doc["pressure"] = values[4].toFloat();
    doc["light"] = values[5].toInt();
    
    // Validate and normalize mold_risk_category
    String category = values[6];
    category.trim();
    category.toLowerCase();
    
    // Replace "moderate" with "medium" if needed
    if (category == "moderate") {
      category = "medium";
    }
    
    // Validate it's one of the allowed values
    if (category != "low" && category != "medium" && category != "high") {
      Serial.print("ERROR: Invalid mold_risk_category '");
      Serial.print(values[6]);
      Serial.println("'. Must be: low, moderate/medium, or high");
      http.end();
      return;
    }
    
    doc["mold_risk_category"] = category;

    String payload;
    serializeJson(doc, payload);
    
    // Debug: Show JSON payload
    Serial.println("=== JSON PAYLOAD ===");
    Serial.println(payload);
    Serial.println("====================");

    int code = http.POST(payload);
    Serial.print("HTTP Response Code: "); Serial.println(code);
    
    // Always show response body for debugging
    String response = http.getString();
    Serial.println("=== RESPONSE BODY ===");
    Serial.println(response.length() > 0 ? response : "(empty)");
    Serial.println("=====================");
    
    if (code >= 200 && code < 300) {
      Serial.println("SUCCESS: Data sent to Supabase!");
    } else {
      Serial.println("ERROR: Request failed!");
    }
    
    http.end();
  } else {
    Serial.println("ERROR: WiFi not connected!");
  }
}

void loop() {
  // Read data from Serial2 whenever available
  if (Serial2.available()) {
    String data = Serial2.readStringUntil('\n');
    data.trim();
    if (data.length() > 0) {
      Serial.println("Received Data: " + data);
      latestData = data;
    }
  }

  // Upload logic: first upload immediately when data arrives, then every minute
  unsigned long currentMillis = millis();
  
  if (latestData.length() > 0) {
    // First upload: send immediately when we have data
    if (!firstUploadDone) {
      Serial.println("=== FIRST UPLOAD ===");
      sendToSupabase(latestData);
      lastSupabaseUpload = currentMillis;
      firstUploadDone = true;
    }
    // Subsequent uploads: every 1 minute
    else if (currentMillis - lastSupabaseUpload >= SUPABASE_INTERVAL) {
      Serial.println("=== 1 MINUTE INTERVAL UPLOAD ===");
      sendToSupabase(latestData);
      lastSupabaseUpload = currentMillis;
    }
  }

  delay(100);
}