idf_component_register(
    SRCS
        src/hapanel_mqtt.c
    INCLUDE_DIRS
        include
    REQUIRES
        mqtt
        runtime)
