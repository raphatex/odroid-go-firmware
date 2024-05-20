idf_component_register(SRCS ./input.c ./main.c ./odroid_display.c ./odroid_sdcard.c)
target_compile_options(${COMPONENT_LIB} PRIVATE -DCOMPILEDATE="$(COMPILEDATE)" -DGITREV="$(GITREV)")
