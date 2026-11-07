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
    REQUIRES nuki_ble nvs_flash esp_wifi esp_netif esp_http_server esp_app_format app_update driver)

# Web page: gzip at build time and embed the .gz; the server sends it with
# "Content-Encoding: gzip" (~10 KB -> ~3 KB of flash). mtime=0 keeps the output reproducible.
idf_build_get_property(python PYTHON)
set(index_src "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html")
set(index_gz "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")
add_custom_command(
    OUTPUT "${index_gz}"
    COMMAND "${python}" -c "import gzip,sys; open(sys.argv[2],'wb').write(gzip.compress(open(sys.argv[1],'rb').read(),9,mtime=0))" "${index_src}" "${index_gz}"
    DEPENDS "${index_src}"
    COMMENT "Compressing web/index.html"
    VERBATIM)
add_custom_target(web_index_gz DEPENDS "${index_gz}")
target_add_binary_data(${COMPONENT_LIB} "${index_gz}" BINARY RENAME_TO index_html_gz DEPENDS "${index_gz}")
add_dependencies(${COMPONENT_LIB} web_index_gz)
