include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc

ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
LOCAL_MODULE := OnlineRadio
else
LOCAL_MODULE := OnlineRadio64
endif

# Listahan ng mga source files, kabilang ang mod at ImGui
LOCAL_SRC_FILES := main.cpp \
                   mod/logger.cpp \
                   mod/config.cpp \
                   imgui/imgui.cpp \
                   imgui/imgui_draw.cpp \
                   imgui/imgui_tables.cpp \
                   imgui/imgui_widgets.cpp \
                   imgui/imgui_impl_renderware.cpp

# Mga include directories
LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/aml-psdk-gtasa \
    $(LOCAL_PATH)/aml-psdk-gtasa/aml-psdk/game_sa \
    $(LOCAL_PATH)/imgui

# Compilation flags
LOCAL_CFLAGS += -O2 -mfloat-abi=softfp -DNDEBUG -std=c++17

# Linker libraries
LOCAL_LDLIBS += -llog

include $(BUILD_SHARED_LIBRARY)
