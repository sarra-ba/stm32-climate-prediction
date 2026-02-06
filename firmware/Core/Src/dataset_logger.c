/**
  * @file    dataset_logger.c
  * @brief   Logs sensor data to memory for AI training dataset export
  */

#include "dataset_logger.h"
#include "stm32u5xx_hal.h"
#include <stdio.h>
#include <string.h>

/* Dataset storage */
static DatasetSample dataset[DATASET_MAX_SAMPLES];
static int dataset_count = 0;
static uint32_t sample_counter = 0;

/**
  * @brief  Initialize the dataset logger
  */
void Dataset_Init(void)
{
    memset(dataset, 0, sizeof(dataset));
    dataset_count = 0;
    sample_counter = 0;
    printf("\n=== Dataset Logger Initialized ===\n");
    printf("Capacity: %d samples\n", DATASET_MAX_SAMPLES);
    printf("Export via: /export_dataset endpoint\n");
    printf("==================================\n\n");
}

/**
  * @brief  Log a sensor sample to the dataset
  * @param  temperature: Temperature value in Celsius
  * @param  humidity: Humidity value in percentage
  * @param  pressure: Pressure value in hPa
  */
void Dataset_LogSample(float temperature, float humidity, float pressure)
{
    if (dataset_count >= DATASET_MAX_SAMPLES)
    {
        /* Circular buffer - overwrite oldest sample */
        memmove(&dataset[0], &dataset[1], sizeof(DatasetSample) * (DATASET_MAX_SAMPLES - 1));
        dataset_count = DATASET_MAX_SAMPLES - 1;
        printf("[Dataset] WARNING: Buffer full, overwriting oldest data\n");
    }

    uint32_t time_sec = HAL_GetTick() / 1000;

    dataset[dataset_count].timestamp = time_sec;
    dataset[dataset_count].temperature = temperature;
    dataset[dataset_count].humidity = humidity;
    dataset[dataset_count].pressure = pressure;
    dataset[dataset_count].hour = (time_sec / 3600) % 24;
    dataset[dataset_count].uploaded = 0;

    dataset_count++;

    /* Log progress every 10 samples */
    if (dataset_count % 10 == 0)
    {
        printf("[Dataset] Logged %d/%d samples\n", dataset_count, DATASET_MAX_SAMPLES);
    }
}

/**
  * @brief  Export dataset as CSV to serial console
  */
void Dataset_ExportCSV(void)
{
    printf("\n");
    printf("========================================\n");
    printf("DATASET CSV EXPORT - START\n");
    printf("========================================\n");
    printf("timestamp,temperature,humidity,pressure,hour,uploaded\n");

    for (int i = 0; i < dataset_count; i++)
    {
        printf("%lu,%.2f,%.2f,%.2f,%d,%d\n",
               dataset[i].timestamp,
               dataset[i].temperature,
               dataset[i].humidity,
               dataset[i].pressure,
               dataset[i].hour,
               dataset[i].uploaded);
    }

    printf("========================================\n");
    printf("DATASET CSV EXPORT - END\n");
    printf("Total samples: %d\n", dataset_count);
    printf("========================================\n");
    printf("\n** Copy the CSV data above (between START and END) **\n");
    printf("** Save as: sensor_data.csv **\n\n");
}

/**
  * @brief  Save dataset to file (placeholder - not implemented without SD card)
  * @param  filename: Path to save the CSV file
  * @retval -1 (not implemented)
  */
int Dataset_SaveToFile(const char *filename)
{
    printf("[Dataset] SD card not available - use /export_dataset for serial output\n");
    return -1;
}

/**
  * @brief  Get current dataset count
  * @retval Number of samples in dataset
  */
int Dataset_GetCount(void)
{
    return dataset_count;
}

/**
  * @brief  Clear the dataset
  */
void Dataset_Clear(void)
{
    memset(dataset, 0, sizeof(dataset));
    dataset_count = 0;
    sample_counter = 0;
    printf("[Dataset] Buffer cleared\n");
}

/**
  * @brief  Print dataset statistics
  */
void Dataset_PrintStats(void)
{
    if (dataset_count == 0)
    {
        printf("\n[Dataset] No data collected yet\n\n");
        return;
    }

    float temp_min = dataset[0].temperature;
    float temp_max = dataset[0].temperature;
    float temp_sum = 0;
    float hum_sum = 0;
    float press_sum = 0;
    uint32_t duration = dataset[dataset_count-1].timestamp - dataset[0].timestamp;

    for (int i = 0; i < dataset_count; i++)
    {
        if (dataset[i].temperature < temp_min) temp_min = dataset[i].temperature;
        if (dataset[i].temperature > temp_max) temp_max = dataset[i].temperature;
        temp_sum += dataset[i].temperature;
        hum_sum += dataset[i].humidity;
        press_sum += dataset[i].pressure;
    }

    printf("\n=== Dataset Statistics ===\n");
    printf("Samples: %d/%d (%.1f%% full)\n",
           dataset_count, DATASET_MAX_SAMPLES,
           (dataset_count * 100.0f) / DATASET_MAX_SAMPLES);
    printf("Temperature: min=%.2f°C, max=%.2f°C, avg=%.2f°C\n",
           temp_min, temp_max, temp_sum / dataset_count);
    printf("Humidity: avg=%.2f%%\n", hum_sum / dataset_count);
    printf("Pressure: avg=%.2f hPa\n", press_sum / dataset_count);
    printf("Duration: %lu seconds (%.1f hours)\n",
           duration, duration / 3600.0f);
    printf("========================\n\n");
}

/**
  * @brief  Get pointer to sample (for HTTP streaming)
  * @param  index: Sample index
  * @retval Pointer to sample, or NULL if invalid
  */
DatasetSample* Dataset_GetSample(int index)
{
    if (index >= 0 && index < dataset_count)
    {
        return &dataset[index];
    }
    return NULL;
}
