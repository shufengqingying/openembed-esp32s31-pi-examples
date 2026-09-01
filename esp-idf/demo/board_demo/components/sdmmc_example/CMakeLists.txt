idf_component_register(SRCS "sdmmc_example.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fatfs sd_card
                       WHOLE_ARCHIVE)