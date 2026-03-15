
// Device credentials
String user_id    = "cd1ab19b-80fe-4807-a445-8f3b6e24bfdc";
const char apn[]  = "airtelgprs.com";
String vercel_api = "https://stachyfind-server.vercel.app/api/sensors";

// Arduino Nano sensor UART
#define RXD2 16
#define TXD2 17

// A7670 4G module UART (UART1, RX=12, TX=21)
#define A7670_RX 19
#define A7670_TX 21
HardwareSerial A7670_Serial(1);

bool is4GConnected = false;

// AT Command Helper
String sendATCommand(String command, unsigned long timeout = 1000) {
  while (A7670_Serial.available()) {
    A7670_Serial.read();
  }

  A7670_Serial.println(command);
  unsigned long start = millis();
  String response = "";

  while (millis() - start < timeout) {
    while (A7670_Serial.available()) {
      char c = A7670_Serial.read();
      response += c;
    }
    if (response.indexOf("OK") != -1 || response.indexOf("ERROR") != -1) {
      break;
    }
  }

  Serial.print("CMD: ");
  Serial.print(command);
  Serial.print(" | RSP: ");
  Serial.println(response);
  return response;
}

// Wait for +HTTPACTION response and parse HTTP status code
int waitForHTTPAction(unsigned long timeout = 15000) {
  unsigned long start = millis();
  String response = "";

  while (millis() - start < timeout) {
    while (A7670_Serial.available()) {
      char c = A7670_Serial.read();
      response += c;
      Serial.write(c);
    }
    int idx = response.indexOf("+HTTPACTION:");
    if (idx != -1) {
      // Format: +HTTPACTION: <method>,<status>,<datalen>
      int firstComma  = response.indexOf(',', idx);
      int secondComma = response.indexOf(',', firstComma + 1);
      if (firstComma != -1 && secondComma != -1) {
        return response.substring(firstComma + 1, secondComma).toInt();
      }
    }
  }
  Serial.println("ERROR: Timed out waiting for +HTTPACTION");
  return -1;
}

// Read and print the HTTP response body for debugging
void readHTTPResponse() {
  String resp = sendATCommand("AT+HTTPREAD=0,512", 5000);
  Serial.println("Server response body: " + resp);
}

// Wait for modem to output the DOWNLOAD prompt after AT+HTTPDATA
bool waitForDownloadPrompt(unsigned long timeout = 5000) {
  unsigned long start = millis();
  String buf = "";
  while (millis() - start < timeout) {
    while (A7670_Serial.available()) {
      char c = A7670_Serial.read();
      buf += c;
    }
    if (buf.indexOf("DOWNLOAD") != -1) return true;
  }
  Serial.println("WARNING: DOWNLOAD prompt not received");
  return false;
}

bool init4GConnection() {
  Serial.println("\n=== Initializing 4G ===");
  for (int i = 0; i < 3; i++) {
    String resp = sendATCommand("AT", 1000);
    if (resp.indexOf("OK") != -1) {
      Serial.println("AT communication OK");
      break;
    }
    delay(1000);
  }

  sendATCommand("AT+CPIN?", 2000);
  sendATCommand("AT+CFUN=1", 2000);

  Serial.println("Waiting for network registration...");
  for (int i = 0; i < 30; i++) {
    String resp = sendATCommand("AT+CREG?", 2000);
    if (resp.indexOf("+CREG: 0,1") != -1 || resp.indexOf("+CREG: 0,5") != -1) {
      Serial.println("Network registered.");
      break;
    }
    delay(2000);
  }

  sendATCommand("AT+CSQ", 1000);
  sendATCommand("AT+CGDCONT=1,\"IP\",\"" + String(apn) + "\"", 2000);
  sendATCommand("AT+CGATT=1", 3000);
  sendATCommand("AT+CGACT=1,1", 5000);

  String checkPDP = sendATCommand("AT+CGACT?", 2000);
  if (checkPDP.indexOf("+CGACT: 1,1") != -1) {
    Serial.println("PDP context active.");
  } else {
    Serial.println("WARNING: PDP context not active.");
  }

  Serial.println("4G ready.");
  is4GConnected = true;
  return true;
}

void sendToSupabase(String data) {
  if (!is4GConnected) {
    Serial.println("ERROR: 4G not connected!");
    return;
  }

  // Strip stray carriage-returns / newlines that survive readStringUntil
  data.replace("\r", "");
  data.replace("\n", "");
  data.trim();

  // Debug: print exactly what we received before parsing
  Serial.print("sendToSupabase raw (len=");
  Serial.print(data.length());
  Serial.print("): '");
  Serial.print(data);
  Serial.println("'");

  // Parse 7 comma-separated values using strtok (more reliable than indexOf loop)
  String values[7];
  int idx = 0;
  char buf[200];
  data.toCharArray(buf, sizeof(buf));
  char* token = strtok(buf, ",");
  while (token != NULL && idx < 7) {
    values[idx++] = String(token);
    token = strtok(NULL, ",");
  }

  // Debug: Print all values
  for (int i = 0; i < 7; i++) {
    Serial.print("Value["); Serial.print(i); Serial.print("]: '");
    Serial.print(values[i]); Serial.print("' (len=");
    Serial.print(values[i].length()); Serial.println(")");
  }

  // Validate we got all 7 values
  if (idx < 7) {
    Serial.print("ERROR: Only got "); Serial.print(idx);
    Serial.println(" values, expected 7!");
    return;
  }

  // Convert date from dd/mm/yyyy or dd-mm-yyyy to yyyy-mm-dd
  String originalDate = values[0];
  String convertedDate = "";
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
    convertedDate = originalDate;
    Serial.println("Date conversion failed, using original: " + originalDate);
  }

  float temperature = values[2].toFloat();
  float humidity    = values[3].toFloat();
  float pressure    = values[4].toFloat();
  float lux         = values[5].toFloat();

  // Validate and normalize mold_risk_category
  String category = values[6];
  category.trim();
  category.toLowerCase();
  if (category == "moderate") category = "medium";

  if (hasSensorFailure(temperature, humidity, pressure, lux)) {
    Serial.println("Sensor failure detected on ESP32 side. Forcing mold_risk_category to critical.");
    category = "critical";
  }

  if (category != "low" && category != "medium" && category != "high" && category != "critical") {
    Serial.print("ERROR: Invalid mold_risk_category '");
    Serial.print(values[6]);
    Serial.println("'. Must be: low, moderate/medium, high, or critical");
    return;
  }

  // Build JSON payload
  String payload = "{\"UID\":\"" + user_id + "\",";
  payload += "\"date\":\"" + convertedDate + "\",";
  payload += "\"time\":\"" + values[1] + "\",";
  payload += "\"temperature\":" + String(temperature) + ",";
  payload += "\"humidity\":" + String(humidity) + ",";
  payload += "\"pressure\":" + String(pressure) + ",";
  payload += "\"light\":" + String((int)lux) + ",";
  payload += "\"mold_risk_category\":\"" + category + "\"}";

  Serial.println("\n=== Uploading via Vercel ===");
  Serial.println("Payload: " + payload);

  sendATCommand("AT+HTTPTERM", 2000);
  delay(1000);
  sendATCommand("AT+HTTPINIT", 2000);
  sendATCommand("AT+HTTPSSL=1", 2000);

  String url = vercel_api + "?type=upload_sensor";
  sendATCommand("AT+HTTPPARA=\"URL\",\"" + url + "\"", 2000);
  sendATCommand("AT+HTTPPARA=\"REDIR\",1", 2000);
  sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 2000);

  // Send AT+HTTPDATA directly so the "DOWNLOAD" prompt is NOT consumed by sendATCommand
  while (A7670_Serial.available()) A7670_Serial.read();  // flush stale bytes
  A7670_Serial.println("AT+HTTPDATA=" + String(payload.length()) + ",10000");
  Serial.println("CMD: AT+HTTPDATA=" + String(payload.length()) + ",10000");
  if (!waitForDownloadPrompt()) {
    Serial.println("ERROR: Aborting upload — DOWNLOAD prompt never arrived.");
    sendATCommand("AT+HTTPTERM", 2000);
    return;
  }
  Serial.println("DOWNLOAD prompt received. Sending payload...");
  A7670_Serial.print(payload);
  delay(2000);
  sendATCommand("AT+HTTPACTION=1", 2000);
  int httpStatus = waitForHTTPAction(15000);
  if (httpStatus == 200 || httpStatus == 201) {
    Serial.println("SUCCESS: Data sent to Supabase!");
  } else if (httpStatus == 308 || httpStatus == 301 || httpStatus == 302) {
    Serial.println("ERROR: Server returned redirect (" + String(httpStatus) + "). Check vercel_api URL or update modem firmware.");
    readHTTPResponse();
  } else {
    Serial.print("ERROR: Upload failed. HTTP Status: ");
    Serial.println(httpStatus);
    readHTTPResponse();
  }

  sendATCommand("AT+HTTPTERM", 2000);
}

void setup() {
  Serial.begin(115200);

  // Arduino Nano sensor UART
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // A7670 4G module UART
  A7670_Serial.begin(115200, SERIAL_8N1, A7670_RX, A7670_TX);
  delay(3000);  // Wait for module to boot

  init4GConnection();
}

void loop() {
  if (Serial2.available()) {
    String data = Serial2.readStringUntil('\n');
    data.replace("\r", "");
    data.trim();
    if (data.length() > 20) {
      // Validate CSV has exactly 6 commas (7 fields)
      int commaCount = 0;
      for (int i = 0; i < (int)data.length(); i++) {
        if (data[i] == ',') commaCount++;
      }
      if (commaCount == 6) {
        Serial.println("Received Data: " + data);
        Serial.println("=== UPLOADING ===");
        sendToSupabase(data);
      } else {
        Serial.println("WARNING: Ignoring malformed CSV (commas=" + String(commaCount) + "): " + data);
      }
    }
  }

  delay(100);
}