idf_component_register(SRCS "mqtt_client_mod.c"
                      INCLUDE_DIRS "include"
                      REQUIRES esp_event esp_system mqtt)
