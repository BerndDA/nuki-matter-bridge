idf_component_register(
    SRCS
        "app_main.cpp"
        "arduino_matter_wraps.cpp"
        "matter_lock.cpp"
        "nuki_bridge.cpp"
        "nuki_sim.cpp"
        "wifi_setup.cpp"
    INCLUDE_DIRS "."
    REQUIRES nuki_ble nvs_flash esp_wifi driver)
