/**
  ******************************************************************************
  * @file    ai_inference.c
  * @brief   AI Weather Forecast Inference Implementation
  *          Multi-variable, multi-horizon prediction using X-CUBE-AI
  ******************************************************************************
  */

#include "ai_inference.h"
#include "network.h"
#include "network_data.h"
#include <string.h>
#include <math.h>

/* Private Variables */
static float input_buffer[AI_INPUT_SAMPLES][AI_INPUT_FEATURES];  /* Circular buffer for 3 variables */
static int sample_count = 0;
static int buffer_index = 0;
static AI_Status_t ai_status = AI_STATUS_INIT;
static WeatherForecast_t weather_forecast;

/* AI Network Handles */
static ai_handle network_handle = AI_HANDLE_NULL;
static ai_buffer *ai_input;
static ai_buffer *ai_output;

/* Activation buffers */
AI_ALIGNED(4) static ai_u8 activations[AI_NETWORK_DATA_ACTIVATIONS_SIZE];

/* Input/Output buffers */
AI_ALIGNED(4) static ai_float in_data[AI_INPUT_SAMPLES * AI_INPUT_FEATURES];
AI_ALIGNED(4) static ai_float out_data[AI_NUM_OUTPUTS];

/* Threshold for trend detection */
#define TEMP_TREND_THRESHOLD  0.5f   /* °C */
#define HUM_TREND_THRESHOLD   3.0f   /* % */
#define PRESS_TREND_THRESHOLD 2.0f   /* hPa */

/* Private Function Prototypes */
static float normalize_temp(float temp);
static float normalize_hum(float hum);
static float normalize_press(float press);
static float denormalize_temp(float norm);
static float denormalize_hum(float norm);
static float denormalize_press(float norm);
static TrendCode_t calculate_trend(float current, float predicted, float threshold);

/**
  * @brief  Initialize AI inference system
  */
void AI_Init(void)
{
    ai_error err;

    /* Clear buffers */
    memset(input_buffer, 0, sizeof(input_buffer));
    memset(&weather_forecast, 0, sizeof(weather_forecast));
    sample_count = 0;
    buffer_index = 0;

    /* Create and initialize the network */
    err = ai_network_create(&network_handle, AI_NETWORK_DATA_CONFIG);
    if (err.type != AI_ERROR_NONE) {
        ai_status = AI_STATUS_ERROR;
        return;
    }

    /* Initialize network */
    const ai_network_params params = {
        AI_NETWORK_DATA_WEIGHTS(ai_network_data_weights_get()),
        AI_NETWORK_DATA_ACTIVATIONS(activations)
    };

    if (!ai_network_init(network_handle, &params)) {
        ai_status = AI_STATUS_ERROR;
        return;
    }

    /* Get input/output buffer info */
    ai_input = ai_network_inputs_get(network_handle, NULL);
    ai_output = ai_network_outputs_get(network_handle, NULL);

    ai_status = AI_STATUS_COLLECTING;
}

/**
  * @brief  Add a new sensor sample to the buffer
  * @param  temperature: Current temperature in °C
  * @param  humidity: Current humidity in %
  * @param  pressure: Current pressure in hPa
  */
void AI_AddSample(float temperature, float humidity, float pressure)
{
    /* Store normalized values in circular buffer */
    input_buffer[buffer_index][0] = normalize_temp(temperature);
    input_buffer[buffer_index][1] = normalize_hum(humidity);
    input_buffer[buffer_index][2] = normalize_press(pressure);

    /* Update buffer index */
    buffer_index = (buffer_index + 1) % AI_INPUT_SAMPLES;

    /* Track total samples collected */
    if (sample_count < AI_INPUT_SAMPLES) {
        sample_count++;
    }

    /* Update current readings */
    weather_forecast.current_temp = temperature;
    weather_forecast.current_hum = humidity;
    weather_forecast.current_press = pressure;

    /* Update status */
    if (sample_count >= AI_INPUT_SAMPLES) {
        ai_status = AI_STATUS_READY;
    } else {
        ai_status = AI_STATUS_COLLECTING;
    }
}

/**
  * @brief  Run AI inference to generate weather forecast
  * @retval 0 on success, -1 on error
  */
int AI_RunInference(void)
{
    if (ai_status != AI_STATUS_READY) {
        return -1;
    }

    /* Prepare input data in correct order (oldest to newest) */
    int idx = 0;
    for (int i = 0; i < AI_INPUT_SAMPLES; i++) {
        int buf_idx = (buffer_index + i) % AI_INPUT_SAMPLES;
        for (int f = 0; f < AI_INPUT_FEATURES; f++) {
            in_data[idx++] = input_buffer[buf_idx][f];
        }
    }

    /* Set input buffer */
    ai_input[0].data = AI_HANDLE_PTR(in_data);
    ai_output[0].data = AI_HANDLE_PTR(out_data);

    /* Run inference */
    ai_i32 batch = ai_network_run(network_handle, ai_input, ai_output);
    if (batch != 1) {
        ai_status = AI_STATUS_ERROR;
        return -1;
    }

    /* Extract and denormalize predictions */
    /* Temperature forecasts */
    weather_forecast.temp_1h = denormalize_temp(out_data[IDX_TEMP_1H]);
    weather_forecast.temp_2h = denormalize_temp(out_data[IDX_TEMP_2H]);
    weather_forecast.temp_6h = denormalize_temp(out_data[IDX_TEMP_6H]);
    weather_forecast.temp_trend = calculate_trend(
        weather_forecast.current_temp,
        weather_forecast.temp_1h,
        TEMP_TREND_THRESHOLD
    );

    /* Humidity forecasts */
    weather_forecast.hum_1h = denormalize_hum(out_data[IDX_HUM_1H]);
    weather_forecast.hum_2h = denormalize_hum(out_data[IDX_HUM_2H]);
    weather_forecast.hum_6h = denormalize_hum(out_data[IDX_HUM_6H]);
    weather_forecast.hum_trend = calculate_trend(
        weather_forecast.current_hum,
        weather_forecast.hum_1h,
        HUM_TREND_THRESHOLD
    );

    /* Pressure forecasts */
    weather_forecast.press_1h = denormalize_press(out_data[IDX_PRESS_1H]);
    weather_forecast.press_2h = denormalize_press(out_data[IDX_PRESS_2H]);
    weather_forecast.press_6h = denormalize_press(out_data[IDX_PRESS_6H]);
    weather_forecast.press_trend = calculate_trend(
        weather_forecast.current_press,
        weather_forecast.press_1h,
        PRESS_TREND_THRESHOLD
    );

    return 0;
}

/**
  * @brief  Get current AI status
  * @retval AI_Status_t
  */
AI_Status_t AI_GetStatus(void)
{
    return ai_status;
}

/**
  * @brief  Get number of samples collected
  * @retval Number of samples
  */
int AI_GetSamplesCollected(void)
{
    return sample_count;
}

/**
  * @brief  Get number of samples required
  * @retval Required samples (24)
  */
int AI_GetSamplesRequired(void)
{
    return AI_INPUT_SAMPLES;
}

/**
  * @brief  Get seconds remaining until ready
  * @retval Seconds remaining
  */
int AI_GetSecondsRemaining(void)
{
    int remaining = AI_INPUT_SAMPLES - sample_count;
    return (remaining * AI_SAMPLE_INTERVAL_MS) / 1000;
}

/**
  * @brief  Get weather forecast data
  * @retval Pointer to WeatherForecast_t structure
  */
WeatherForecast_t* AI_GetForecast(void)
{
    return &weather_forecast;
}

/* ============== Private Functions ============== */

static float normalize_temp(float temp)
{
    return (temp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
}

static float normalize_hum(float hum)
{
    return (hum - HUM_MIN) / (HUM_MAX - HUM_MIN);
}

static float normalize_press(float press)
{
    return (press - PRESS_MIN) / (PRESS_MAX - PRESS_MIN);
}

static float denormalize_temp(float norm)
{
    return norm * (TEMP_MAX - TEMP_MIN) + TEMP_MIN;
}

static float denormalize_hum(float norm)
{
    return norm * (HUM_MAX - HUM_MIN) + HUM_MIN;
}

static float denormalize_press(float norm)
{
    return norm * (PRESS_MAX - PRESS_MIN) + PRESS_MIN;
}

static TrendCode_t calculate_trend(float current, float predicted, float threshold)
{
    float diff = predicted - current;

    if (diff > threshold) {
        return TREND_RISING;
    } else if (diff < -threshold) {
        return TREND_FALLING;
    } else {
        return TREND_STABLE;
    }
}
