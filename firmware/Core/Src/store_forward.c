/**
  * @file    store_forward.c
  * @brief   Store and Forward implementation
  */

#include "store_forward.h"
#include "thingspeak.h"
#include "stm32u5xx_hal.h"  // ADD THIS for HAL_GetTick and HAL_Delay
#include <stdio.h>
#include <string.h>
#define MAX_SAMPLES 100  // Maximum stored readings

typedef struct {
  float temperature;
  float humidity;
  float pressure;
  uint32_t timestamp;  // When it was recorded
} StoredData;

static StoredData dataBuffer[MAX_SAMPLES];
static int dataCount = 0;

/**
  * @brief  Initialize Store & Forward module
  */
void SF_Init(void)
{
  dataCount = 0;
  memset(dataBuffer, 0, sizeof(dataBuffer));
  printf("Store & Forward initialized (buffer size: %d)\n", MAX_SAMPLES);
}

/**
  * @brief  Store one sensor reading
  * @param  temp: Temperature value
  * @param  hum: Humidity value
  * @param  press: Pressure value
  */
void SF_StoreData(float temp, float hum, float press)
{
  if (dataCount < MAX_SAMPLES)
  {
    dataBuffer[dataCount].temperature = temp;
    dataBuffer[dataCount].humidity = hum;
    dataBuffer[dataCount].pressure = press;
    dataBuffer[dataCount].timestamp = HAL_GetTick();
    dataCount++;
    printf("Stored sample #%d (buffer: %d/%d)\n", dataCount, dataCount, MAX_SAMPLES);
  }
  else
  {
    printf("WARNING: Store & Forward buffer FULL! Oldest data will be lost.\n");
    // Optional: Overwrite oldest data (circular buffer)
    // Shift all data left and add new one at the end
    for (int i = 0; i < MAX_SAMPLES - 1; i++)
    {
      dataBuffer[i] = dataBuffer[i + 1];
    }
    dataBuffer[MAX_SAMPLES - 1].temperature = temp;
    dataBuffer[MAX_SAMPLES - 1].humidity = hum;
    dataBuffer[MAX_SAMPLES - 1].pressure = press;
    dataBuffer[MAX_SAMPLES - 1].timestamp = HAL_GetTick();
  }
}

/**
  * @brief  Send all stored data to ThingSpeak
  * @retval Number of samples successfully sent
  */
/**
  * @brief  Send all stored data to ThingSpeak
  * @retval Number of samples successfully sent
  */
int SF_SendStoredData(void)
{
  int sent_count = 0;

  printf("\n=== Sending stored data to ThingSpeak ===\n");
  printf("Buffer contains %d samples\n", dataCount);

  while (dataCount > 0)
  {
    float temp = dataBuffer[0].temperature;
    float hum  = dataBuffer[0].humidity;
    float press = dataBuffer[0].pressure;

    printf("Sending sample (T=%.2f H=%.2f P=%.2f)... ", temp, hum, press);

    // Try to send to ThingSpeak (void return, assume success)
    thingspeak_send_data_all(temp, hum, press);
    printf("SENT\n");
    sent_count++;

    // Remove this sample from buffer (shift array left)
    for (int j = 0; j < dataCount - 1; j++)
    {
      dataBuffer[j] = dataBuffer[j + 1];
    }
    dataCount--;

    // ThingSpeak rate limit: wait 15 seconds between uploads
    HAL_Delay(15000);
  }

  printf("=== Send complete: %d samples sent ===\n\n", sent_count);
  return sent_count;
}
/**
  * @brief  Print all stored samples (for debugging)
  */
void SF_PrintStoredData(void)
{
  printf("\n=== Stored Data Buffer ===\n");
  printf("Total samples: %d/%d\n", dataCount, MAX_SAMPLES);

  for (int i = 0; i < dataCount; i++)
  {
    printf("[%d] T=%.2f°C  H=%.2f%%  P=%.2fhPa  (age: %lus)\n",
           i,
           dataBuffer[i].temperature,
           dataBuffer[i].humidity,
           dataBuffer[i].pressure,
           (HAL_GetTick() - dataBuffer[i].timestamp) / 1000);
  }
  printf("==========================\n\n");
}

/**
  * @brief  Get number of stored samples
  * @retval Number of samples in buffer
  */
int SF_GetStoredCount(void)
{
  return dataCount;
}

/**
  * @brief  Check if buffer is full
  * @retval 1 if full, 0 otherwise
  */
int SF_IsBufferFull(void)
{
  return (dataCount >= MAX_SAMPLES);
}
