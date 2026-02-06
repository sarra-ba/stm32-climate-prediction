/**
  ******************************************************************************
  * @file    weather_predict.c
  * @brief   Advanced Weather Prediction Module Implementation
  *          Rain probability, weather conditions, alerts
  ******************************************************************************
  */

#include "weather_predict.h"
#include "main.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ============================================================================
   CONFIGURATION
   ============================================================================ */
#define PRESSURE_HISTORY_SIZE   18    /* 3 hours at 10-min intervals */
#define PRESSURE_HIGH           1020.0f
#define PRESSURE_LOW            1000.0f
#define HUMIDITY_HIGH           80.0f
#define HUMIDITY_VERY_HIGH      90.0f

/* ============================================================================
   PRIVATE VARIABLES
   ============================================================================ */
static WeatherAnalysis_t weather_analysis;
static PressureHistoryEntry_t pressure_history[PRESSURE_HISTORY_SIZE];
static int pressure_history_index = 0;
static int pressure_history_count = 0;

/* ============================================================================
   IMPLEMENTATION
   ============================================================================ */

/**
  * @brief  Initialize weather prediction module
  */
void Weather_Init(void)
{
    memset(&weather_analysis, 0, sizeof(weather_analysis));
    memset(pressure_history, 0, sizeof(pressure_history));
    pressure_history_index = 0;
    pressure_history_count = 0;

    printf("[WEATHER] Weather prediction module initialized\r\n");
}

/**
  * @brief  Calculate dew point using Magnus formula
  */
float Weather_CalcDewPoint(float temp, float hum)
{
    /* Magnus formula constants */
    const float a = 17.27f;
    const float b = 237.7f;

    float alpha = ((a * temp) / (b + temp)) + logf(hum / 100.0f);
    float dew_point = (b * alpha) / (a - alpha);

    return dew_point;
}

/**
  * @brief  Calculate heat index (feels-like temperature)
  *         Uses simplified Rothfusz regression
  */
float Weather_CalcHeatIndex(float temp, float hum)
{
    /* Heat index only applies above 27°C */
    if (temp < 27.0f) {
        return temp;
    }

    /* Simple heat index formula */
    float hi = -8.78469475556f
             + 1.61139411f * temp
             + 2.33854883889f * hum
             - 0.14611605f * temp * hum
             - 0.012308094f * temp * temp
             - 0.0164248277778f * hum * hum
             + 0.002211732f * temp * temp * hum
             + 0.00072546f * temp * hum * hum
             - 0.000003582f * temp * temp * hum * hum;

    return hi;
}

/**
  * @brief  Calculate pressure trend (change over 3 hours)
  */
static float calc_pressure_trend(void)
{
    if (pressure_history_count < 2) {
        return 0.0f;
    }

    /* Get oldest and newest entries */
    int oldest_idx = (pressure_history_index - pressure_history_count + PRESSURE_HISTORY_SIZE) % PRESSURE_HISTORY_SIZE;
    int newest_idx = (pressure_history_index - 1 + PRESSURE_HISTORY_SIZE) % PRESSURE_HISTORY_SIZE;

    float oldest_pressure = pressure_history[oldest_idx].pressure;
    float newest_pressure = pressure_history[newest_idx].pressure;
    uint32_t time_diff_ms = pressure_history[newest_idx].timestamp - pressure_history[oldest_idx].timestamp;

    if (time_diff_ms == 0) return 0.0f;

    /* Calculate hPa per 3 hours */
    float hours = time_diff_ms / 3600000.0f;
    float change_per_hour = (newest_pressure - oldest_pressure) / hours;

    return change_per_hour * 3.0f;  /* Return change over 3 hours */
}

/**
  * @brief  Determine pressure trend category
  */
static PressureTrend_t get_pressure_trend_category(float change_3h)
{
    if (change_3h > 4.0f) return PRESSURE_RISING_FAST;
    if (change_3h > 1.5f) return PRESSURE_RISING;
    if (change_3h > -1.5f) return PRESSURE_STABLE;
    if (change_3h > -4.0f) return PRESSURE_FALLING;
    return PRESSURE_FALLING_FAST;
}

/**
  * @brief  Calculate rain probability
  */
uint8_t Weather_CalcRainProbability(float humidity, float pressure, float pressure_trend)
{
    float prob = 0.0f;

    /* Base probability from humidity */
    if (humidity >= 95.0f) prob += 60.0f;
    else if (humidity >= 90.0f) prob += 45.0f;
    else if (humidity >= 85.0f) prob += 30.0f;
    else if (humidity >= 80.0f) prob += 20.0f;
    else if (humidity >= 70.0f) prob += 10.0f;
    else if (humidity >= 60.0f) prob += 5.0f;

    /* Modify based on pressure */
    if (pressure < 1000.0f) prob += 25.0f;
    else if (pressure < 1005.0f) prob += 15.0f;
    else if (pressure < 1010.0f) prob += 10.0f;
    else if (pressure > 1020.0f) prob -= 15.0f;
    else if (pressure > 1015.0f) prob -= 10.0f;

    /* Modify based on pressure trend (3h change) */
    if (pressure_trend < -6.0f) prob += 35.0f;      /* Rapidly falling */
    else if (pressure_trend < -4.0f) prob += 25.0f;
    else if (pressure_trend < -2.0f) prob += 15.0f;
    else if (pressure_trend < -1.0f) prob += 8.0f;
    else if (pressure_trend > 3.0f) prob -= 15.0f;  /* Rising = clearing */
    else if (pressure_trend > 1.5f) prob -= 10.0f;

    /* Clamp to 0-100 */
    if (prob < 0.0f) prob = 0.0f;
    if (prob > 100.0f) prob = 100.0f;

    return (uint8_t)prob;
}

/**
  * @brief  Determine weather condition
  */
WeatherCondition_t Weather_DetermineCondition(float temp, float hum, float press, uint8_t rain_prob)
{
    /* Storm conditions */
    if (rain_prob >= 85 && press < 1000.0f) {
        return WEATHER_STORM;
    }

    /* Heavy rain */
    if (rain_prob >= 75) {
        return WEATHER_HEAVY_RAIN;
    }

    /* Rain */
    if (rain_prob >= 55) {
        return WEATHER_RAIN;
    }

    /* Light rain possible */
    if (rain_prob >= 40) {
        return WEATHER_LIGHT_RAIN;
    }

    /* Overcast */
    if (hum >= 85 && rain_prob >= 25) {
        return WEATHER_OVERCAST;
    }

    /* Cloudy */
    if (hum >= 70 || rain_prob >= 20) {
        return WEATHER_CLOUDY;
    }

    /* Partly cloudy */
    if (hum >= 50) {
        return WEATHER_PARTLY_CLOUDY;
    }

    /* Clear */
    return WEATHER_CLEAR;
}

/**
  * @brief  Check for weather alerts
  */
static WeatherAlert_t check_alerts(WeatherAnalysis_t *analysis)
{
    analysis->alert_count = 0;

    /* Storm warning - rapid pressure drop */
    if (analysis->pressure_change_3h < -6.0f) {
        analysis->alert_count++;
        return ALERT_STORM_WARNING;
    }

    /* Rain likely */
    if (analysis->rain_prob_1h >= 60 || analysis->rain_prob_2h >= 70) {
        analysis->alert_count++;
        return ALERT_RAIN_LIKELY;
    }

    /* Frost warning */
    if (analysis->temperature <= 3.0f || analysis->temp_6h <= 1.0f) {
        analysis->alert_count++;
        return ALERT_FROST_WARNING;
    }

    /* Heat warning */
    if (analysis->heat_index >= 40.0f) {
        analysis->alert_count++;
        return ALERT_HEAT_WARNING;
    }

    /* High humidity discomfort */
    if (analysis->humidity >= 90.0f && analysis->temperature >= 28.0f) {
        analysis->alert_count++;
        return ALERT_HIGH_HUMIDITY;
    }

    /* Low pressure */
    if (analysis->pressure < 995.0f) {
        analysis->alert_count++;
        return ALERT_LOW_PRESSURE;
    }

    return ALERT_NONE;
}

/**
  * @brief  Calculate comfort level (0-100, 50=ideal)
  */
static uint8_t calc_comfort_level(float temp, float hum)
{
    uint8_t comfort = 50;

    /* Temperature comfort (ideal: 20-24°C) */
    if (temp < 15.0f) comfort -= (uint8_t)((15.0f - temp) * 3);
    else if (temp < 20.0f) comfort -= (uint8_t)((20.0f - temp) * 2);
    else if (temp > 28.0f) comfort -= (uint8_t)((temp - 28.0f) * 3);
    else if (temp > 24.0f) comfort -= (uint8_t)((temp - 24.0f) * 2);

    /* Humidity comfort (ideal: 40-60%) */
    if (hum < 30.0f) comfort -= (uint8_t)((30.0f - hum) * 0.5f);
    else if (hum > 80.0f) comfort -= (uint8_t)((hum - 80.0f) * 1.0f);
    else if (hum > 60.0f) comfort -= (uint8_t)((hum - 60.0f) * 0.5f);

    /* Clamp */
    if (comfort > 100) comfort = 100;

    return comfort;
}

/**
  * @brief  Update weather analysis with new sensor data
  */
void Weather_Update(float temp, float hum, float press)
{
    /* Store current values */
    weather_analysis.temperature = temp;
    weather_analysis.humidity = hum;
    weather_analysis.pressure = press;

    /* Calculate derived values */
    weather_analysis.dew_point = Weather_CalcDewPoint(temp, hum);
    weather_analysis.heat_index = Weather_CalcHeatIndex(temp, hum);

    /* Update pressure history */
    pressure_history[pressure_history_index].pressure = press;
    pressure_history[pressure_history_index].timestamp = HAL_GetTick();
    pressure_history_index = (pressure_history_index + 1) % PRESSURE_HISTORY_SIZE;
    if (pressure_history_count < PRESSURE_HISTORY_SIZE) {
        pressure_history_count++;
    }

    /* Calculate pressure trend */
    weather_analysis.pressure_change_3h = calc_pressure_trend();
    weather_analysis.pressure_trend = get_pressure_trend_category(weather_analysis.pressure_change_3h);

    /* Calculate current rain probability */
    weather_analysis.rain_prob_now = Weather_CalcRainProbability(hum, press, weather_analysis.pressure_change_3h);

    /* Determine current condition */
    weather_analysis.condition_now = Weather_DetermineCondition(temp, hum, press, weather_analysis.rain_prob_now);

    /* Calculate comfort */
    weather_analysis.comfort_level = calc_comfort_level(temp, hum);

    /* Check alerts */
    weather_analysis.primary_alert = check_alerts(&weather_analysis);
}

/**
  * @brief  Set AI forecast predictions
  */
void Weather_SetForecast(float temp_1h, float temp_2h, float temp_6h,
                         float hum_1h, float hum_2h, float hum_6h,
                         float press_1h, float press_2h, float press_6h)
{
    /* Store predictions */
    weather_analysis.temp_1h = temp_1h;
    weather_analysis.temp_2h = temp_2h;
    weather_analysis.temp_6h = temp_6h;

    weather_analysis.hum_1h = hum_1h;
    weather_analysis.hum_2h = hum_2h;
    weather_analysis.hum_6h = hum_6h;

    weather_analysis.press_1h = press_1h;
    weather_analysis.press_2h = press_2h;
    weather_analysis.press_6h = press_6h;

    /* Calculate future rain probabilities */
    float trend_1h = press_1h - weather_analysis.pressure;
    float trend_2h = press_2h - weather_analysis.pressure;
    float trend_6h = press_6h - weather_analysis.pressure;

    weather_analysis.rain_prob_1h = Weather_CalcRainProbability(hum_1h, press_1h, trend_1h);
    weather_analysis.rain_prob_2h = Weather_CalcRainProbability(hum_2h, press_2h, trend_2h);
    weather_analysis.rain_prob_6h = Weather_CalcRainProbability(hum_6h, press_6h, trend_6h);

    /* Determine future conditions */
    weather_analysis.condition_1h = Weather_DetermineCondition(temp_1h, hum_1h, press_1h, weather_analysis.rain_prob_1h);
    weather_analysis.condition_2h = Weather_DetermineCondition(temp_2h, hum_2h, press_2h, weather_analysis.rain_prob_2h);
    weather_analysis.condition_6h = Weather_DetermineCondition(temp_6h, hum_6h, press_6h, weather_analysis.rain_prob_6h);

    weather_analysis.forecast_valid = 1;

    /* Update alerts with forecast data */
    weather_analysis.primary_alert = check_alerts(&weather_analysis);
}

/**
  * @brief  Get complete weather analysis
  */
WeatherAnalysis_t* Weather_GetAnalysis(void)
{
    return &weather_analysis;
}

/**
  * @brief  Get weather condition as string
  */
const char* Weather_GetConditionString(WeatherCondition_t condition)
{
    switch (condition) {
        case WEATHER_CLEAR:         return "Clear";
        case WEATHER_PARTLY_CLOUDY: return "Partly Cloudy";
        case WEATHER_CLOUDY:        return "Cloudy";
        case WEATHER_OVERCAST:      return "Overcast";
        case WEATHER_LIGHT_RAIN:    return "Light Rain";
        case WEATHER_RAIN:          return "Rain";
        case WEATHER_HEAVY_RAIN:    return "Heavy Rain";
        case WEATHER_STORM:         return "Storm";
        default:                    return "Unknown";
    }
}

/**
  * @brief  Get weather condition icon
  */
const char* Weather_GetConditionIcon(WeatherCondition_t condition)
{
    switch (condition) {
        case WEATHER_CLEAR:         return "sun";
        case WEATHER_PARTLY_CLOUDY: return "cloud-sun";
        case WEATHER_CLOUDY:        return "cloud";
        case WEATHER_OVERCAST:      return "cloud";
        case WEATHER_LIGHT_RAIN:    return "cloud-sun-rain";
        case WEATHER_RAIN:          return "cloud-rain";
        case WEATHER_HEAVY_RAIN:    return "cloud-showers-heavy";
        case WEATHER_STORM:         return "bolt";
        default:                    return "question";
    }
}

/**
  * @brief  Get alert string
  */
const char* Weather_GetAlertString(WeatherAlert_t alert)
{
    switch (alert) {
        case ALERT_NONE:          return "No alerts";
        case ALERT_RAIN_LIKELY:   return "Rain likely";
        case ALERT_STORM_WARNING: return "Storm warning!";
        case ALERT_FROST_WARNING: return "Frost warning";
        case ALERT_HEAT_WARNING:  return "Heat warning";
        case ALERT_HIGH_HUMIDITY: return "High humidity";
        case ALERT_LOW_PRESSURE:  return "Low pressure";
        default:                  return "Unknown";
    }
}

/**
  * @brief  Get pressure trend string
  */
const char* Weather_GetPressureTrendString(PressureTrend_t trend)
{
    switch (trend) {
        case PRESSURE_RISING_FAST:  return "Rising rapidly - Clearing";
        case PRESSURE_RISING:       return "Rising - Improving";
        case PRESSURE_STABLE:       return "Stable";
        case PRESSURE_FALLING:      return "Falling - Deteriorating";
        case PRESSURE_FALLING_FAST: return "Falling rapidly - Storm approaching";
        default:                    return "Unknown";
    }
}
