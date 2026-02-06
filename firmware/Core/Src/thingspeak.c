#include "thingspeak.h"

void thingspeak_send_data(uint8_t field, float value)
{
    float temperature = 0, humidity = 0, pressure = 0;

    // Save the value into the correct variable
    switch(field)
    {
        case 1: temperature = value; break;
        case 2: pressure = value; break;
        case 3: humidity = value; break;
        default: return;
    }

    // Send all three values to ThingSpeak
    thingspeak_send_data_all(temperature, humidity, pressure);
}

void thingspeak_send_data_all(float temperature, float humidity, float pressure)
{
    char request[512];

    sprintf(request,
        "GET /update?api_key=%s&field1=%.2f&field2=%.2f&field3=%.2f HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n\r\n",
        THINGSPEAK_API_KEY, temperature, humidity, pressure, THINGSPEAK_SERVER);

    if(WiFi_TCP_Connect(THINGSPEAK_SERVER, THINGSPEAK_PORT) == 0)
    {
        WiFi_TCP_Send((uint8_t*)request, strlen(request));
        WiFi_TCP_Close();
    }
}
