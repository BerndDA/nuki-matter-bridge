idf_component_register(
    SRCS
        "app_main.cpp"
        "arduino_matter_wraps.cpp"
        "matter_lock.cpp"
        "nuki_bridge.cpp"
        "nuki_sim.cpp"
        "web_ui.cpp"
        "wifi_setup.cpp"
    INCLUDE_DIRS "."
    EMBED_FILES "web/index.html"
    REQUIRES nuki_ble nvs_flash esp_wifi esp_netif esp_http_server esp_app_format driver)
