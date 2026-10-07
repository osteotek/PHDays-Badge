idf_component_register(SRCS "main.c" "display.c" "screens.c" "settings.c" "clock_time.c" "weather.c" "timer.c"
                            "info_api.c" "rest_server.c" "buttons.c" "buzzer.c" "adc_utils.c"
                            "wifi.c" "home_wifi.c" "ota.c" "ota_upload.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "ui/index.html.gz" "ui/wifi.html")
