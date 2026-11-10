# Preserved earlier firmware

`esp32s3_sensor_bringup/` preserves the previous small three-sensor firmware and its local build results. `esp32s3_idf_initial/` preserves the earlier ESP-IDF experiment. These directories are historical backups and are excluded from AAI15-MTK.code-workspace and the active CMake target. Their old build caches are tied to their former paths; use the active `../esp32s3_mtk/` project for current builds.
