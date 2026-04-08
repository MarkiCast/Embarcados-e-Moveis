#include <DHT11.h>
//#define DHT11PIN 4

DHT11 dht11(4);

void  setup()
{
  Serial.begin(9600);
 
}

void loop()
{
  //Serial.println();

  //int chk = DHT11.read(DHT11PIN);

  //Serial.print("Humidity (%): ");
  //Serial.println((float)DHT11.humidity, 2);

  //Serial.print("Temperature  (C): ");
  //Serial.println((float)DHT11.temperature, 2);


// Attempt to read the temperature value from the DHT11 sensor.
    int temperature = dht11.readTemperature();

    // Check the result of the reading.
    // If there's no error, print the temperature value.
    // If there's an error, print the appropriate error message.
    if (temperature != DHT11::ERROR_CHECKSUM && temperature != DHT11::ERROR_TIMEOUT) {
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" °C");
    } else {
        // Print error message based on the error code.
        Serial.println(DHT11::getErrorString(temperature));
    }
  delay(2000);

}