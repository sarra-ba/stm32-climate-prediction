/**
  **********************************************************************************************************************
  * @file    webserver_http_response.c
  * @author  MCD Application Team
  * @brief   This file implements the web server http response services
  *          Updated for Full Weather Station with Rain Prediction + Low Power Monitor
  **********************************************************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  **********************************************************************************************************************
  */

/* Includes ----------------------------------------------------------------------------------------------------------*/
#include "webserver_http_response.h"
#include "webserver_http_encoder.h"
#include "net_connect.h"
#include "net_interface.h"
#include "mx_wifi.h"
#include "thingspeak.h"
#include "store_forward.h"
#include "dataset_logger.h"
#include "../../X-CUBE-AI/App/ai_inference.h"
#include "weather_predict.h"
#include "low_power.h"
#include "weather_station_res.h"
#include "power_dashboard_res.h"

/* External network interface handle */
extern net_if_handle_t *Netif;

/* Private typedef ---------------------------------------------------------------------------------------------------*/
/* Private define ----------------------------------------------------------------------------------------------------*/
#define HTTP_SERVER_PORT         (80U)
#define HTTP_RECEIVE_BUFFER_SIZE (1500U)
#define HTTP_SENSORS_BUFFER_SIZE (20U)
#define HTTP_HEADERS_BUFFER_SIZE (500U)

#define MAX_SOCKET_DATASIZE      (MX_WIFI_BUFFER_SIZE - 100U)

/* AI Auto-sampling configuration */
#define AI_AUTO_SAMPLE_INTERVAL_MS    (6000)  /* 10 minutes */

/* Private macro -----------------------------------------------------------------------------------------------------*/
/* Private variables -------------------------------------------------------------------------------------------------*/
/* HTTP buffers declaration */
char http_sensor_value[HTTP_SENSORS_BUFFER_SIZE];
char http_header_response[HTTP_HEADERS_BUFFER_SIZE];

/* AI variables */
static uint8_t ai_initialized = 0;
static uint32_t last_sample_time = 0;

/* Private function prototypes ---------------------------------------------------------------------------------------*/
static WebServer_StatusTypeDef http_treat_request(int32_t socket);
static WebServer_StatusTypeDef http_send_headers_response(int32_t socket,
                                                          uint32_t headers_id,
                                                          char *headers_buff,
                                                          uint32_t data_size);
static WebServer_StatusTypeDef http_send_response(int32_t socket,
                                                  uint32_t headers_id,
                                                  char *headers_buff,
                                                  const char *body_buff,
                                                  uint32_t data_size);
static WebServer_StatusTypeDef http_send(int32_t socket,
                                         const char *frame,
                                         uint32_t frame_size);
static void ai_auto_sample(void);

/* Functions prototypes ----------------------------------------------------------------------------------------------*/

/**
  * @brief  Automatic AI sampling with weather prediction
  * @param  None
  * @retval None
  */
static void ai_auto_sample(void)
{
    uint32_t current_time = HAL_GetTick();

    /* Check if it's time for a new sample */
    if ((current_time - last_sample_time) < AI_AUTO_SAMPLE_INTERVAL_MS)
    {
        return;  /* Not time yet */
    }

    last_sample_time = current_time;

    if (!ai_initialized)
    {
        return;
    }

    /* Read sensors */
    float temperature = 0, humidity = 0, pressure = 0;
    webserver_temp_sensor_read(&temperature);
    webserver_humid_sensor_read(&humidity);
    webserver_press_sensor_read(&pressure);

    /* Update weather analysis (calculates rain probability, trends, alerts) */
    Weather_Update(temperature, humidity, pressure);

    /* Add sample to AI buffer */
    AI_AddSample(temperature, humidity, pressure);

    /* Also log to dataset */
    Dataset_LogSample(temperature, humidity, pressure);

    /* Check status and run inference if ready */
    AI_Status_t status = AI_GetStatus();

    if (status == AI_STATUS_READY)
    {
        /* Run inference */
        if (AI_RunInference() == 0)
        {
            WeatherForecast_t *forecast = AI_GetForecast();

            /* Update weather module with AI predictions */
            Weather_SetForecast(
                forecast->temp_1h, forecast->temp_2h, forecast->temp_6h,
                forecast->hum_1h, forecast->hum_2h, forecast->hum_6h,
                forecast->press_1h, forecast->press_2h, forecast->press_6h
            );

            /* Get complete weather analysis */
            WeatherAnalysis_t *weather = Weather_GetAnalysis();

            /* Print comprehensive weather report */
            printf("\r\n");
            printf("╔═══════════════════════════════════════════════════════════════╗\r\n");
            printf("║           🌤️  AI WEATHER STATION                              ║\r\n");
            printf("╠═══════════════════════════════════════════════════════════════╣\r\n");
            printf("║  Current: %-12s  %.1f°C  %.0f%% humidity                 ║\r\n",
                   Weather_GetConditionString(weather->condition_now),
                   weather->temperature, weather->humidity);
            printf("║                                                               ║\r\n");
            printf("║  🌡️  Temperature:                                             ║\r\n");
            printf("║     Now: %5.1f°C  +1h: %5.1f°C  +2h: %5.1f°C  +6h: %5.1f°C   ║\r\n",
                   forecast->current_temp, forecast->temp_1h,
                   forecast->temp_2h, forecast->temp_6h);
            printf("║                                                               ║\r\n");
            printf("║  🌧️  Rain Probability:                                        ║\r\n");
            printf("║     Now: %2d%%    +1h: %2d%%    +2h: %2d%%    +6h: %2d%%           ║\r\n",
                   weather->rain_prob_now, weather->rain_prob_1h,
                   weather->rain_prob_2h, weather->rain_prob_6h);
            printf("║                                                               ║\r\n");
            printf("║  📊 Pressure: %.1f hPa  (%+.1f hPa/3h)                       ║\r\n",
                   weather->pressure, weather->pressure_change_3h);
            printf("║     %s                            ║\r\n",
                   Weather_GetPressureTrendString(weather->pressure_trend));
            printf("║                                                               ║\r\n");
            printf("║  🌡️  Comfort: %d%%  |  Dew Point: %.1f°C  |  Feels: %.1f°C    ║\r\n",
                   weather->comfort_level, weather->dew_point, weather->heat_index);
            if (weather->primary_alert != ALERT_NONE) {
                printf("║                                                               ║\r\n");
                printf("║  ⚠️  ALERT: %-45s  ║\r\n",
                       Weather_GetAlertString(weather->primary_alert));
            }
            printf("╚═══════════════════════════════════════════════════════════════╝\r\n");
            printf("\r\n");
        }
    }
    else if (status == AI_STATUS_COLLECTING)
    {
        /* Get current weather analysis even while collecting */
        WeatherAnalysis_t *weather = Weather_GetAnalysis();

        printf("[WEATHER] T=%.1f°C H=%.0f%% P=%.1f hPa | Rain: %d%% | %s | Samples: %d/%d\r\n",
               temperature, humidity, pressure,
               weather->rain_prob_now,
               Weather_GetConditionString(weather->condition_now),
               AI_GetSamplesCollected(), AI_GetSamplesRequired());
    }
}

/**
  * @brief  Start HTTP web server process
  * @param  None
  * @retval Web Server status
  */
WebServer_StatusTypeDef webserver_http_start(void)
{
  /* The IPv4 network socket for this server, to bind with the port to listen to. */
  struct net_sockaddr_in s_addr_in = {0};
  net_ip_addr_t ip_addr_in = {0};
  int32_t timeout = 5000;  /* Reduced timeout to allow auto-sampling */
  int32_t sock = 0;

  /* ============================================================
     INITIALIZE AI WEATHER STATION
     ============================================================ */
  printf("\r\n");
  printf("╔═══════════════════════════════════════════════════════════╗\r\n");
  printf("║       🌤️  AI WEATHER STATION - STM32U5                    ║\r\n");
  printf("╠═══════════════════════════════════════════════════════════╣\r\n");
  printf("║  Features:                                                ║\r\n");
  printf("║    • Temperature, Humidity, Pressure forecasts           ║\r\n");
  printf("║    • Rain probability prediction                         ║\r\n");
  printf("║    • Weather condition icons                             ║\r\n");
  printf("║    • Storm and weather alerts                            ║\r\n");
  printf("║    • Dew point and heat index                            ║\r\n");
  printf("║    • Low power monitoring                                ║\r\n");
  printf("╚═══════════════════════════════════════════════════════════╝\r\n");

  /* Initialize AI */
  AI_Init();
  AI_Status_t status = AI_GetStatus();

  /* Initialize Weather Prediction */
  Weather_Init();

  /* Initialize Low Power Manager */
  LowPower_Init();

  if (status != AI_STATUS_ERROR)
  {
    ai_initialized = 1;
    printf("[AI] Ready! Auto-sampling every %lu seconds\r\n", AI_AUTO_SAMPLE_INTERVAL_MS / 1000);
    printf("[AI] First forecast after %d samples (~%lu seconds)\r\n",
           AI_GetSamplesRequired(), (AI_GetSamplesRequired() * AI_AUTO_SAMPLE_INTERVAL_MS) / 1000);
  }
  else
  {
    printf("[AI] WARNING: Initialization failed - forecasts disabled\r\n");
    ai_initialized = 0;
  }
  printf("\r\n");

  /* Initialize sample timer */
  last_sample_time = HAL_GetTick();

  /* Create a TCP socket. */
  printf("*** Create TCP socket\r\n");
  if ((sock = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP)) < 0)
  {
    printf("*** Fail : Socket not created !!!!\r\n");
    return SOCKET_ERROR;
  }
  printf("*** TCP socket created\r\n");

  printf("*** net_setsockopt ...\r\n");
  net_setsockopt(sock, NET_SOL_SOCKET, NET_SO_BINDTODEVICE, Netif, sizeof(&Netif));
  net_setsockopt(sock, NET_SOL_SOCKET, NET_SO_RCVTIMEO, &timeout, sizeof(timeout));

  /* Bind socket */
  printf("*** Set port and bind socket\r\n");
  s_addr_in.sin_family = NET_AF_INET;
  s_addr_in.sin_len    = sizeof(s_addr_in);

  net_if_get_ip_address(Netif, &ip_addr_in);
  s_addr_in.sin_addr.s_addr = ip_addr_in.addr;

  net_set_port((struct net_sockaddr *)&s_addr_in, HTTP_SERVER_PORT);
  if (net_bind(sock, (struct net_sockaddr *)&s_addr_in, sizeof(s_addr_in)) != 0U)
  {
    printf("*** Fail : Socket not binded !!!!\r\n");
    return SOCKET_ERROR;
  }
  printf("*** Port and socket binded\r\n");

  /* listen for incoming connections */
  printf("*** Listen for incoming connections\r\n");
  if (net_listen(sock, 5) != 0U)
  {
    printf("*** Fail : Listening not started !!!!\r\n");
    return SOCKET_ERROR;
  }
  printf("*** Listening started \r\n");

  printf("\r\n");
  printf("╔═══════════════════════════════════════════════════════════╗\r\n");
  printf("║  HTTP Server Active                                       ║\r\n");
  printf("║  Connect to: http://%s                        ║\r\n", net_ntoa(&ip_addr_in));
  printf("╠═══════════════════════════════════════════════════════════╣\r\n");
  printf("║  Pages:                                                   ║\r\n");
  printf("║    /             - Main dashboard                         ║\r\n");
  printf("║    /weather      - Weather station                        ║\r\n");
  printf("║    /power        - Power monitor                          ║\r\n");
  printf("╠═══════════════════════════════════════════════════════════╣\r\n");
  printf("║  APIs:                                                    ║\r\n");
  printf("║    /api/weather  - Full weather data + rain probability   ║\r\n");
  printf("║    /api/forecast - AI forecast data                       ║\r\n");
  printf("║    /api/power    - Power statistics                       ║\r\n");
  printf("╚═══════════════════════════════════════════════════════════╝\r\n");
  printf("\r\n");

  /* Infinite loop to serve socket communication */
  while (1)
  {
    struct net_sockaddr_in s_addr_in_remote_host = {0};
    uint32_t s_addr_in_remote_host_len = sizeof(s_addr_in_remote_host);

    /* === AUTO-SAMPLE FOR AI (runs every loop iteration) === */
    ai_auto_sample();

    /* Accept net socket requests (with timeout to allow auto-sampling) */
    const int32_t newconn = net_accept(sock, (struct net_sockaddr *)&s_addr_in_remote_host,
                                       (uint32_t *)&s_addr_in_remote_host_len);

    /* Check if a valid new connection is requested */
    if (newconn > 0)
    {
      net_ip_addr_t ip_addr_in_remote_host = {0};
      ip_addr_in_remote_host.addr = s_addr_in_remote_host.sin_addr.s_addr;

      printf("Request from %s:%" PRIu32 "\n",
             net_ntoa(&ip_addr_in_remote_host), (uint32_t)NET_NTOHS(s_addr_in_remote_host.sin_port));

      /* Treat net socket requests */
      if (http_treat_request(newconn) != WEBSERVER_OK)
      {
        printf("*** Fail : Invalid HTTP request !!!!\r\n");
        /* Don't return - continue serving */
      }
    }
    /* Timeout is OK - it allows auto-sampling to run */
  }
}

/**
  * @brief  Treat webserver HTTP request
  * @param  socket : connection socket
  * @retval Web Server status
  */
static WebServer_StatusTypeDef http_treat_request(int32_t socket)
{
    static unsigned char recv_buffer[HTTP_RECEIVE_BUFFER_SIZE];
    char http_sensor_value[32];
    int32_t recv_len = 0;

    /* Clear receive buffer */
    memset(recv_buffer, 0, sizeof(recv_buffer));

    printf("Waiting for HTTP request data...\n");

    /* Poll WiFi module */
    for (int i = 0; i < 30; i++)
    {
        net_if_yield(Netif, 100);
        HAL_Delay(100);
    }

    recv_len = net_recv(socket, recv_buffer, HTTP_RECEIVE_BUFFER_SIZE, 0);
    printf("net_recv returned: %ld\n", recv_len);

    if (recv_len <= 0)
    {
        printf("ERROR: net_recv failed or no data\n");
        net_closesocket(socket);
        return HTTP_ERROR;
    }

    printf("SUCCESS: Received %ld bytes\n", recv_len);

    /* Make sure it's a GET request */
    if (strncmp((char *)recv_buffer, "GET ", 4) != 0)
    {
        printf("ERROR: Not a valid GET request\n");
        net_closesocket(socket);
        return HTTP_ERROR;
    }

    char *req_path = (char *)&recv_buffer[4];
    char *end_path = strstr(req_path, " ");
    if (!end_path)
        end_path = req_path + strlen(req_path);
    *end_path = '\0';

    printf("Request path: %s\n", req_path);

    /* === ROUTE MATCHING === */

    /* Main page */
    if (strcmp(req_path, "/") == 0 || strcmp(req_path, "/index.html") == 0)
    {
        printf(">>> Sending HTML page\n");
        http_send_response(socket, HTTP_HEADER_HTML_ID, http_header_response,
                          html_buff, html_buff_size);
    }
    /* Static assets */
    else if (strcmp(req_path, "/static/css/chunk.css") == 0)
    {
        printf(">>> Sending CSS chunk\n");
        http_send_response(socket, HTTP_HEADER_CSS_ID, http_header_response,
                          css_shunk_buff, css_shunk_buff_size);
    }
    else if (strcmp(req_path, "/static/css/main.css") == 0)
    {
        printf(">>> Sending main CSS\n");
        http_send_response(socket, HTTP_HEADER_CSS_ID, http_header_response,
                          css_main_buff, css_main_buff_size);
    }
    else if (strcmp(req_path, "/static/js/chunk.js") == 0)
    {
        printf(">>> Sending JS chunk\n");
        http_send_response(socket, HTTP_HEADER_JS_ID, http_header_response,
                          js_shunk_buff, js_shunk_buff_size);
    }
    else if (strcmp(req_path, "/static/js/main.js") == 0)
    {
        printf(">>> Sending main JS\n");
        http_send_response(socket, HTTP_HEADER_JS_ID, http_header_response,
                          js_main_buff, js_main_buff_size);
    }
    else if (strcmp(req_path, "/favicon.png") == 0)
    {
        printf(">>> Sending favicon\n");
        http_send_response(socket, HTTP_HEADER_FAVICON_ID, http_header_response,
                          favicon_buff, favicon_buff_size);
    }
    else if (strcmp(req_path, "/static/media/FLSTM32U5.jpg") == 0)
    {
        printf(">>> Sending image\n");
        http_send_response(socket, HTTP_HEADER_IMAGE_ID, http_header_response,
                          image_buff, image_buff_size);
    }
    else if (strcmp(req_path, "/static/media/fa-solid-900.woff2") == 0)
    {
        printf(">>> Sending font\n");
        http_send_response(socket, HTTP_HEADER_FONT_ID, http_header_response,
                          font_buff, font_buff_size);
    }
    /* === SENSOR ENDPOINTS === */
    else if (strcmp(req_path, "/Read_Temperature") == 0 ||
             strcmp(req_path, "/Read_Humidity") == 0 ||
             strcmp(req_path, "/Read_Pressure") == 0)
    {
        float temperature = 0, humidity = 0, pressure = 0;

        /* Read all sensors */
        webserver_temp_sensor_read(&temperature);
        webserver_humid_sensor_read(&humidity);
        webserver_press_sensor_read(&pressure);

        /* Prepare HTTP response for requested sensor */
        if (strcmp(req_path, "/Read_Temperature") == 0)
        {
            sprintf(http_sensor_value, "%.2f", temperature);
            printf(">>> Sending Temperature: %s°C\n", http_sensor_value);
        }
        else if (strcmp(req_path, "/Read_Humidity") == 0)
        {
            sprintf(http_sensor_value, "%.2f", humidity);
            printf(">>> Sending Humidity: %s%%\n", http_sensor_value);
        }
        else
        {
            sprintf(http_sensor_value, "%.2f", pressure);
            printf(">>> Sending Pressure: %s hPa\n", http_sensor_value);
        }

        /* Send HTTP response */
        http_send_response(socket, HTTP_HEADER_SENSOR_ID, http_header_response,
                          http_sensor_value, strlen(http_sensor_value));

        /* Store & Forward */
        SF_StoreData(temperature, humidity, pressure);
        int sent = SF_SendStoredData();
        if (sent > 0) printf("ThingSpeak: uploaded %d samples\n", sent);
    }
    /* === WEATHER STATION DASHBOARD === */
    else if (strcmp(req_path, "/weather") == 0 || strncmp(req_path, "/ai", 3) == 0)
    {
        printf(">>> Sending Weather Station Dashboard\n");
        http_send_response(socket, HTTP_HEADER_HTML_ID, http_header_response,
                          weather_station_buff, weather_station_buff_size);
    }
    /* === POWER MONITOR DASHBOARD === */
    else if (strcmp(req_path, "/power") == 0)
    {
        printf(">>> Sending Power Monitor Dashboard\n");
        http_send_response(socket, HTTP_HEADER_HTML_ID, http_header_response,
                          power_dashboard_buff, power_dashboard_buff_size);
    }
    /* === POWER API === */
    else if (strcmp(req_path, "/api/power") == 0)
    {
        printf(">>> Power API requested\n");

        char json_response[512];
        PowerStats_t *stats = LowPower_GetStats();
        char uptime_str[32];
        LowPower_GetUptimeString(uptime_str);

        sprintf(json_response,
            "{"
            "\"mode\":\"%s\","
            "\"enabled\":%s,"
            "\"active_ua\":%lu,"
            "\"sleep_ua\":%lu,"
            "\"avg_ua\":%.1f,"
            "\"duty_cycle\":%.2f,"
            "\"wake_count\":%lu,"
            "\"sleep_count\":%lu,"
            "\"uptime\":\"%s\","
            "\"uptime_sec\":%lu,"
            "\"run_time_ms\":%lu,"
            "\"sleep_time_ms\":%lu,"
            "\"life_cr2032\":%.1f,"
            "\"life_aa\":%.1f,"
            "\"life_18650\":%.1f"
            "}",
            LowPower_GetModeString(stats->current_mode),
            stats->low_power_enabled ? "true" : "false",
            stats->active_current_ua,
            stats->sleep_current_ua,
            stats->average_current_ua,
            stats->duty_cycle_percent,
            stats->wake_count,
            stats->sleep_count,
            uptime_str,
            stats->uptime_seconds,
            stats->total_run_time_ms,
            stats->total_sleep_time_ms,
            stats->battery_life_hours_cr2032,
            stats->battery_life_hours_aa,
            stats->battery_life_hours_18650
        );

        http_send_response(socket, HTTP_HEADER_JSON_ID, http_header_response,
                          json_response, strlen(json_response));
    }
    /* === POWER TOGGLE API === */
    else if (strcmp(req_path, "/api/power/toggle") == 0)
    {
        printf(">>> Power Toggle API requested\n");

        /* Toggle low power mode */
        uint8_t current = LowPower_IsEnabled();
        LowPower_Enable(!current);

        char json_response[64];
        sprintf(json_response, "{\"enabled\":%s}",
                LowPower_IsEnabled() ? "true" : "false");

        http_send_response(socket, HTTP_HEADER_JSON_ID, http_header_response,
                          json_response, strlen(json_response));
    }
    /* === COMPREHENSIVE WEATHER API === */
    else if (strcmp(req_path, "/api/weather") == 0)
    {
        printf(">>> Weather API requested\n");

        char json_response[2048];
        AI_Status_t ai_status = AI_GetStatus();
        WeatherAnalysis_t *weather = Weather_GetAnalysis();

        if (ai_status == AI_STATUS_READY && weather->forecast_valid)
        {
            sprintf(json_response,
                "{"
                "\"status\":\"ready\","

                /* Current conditions */
                "\"temp\":%.2f,"
                "\"humidity\":%.2f,"
                "\"pressure\":%.2f,"
                "\"dew_point\":%.2f,"
                "\"heat_index\":%.2f,"
                "\"condition_now\":\"%s\","
                "\"icon_now\":\"%s\","

                /* Rain probability */
                "\"rain_now\":%d,"
                "\"rain_1h\":%d,"
                "\"rain_2h\":%d,"
                "\"rain_6h\":%d,"

                /* Temperature forecast */
                "\"temp_1h\":%.2f,"
                "\"temp_2h\":%.2f,"
                "\"temp_6h\":%.2f,"

                /* Humidity forecast */
                "\"hum_1h\":%.2f,"
                "\"hum_2h\":%.2f,"
                "\"hum_6h\":%.2f,"

                /* Pressure forecast */
                "\"press_1h\":%.2f,"
                "\"press_2h\":%.2f,"
                "\"press_6h\":%.2f,"

                /* Conditions forecast */
                "\"condition_1h\":\"%s\","
                "\"condition_2h\":\"%s\","
                "\"condition_6h\":\"%s\","
                "\"icon_1h\":\"%s\","
                "\"icon_2h\":\"%s\","
                "\"icon_6h\":\"%s\","

                /* Pressure trend */
                "\"pressure_change\":%.2f,"
                "\"pressure_trend\":%d,"
                "\"pressure_trend_str\":\"%s\","

                /* Comfort & alerts */
                "\"comfort\":%d,"
                "\"alert_code\":%d,"
                "\"alert_str\":\"%s\""
                "}",

                weather->temperature,
                weather->humidity,
                weather->pressure,
                weather->dew_point,
                weather->heat_index,
                Weather_GetConditionString(weather->condition_now),
                Weather_GetConditionIcon(weather->condition_now),

                weather->rain_prob_now,
                weather->rain_prob_1h,
                weather->rain_prob_2h,
                weather->rain_prob_6h,

                weather->temp_1h,
                weather->temp_2h,
                weather->temp_6h,

                weather->hum_1h,
                weather->hum_2h,
                weather->hum_6h,

                weather->press_1h,
                weather->press_2h,
                weather->press_6h,

                Weather_GetConditionString(weather->condition_1h),
                Weather_GetConditionString(weather->condition_2h),
                Weather_GetConditionString(weather->condition_6h),
                Weather_GetConditionIcon(weather->condition_1h),
                Weather_GetConditionIcon(weather->condition_2h),
                Weather_GetConditionIcon(weather->condition_6h),

                weather->pressure_change_3h,
                weather->pressure_trend,
                Weather_GetPressureTrendString(weather->pressure_trend),

                weather->comfort_level,
                weather->primary_alert,
                Weather_GetAlertString(weather->primary_alert)
            );
        }
        else
        {
            /* Still collecting data - but show current weather analysis */
            sprintf(json_response,
                "{"
                "\"status\":\"collecting\","
                "\"samples\":%d,"
                "\"required\":%d,"
                "\"seconds_remaining\":%d,"
                "\"temp\":%.2f,"
                "\"humidity\":%.2f,"
                "\"pressure\":%.2f,"
                "\"dew_point\":%.2f,"
                "\"heat_index\":%.2f,"
                "\"rain_now\":%d,"
                "\"condition_now\":\"%s\","
                "\"icon_now\":\"%s\","
                "\"pressure_change\":%.2f,"
                "\"pressure_trend\":%d,"
                "\"pressure_trend_str\":\"%s\","
                "\"comfort\":%d,"
                "\"alert_code\":%d,"
                "\"alert_str\":\"%s\""
                "}",
                AI_GetSamplesCollected(),
                AI_GetSamplesRequired(),
                AI_GetSecondsRemaining(),
                weather->temperature,
                weather->humidity,
                weather->pressure,
                weather->dew_point,
                weather->heat_index,
                weather->rain_prob_now,
                Weather_GetConditionString(weather->condition_now),
                Weather_GetConditionIcon(weather->condition_now),
                weather->pressure_change_3h,
                weather->pressure_trend,
                Weather_GetPressureTrendString(weather->pressure_trend),
                weather->comfort_level,
                weather->primary_alert,
                Weather_GetAlertString(weather->primary_alert)
            );
        }

        http_send_response(socket, HTTP_HEADER_JSON_ID, http_header_response,
                          json_response, strlen(json_response));
    }
    /* === RAIN PROBABILITY API === */
    else if (strcmp(req_path, "/api/rain") == 0)
    {
        printf(">>> Rain Probability API requested\n");

        char json_response[256];
        WeatherAnalysis_t *weather = Weather_GetAnalysis();

        sprintf(json_response,
            "{"
            "\"now\":%d,"
            "\"1h\":%d,"
            "\"2h\":%d,"
            "\"6h\":%d,"
            "\"pressure_trend\":\"%.1f hPa/3h\","
            "\"condition\":\"%s\","
            "\"alert\":\"%s\""
            "}",
            weather->rain_prob_now,
            weather->rain_prob_1h,
            weather->rain_prob_2h,
            weather->rain_prob_6h,
            weather->pressure_change_3h,
            Weather_GetConditionString(weather->condition_now),
            Weather_GetAlertString(weather->primary_alert)
        );

        http_send_response(socket, HTTP_HEADER_JSON_ID, http_header_response,
                          json_response, strlen(json_response));
    }
    /* === ALERTS API === */
    else if (strcmp(req_path, "/api/alerts") == 0)
    {
        printf(">>> Alerts API requested\n");

        char json_response[512];
        WeatherAnalysis_t *weather = Weather_GetAnalysis();

        sprintf(json_response,
            "{"
            "\"primary_alert\":%d,"
            "\"alert_string\":\"%s\","
            "\"alert_count\":%d,"
            "\"rain_likely\":%s,"
            "\"storm_warning\":%s,"
            "\"frost_warning\":%s,"
            "\"heat_warning\":%s,"
            "\"pressure_trend\":\"%s\","
            "\"comfort\":%d"
            "}",
            weather->primary_alert,
            Weather_GetAlertString(weather->primary_alert),
            weather->alert_count,
            weather->rain_prob_1h >= 60 ? "true" : "false",
            weather->pressure_change_3h < -6.0f ? "true" : "false",
            weather->temperature <= 3.0f ? "true" : "false",
            weather->heat_index >= 38.0f ? "true" : "false",
            Weather_GetPressureTrendString(weather->pressure_trend),
            weather->comfort_level
        );

        http_send_response(socket, HTTP_HEADER_JSON_ID, http_header_response,
                          json_response, strlen(json_response));
    }
    /* === AI FORECAST API (original, kept for compatibility) === */
    else if (strcmp(req_path, "/api/forecast") == 0)
    {
        printf(">>> Weather Forecast API requested\n");

        char json_response[1024];
        AI_Status_t status = AI_GetStatus();

        if (!ai_initialized || status == AI_STATUS_ERROR)
        {
            sprintf(json_response,
                "{\"status\":\"error\",\"message\":\"AI not initialized\"}");
        }
        else if (status == AI_STATUS_COLLECTING || status == AI_STATUS_INIT)
        {
            sprintf(json_response,
                "{\"status\":\"collecting\","
                "\"samples_collected\":%d,"
                "\"samples_required\":%d,"
                "\"seconds_remaining\":%d}",
                AI_GetSamplesCollected(),
                AI_GetSamplesRequired(),
                AI_GetSecondsRemaining());
        }
        else if (status == AI_STATUS_READY)
        {
            /* Run inference */
            AI_RunInference();
            WeatherForecast_t *forecast = AI_GetForecast();

            sprintf(json_response,
                "{\"status\":\"ready\","
                "\"current\":{"
                    "\"temp\":%.2f,"
                    "\"hum\":%.2f,"
                    "\"press\":%.2f"
                "},"
                "\"forecast\":{"
                    "\"temp_1h\":%.2f,"
                    "\"temp_2h\":%.2f,"
                    "\"temp_6h\":%.2f,"
                    "\"hum_1h\":%.2f,"
                    "\"hum_2h\":%.2f,"
                    "\"hum_6h\":%.2f,"
                    "\"press_1h\":%.2f,"
                    "\"press_2h\":%.2f,"
                    "\"press_6h\":%.2f"
                "},"
                "\"trends\":{"
                    "\"temp\":%d,"
                    "\"hum\":%d,"
                    "\"press\":%d"
                "}}",
                forecast->current_temp,
                forecast->current_hum,
                forecast->current_press,
                forecast->temp_1h,
                forecast->temp_2h,
                forecast->temp_6h,
                forecast->hum_1h,
                forecast->hum_2h,
                forecast->hum_6h,
                forecast->press_1h,
                forecast->press_2h,
                forecast->press_6h,
                (int)forecast->temp_trend,
                (int)forecast->hum_trend,
                (int)forecast->press_trend);
        }
        else
        {
            sprintf(json_response,
                "{\"status\":\"unknown\"}");
        }

        http_send_response(socket, HTTP_HEADER_JSON_ID, http_header_response,
                          json_response, strlen(json_response));
    }
    /* === LEGACY API (backwards compatibility) === */
    else if (strcmp(req_path, "/api/predict") == 0)
    {
        printf(">>> Legacy Predict API - redirecting to forecast\n");

        char json_response[512];
        AI_Status_t status = AI_GetStatus();

        if (!ai_initialized || status == AI_STATUS_ERROR)
        {
            sprintf(json_response,
                "{\"error\":\"AI not initialized\",\"status\":\"disabled\"}");
        }
        else if (status == AI_STATUS_COLLECTING || status == AI_STATUS_INIT)
        {
            sprintf(json_response,
                "{\"error\":\"Collecting samples\","
                "\"samples_collected\":%d,"
                "\"samples_required\":%d,"
                "\"seconds_remaining\":%d,"
                "\"status\":\"collecting\"}",
                AI_GetSamplesCollected(),
                AI_GetSamplesRequired(),
                AI_GetSecondsRemaining());
        }
        else
        {
            AI_RunInference();
            WeatherForecast_t *forecast = AI_GetForecast();

            sprintf(json_response,
                "{\"current_temp\":%.2f,"
                "\"predicted_temp\":%.2f,"
                "\"difference\":%.2f,"
                "\"samples\":%d,"
                "\"status\":\"ready\"}",
                forecast->current_temp,
                forecast->temp_1h,
                forecast->temp_1h - forecast->current_temp,
                AI_GetSamplesCollected());
        }

        http_send_response(socket, HTTP_HEADER_JSON_ID, http_header_response,
                          json_response, strlen(json_response));
    }
    /* === DATASET EXPORT === */
    else if (strcmp(req_path, "/export_dataset") == 0)
    {
        printf(">>> Dataset export requested\n");

        char export_msg[1024];
        int count = Dataset_GetCount();

        sprintf(export_msg,
            "<html><head><title>Dataset Export</title></head>"
            "<body style='font-family:Arial; padding:50px;'>"
            "<h1>Dataset Exported</h1>"
            "<p>CSV data sent to serial terminal.</p>"
            "<p><strong>Total samples:</strong> %d</p>"
            "<p><a href='/'>Back to Home</a></p>"
            "</body></html>",
            count);

        http_send_response(socket, HTTP_HEADER_HTML_ID, http_header_response,
                          export_msg, strlen(export_msg));

        Dataset_ExportCSV();
    }
    /* === DATASET STATUS API === */
    else if (strcmp(req_path, "/api/dataset_status") == 0)
    {
        printf(">>> Dataset status API requested\n");

        char json_response[256];
        int count = Dataset_GetCount();
        sprintf(json_response,
                "{\"samples\":%d,\"status\":\"%s\",\"storage\":\"RAM\"}",
                count, count > 0 ? "active" : "empty");

        http_send_response(socket, HTTP_HEADER_JSON_ID, http_header_response,
                          json_response, strlen(json_response));
    }
    /* === 404 NOT FOUND === */
    else
    {
        printf("WARNING: 404 - %s\n", req_path);
        const char *not_found =
            "<html><body><h1>404 Not Found</h1>"
            "<p><a href='/'>Back to Home</a></p>"
            "<p><a href='/weather'>Weather Station</a></p>"
            "<p><a href='/power'>Power Monitor</a></p></body></html>";
        http_send_response(socket, HTTP_HEADER_HTML_ID, http_header_response,
                          not_found, strlen(not_found));
    }

    net_closesocket(socket);
    printf("=== Request completed ===\n\n");
    return WEBSERVER_OK;
}

/**
  * @brief  Send HTTP response with headers and body
  */
static WebServer_StatusTypeDef http_send_response(int32_t socket,
                                                  uint32_t headers_id,
                                                  char *headers_buff,
                                                  const char *body_buff,
                                                  uint32_t data_size)
{
  if (http_send_headers_response(socket, headers_id, headers_buff, data_size) != WEBSERVER_OK)
  {
    return HTTP_ERROR;
  }

  if (http_send(socket, (const char *)body_buff, data_size) != WEBSERVER_OK)
  {
    return HTTP_ERROR;
  }

  return WEBSERVER_OK;
}

/**
  * @brief  Send HTTP headers
  */
static WebServer_StatusTypeDef http_send_headers_response(int32_t socket,
                                                          uint32_t headers_id,
                                                          char *headers_buff,
                                                          uint32_t data_size)
{
  switch(headers_id)
  {
  case HTTP_HEADER_HTML_ID:
    if (webserver_http_encode_html_response(headers_buff, data_size) != WEBSERVER_OK)
      return HTTP_ERROR;
    break;

  case HTTP_HEADER_CSS_ID:
    if (webserver_http_encode_css_response(headers_buff, data_size) != WEBSERVER_OK)
      return HTTP_ERROR;
    break;

  case HTTP_HEADER_JS_ID:
    if (webserver_http_encode_js_response(headers_buff, data_size) != WEBSERVER_OK)
      return HTTP_ERROR;
    break;

  case HTTP_HEADER_FAVICON_ID:
    if (webserver_http_encode_favicon_response(headers_buff, data_size) != WEBSERVER_OK)
      return HTTP_ERROR;
    break;

  case HTTP_HEADER_FONT_ID:
    if (webserver_http_encode_woff2_response(headers_buff, data_size) != WEBSERVER_OK)
      return HTTP_ERROR;
    break;

  case HTTP_HEADER_SENSOR_ID:
    if (webserver_http_encode_sensor_response(headers_buff, data_size) != WEBSERVER_OK)
      return HTTP_ERROR;
    break;

  case HTTP_HEADER_JSON_ID:
    if (webserver_http_encode_json_response(headers_buff, data_size) != WEBSERVER_OK)
      return HTTP_ERROR;
    break;

  case HTTP_HEADER_IMAGE_ID:
    if (webserver_http_encode_image_response(headers_buff, data_size) != WEBSERVER_OK)
      return HTTP_ERROR;
    break;

  default:
    return HTTP_ERROR;
  }

  if (http_send(socket, (const char *)headers_buff, strlen((char*)headers_buff)) != WEBSERVER_OK)
  {
    return HTTP_ERROR;
  }

  return WEBSERVER_OK;
}

/**
  * @brief  Send data over socket in chunks
  */
static WebServer_StatusTypeDef http_send(int32_t socket,
                                         const char *frame,
                                         uint32_t frame_size)
{
  uint32_t data_size = frame_size;
  uint32_t data_idx  = 0U;

  while (data_size > 0U)
  {
    if (data_size >= MAX_SOCKET_DATASIZE)
    {
      if (net_send(socket, (uint8_t*)&frame[data_idx], MAX_SOCKET_DATASIZE, 0) <= 0U)
      {
        return HTTP_ERROR;
      }

      data_size -= MAX_SOCKET_DATASIZE;
      data_idx += MAX_SOCKET_DATASIZE;
    }
    else
    {
      if (net_send(socket, (uint8_t*)&frame[data_idx], data_size, 0) <= 0U)
      {
        return HTTP_ERROR;
      }

      data_size = 0U;
    }
  }

  return WEBSERVER_OK;
}
