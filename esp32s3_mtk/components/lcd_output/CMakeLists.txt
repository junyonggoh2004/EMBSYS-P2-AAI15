idf_component_register(
    SRCS "lcd_output.c" "lcd_mock_screen.c" "lcd_st7789.c" "lcd_transport_mtk.c" "lcd_font_5x7.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../libraries/EdgeCore/src"
)

# The transport calls the existing hal_* functions implemented by edge_mtk.
# Keep this component independent of edge_mtk so ApplicationTask can depend
# on lcd_output later without introducing a component dependency cycle.
