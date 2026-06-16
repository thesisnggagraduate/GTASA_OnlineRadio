LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc

ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_MODULE := OnlineRadio
else
    LOCAL_MODULE := OnlineRadio64
endif

LOCAL_SRC_FILES := main.cpp \
                   mod/logger.cpp \
                   mod/config.cpp \
                   $(EXTERNAL_IMGUI_PATH)/imgui.cpp \
                   $(EXTERNAL_IMGUI_PATH)/imgui_draw.cpp \
                   $(EXTERNAL_IMGUI_PATH)/imgui_tables.cpp \
                   $(EXTERNAL_IMGUI_PATH)/imgui_widgets.cpp \
                   $(EXTERNAL_IMGUI_PATH)/backends/imgui_impl_renderware.cpp

LOCAL_CFLAGS += -O2 -mfloat-abi=softfp -DNDEBUG -std=c++17

# Include directories para sa ImGui
LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/aml-psdk-gtasa \
    $(LOCAL_PATH)/aml-psdk-gtasa/aml-psdk/game_sa \
    $(LOCAL_PATH)/AML_ImGui

LOCAL_LDLIBS += -llog

include $(BUILD_SHARED_LIBRARY)
