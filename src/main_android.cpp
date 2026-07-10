//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2016-2017 MinkowskiKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#ifdef MOBILE_STK

#include "config/user_config.hpp"
#include "graphics/irr_driver.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"

#ifdef ANDROID
#include "SDL_cpuinfo.h"
#include "SDL_stdinc.h"
#include "SDL_system.h"
#include <cmath>
#include <jni.h>
std::string g_android_main_user_agent;

extern int android_main(int argc, char *argv[]);

extern "C" JNIEXPORT void JNICALL debugMsg(JNIEnv* env, jclass cls, jstring msg);
extern "C" JNIEXPORT void JNICALL handlePadding(JNIEnv* env, jclass cls, jboolean val);
extern "C" JNIEXPORT void JNICALL addDNSSrvRecords(JNIEnv* env, jclass cls, jstring name, jint weight);
extern "C" JNIEXPORT void JNICALL pauseRenderingJNI(JNIEnv* env, jclass cls);

extern "C" JNIEXPORT void JNICALL editText2STKEditbox(JNIEnv* env, jclass cls, jint widget_id, jstring text, jint start, jint end, jint composing_start, jint composing_end);
extern "C" JNIEXPORT void JNICALL handleActionNext(JNIEnv* env, jclass cls, jint widget_id);
extern "C" JNIEXPORT void JNICALL handleLeftRight(JNIEnv* env, jclass cls, jboolean left, jint widget_id);

#if !defined(ANDROID_PACKAGE_CLASS_NAME)
    #error
#endif

void registering_natives()
{
    JNINativeMethod stkactivity_tab[] =
    {
        { "debugMsg",           "(Ljava/lang/String;)V", (void*)&debugMsg },
        { "handlePadding",      "(Z)V", (void*)&handlePadding },
        { "addDNSSrvRecords",   "(Ljava/lang/String;I)V", (void*)&addDNSSrvRecords },
        { "pauseRenderingJNI",   "()V", (void*)&pauseRenderingJNI }
    };
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    assert(env);
    const char* stkactivity_class = ANDROID_PACKAGE_CLASS_NAME "/SuperTuxKartActivity";
    jclass clazz = env->FindClass(stkactivity_class);
    if (clazz == NULL)
    {
        Log::error("MainAndroid", "Failed to find class %s.",
            stkactivity_class);
        return;
    }
    if (env->RegisterNatives(
        clazz, stkactivity_tab, (int)SDL_arraysize(stkactivity_tab)) < 0)
    {
        Log::error("MainAndroid", "Failed to register methods of %s.",
            stkactivity_class);
    }

    JNINativeMethod stkeditbox_tab[] =
    {
        { "editText2STKEditbox", "(ILjava/lang/String;IIII)V", (void*)&editText2STKEditbox },
        { "handleActionNext",    "(I)V", (void*)&handleActionNext },
        { "handleLeftRight",     "(ZI)V", (void*)&handleLeftRight }
    };
    const char* stkeditbox_class = ANDROID_PACKAGE_CLASS_NAME "/STKEditText";
    clazz = env->FindClass(stkeditbox_class);
    if (clazz == NULL)
    {
        Log::error("MainAndroid", "Failed to find class %s.",
            stkeditbox_class);
        return;
    }
    if (env->RegisterNatives(
        clazz, stkeditbox_tab, (int)SDL_arraysize(stkeditbox_tab)) < 0)
    {
        Log::error("MainAndroid", "Failed to register methods of %s.",
            stkeditbox_class);
    }
}

void override_default_params_for_mobile();
extern "C" int SDL_main(int argc, char *argv[])
{
    registering_natives();
    override_default_params_for_mobile();
    return android_main(argc, argv);
}
#endif

void override_default_params_for_mobile()
{
    // It has an effect only on the first run, when config file is created.
    // So that we can still modify these params in MK options and user's
    // choice will be then remembered.
    
    // Set smaller texture size to avoid high RAM usage
    UserConfigParams::m_max_texture_size = 256;
    UserConfigParams::m_high_definition_textures = false;
    
    // Enable advanced lighting only for android >= 8
#ifdef ANDROID
    UserConfigParams::m_dynamic_lights = (SDL_GetAndroidSDKVersion() >= 26);
    // Advanced lighting on tile-based mobile GPUs is bandwidth-heavy. These
    // are defaults only: loadConfig() runs afterwards and restores any saved
    // user choices from config.xml.
    UserConfigParams::m_shadows_resolution.setDefaultValue(1024);
    UserConfigParams::m_pcss.setDefaultValue(false);
#endif

    // Disable light scattering for better performance
    UserConfigParams::m_light_scatter = false;

    // Enable multitouch race GUI
    UserConfigParams::m_multitouch_draw_gui = true;

#ifdef ANDROID
    // For usage in StringUtils::getUserAgentString
    if (SDL_IsAndroidTV())
    {
        // For some android tv sdl returns a touch screen device even it doesn't
        // have
        UserConfigParams::m_multitouch_draw_gui = false;
        g_android_main_user_agent = " (AndroidTV)";
    }
    else if (SDL_IsChromebook())
        g_android_main_user_agent = " (ChromeOS)";
    else
        g_android_main_user_agent = " (Android)";

    // Get some info about display
    const int SCREENLAYOUT_SIZE_SMALL = 1;
    const int SCREENLAYOUT_SIZE_NORMAL = 2;
    const int SCREENLAYOUT_SIZE_LARGE = 3;
    const int SCREENLAYOUT_SIZE_XLARGE = 4;
    int32_t screen_size = 0;
    int ddpi = 0;
    int display_width = 0;
    int display_height = 0;
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    assert(env);
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (activity != NULL)
    {
        jclass clazz = env->GetObjectClass(activity);
        if (clazz != NULL)
        {
            jmethodID method_id = env->GetMethodID(clazz, "getScreenSize",
                "()I");
            if (method_id != NULL)
                screen_size = env->CallIntMethod(activity, method_id);
                
            jmethodID display_dpi_id = env->GetStaticMethodID(clazz,
                "getDisplayDPI", "()Landroid/util/DisplayMetrics;");
                
            if (display_dpi_id != NULL)
            {
                jobject display_dpi_obj = env->CallStaticObjectMethod(clazz,
                                                                display_dpi_id);
                jclass display_dpi_class = env->GetObjectClass(display_dpi_obj);
            
                jfieldID ddpi_field = env->GetFieldID(display_dpi_class,
                                                      "densityDpi", "I");
                ddpi = env->GetIntField(display_dpi_obj, ddpi_field);

                jfieldID width_field = env->GetFieldID(display_dpi_class,
                                                       "widthPixels", "I");
                jfieldID height_field = env->GetFieldID(display_dpi_class,
                                                        "heightPixels", "I");
                display_width = env->GetIntField(display_dpi_obj, width_field);
                display_height = env->GetIntField(display_dpi_obj, height_field);
            
                env->DeleteLocalRef(display_dpi_obj);
                env->DeleteLocalRef(display_dpi_class);
            }
            
            env->DeleteLocalRef(activity);
            env->DeleteLocalRef(clazz);
        }
    }

    // Set multitouch device scale depending on actual screen size
    switch (screen_size)
    {
    case SCREENLAYOUT_SIZE_SMALL:
    case SCREENLAYOUT_SIZE_NORMAL:
        UserConfigParams::m_multitouch_scale.setDefaultValue(1.3f);
        UserConfigParams::m_multitouch_sensitivity_x.setDefaultValue(0.1f);
        UserConfigParams::m_font_size = 5.0f;
        break;
    case SCREENLAYOUT_SIZE_LARGE:
        UserConfigParams::m_multitouch_scale.setDefaultValue(1.2f);
        UserConfigParams::m_multitouch_sensitivity_x.setDefaultValue(0.15f);
        UserConfigParams::m_font_size = 5.0f;
        break;
    case SCREENLAYOUT_SIZE_XLARGE:
        UserConfigParams::m_multitouch_scale.setDefaultValue(1.1f);
        UserConfigParams::m_multitouch_sensitivity_x.setDefaultValue(0.2f);
        UserConfigParams::m_font_size = 4.0f;
        break;
    default:
        break;
    }
    
    const int total_memory = (int)std::ceil(
        (float)(SDL_GetSystemRAM()) / 1024.0f);

    // Choose the first-run render scale from actual fill-rate demand instead
    // of treating RAM and Android version as a GPU-performance proxy.  The
    // advanced deferred path targets about 1.5 MP; the simpler path can afford
    // more. Low-memory devices use a smaller budget to reduce attachment and
    // post-processing pressure as a secondary constraint.
    float target_pixels = UserConfigParams::m_dynamic_lights ?
        1500000.0f : 2000000.0f;
    if (total_memory <= 4)
        target_pixels *= 0.75f;

    float render_scale = 0.85f;
    if (ddpi < 1)
    {
        Log::warn("MainAndroid", "Failed to get display DPI.");
        render_scale = 0.7f;
    }
    else if (ddpi > 500)
        render_scale = 0.7f;
    else if (ddpi > 400)
        render_scale = 0.75f;
    else if (ddpi > 300)
        render_scale = 0.8f;

    if (display_width > 0 && display_height > 0)
    {
        const float native_pixels =
            (float)display_width * (float)display_height;
        const float pixel_limited_scale =
            std::sqrt(target_pixels / native_pixels);
        if (pixel_limited_scale < render_scale)
            render_scale = pixel_limited_scale;
    }

    // Keep the value on an existing options-screen preset and within a range
    // that remains legible while avoiding native-resolution overload.
    if (render_scale < 0.5f)
        render_scale = 0.5f;
    else if (render_scale > 0.85f)
        render_scale = 0.85f;
    render_scale = std::floor(render_scale * 20.0f) / 20.0f;
    UserConfigParams::m_scale_rtts_factor.setDefaultValue(render_scale);

    Log::info("MainAndroid", "Display: %ix%i at %i DPI, RAM: %i GB",
              display_width, display_height, ddpi, total_memory);
    Log::info("MainAndroid", "Default render scale: %f (target %.1f MP)",
              render_scale, target_pixels / 1000000.0f);
#endif

    // Enable screen keyboard
    UserConfigParams::m_screen_keyboard = 1;
    
    // It shouldn't matter, but MK is always run in fullscreen on android
    UserConfigParams::m_fullscreen = true;
    
    // Make sure that user can play every track even if there are installed
    // only few tracks and it's impossible to finish overworld challenges
    UserConfigParams::m_unlock_everything = 1;
    
    // Create default user istead of showing login screen to make life easier
    UserConfigParams::m_enforce_current_player = true;
}

#ifdef IOS_STK
void getConfigForDevice(const char* dev)
{
    // Check browser.geekbench.com/ios-benchmarks metal benchmark
    // https://gist.github.com/adamawolf/3048717 for device name mapping
    std::string device = dev;
    if (device.find("iPhone") != std::string::npos)
    {
        // Normal configuration default
        UserConfigParams::m_multitouch_scale.setDefaultValue(1.3f);
        UserConfigParams::m_multitouch_sensitivity_x.setDefaultValue(0.1f);
        UserConfigParams::m_font_size = 5.0f;
        device.erase(0, 6);
        auto versions = StringUtils::splitToUInt(device, ',');
        if (versions.size() == 2)
        {
            // A9 GPU
            if (versions[0] >= 8)
            {
                UserConfigParams::m_dynamic_lights = true;
                UserConfigParams::m_high_definition_textures = 1;
            }
            if (versions[0] < 7 || // iPhone 5s
                (versions[0] == 7 && versions[1] == 2) || // iPhone 6
                (versions[0] == 8 && versions[1] == 1) || // iPhone 6S
                (versions[0] == 8 && versions[1] == 4) || // iPhone SE
                (versions[0] == 9 && versions[1] == 1) || // iPhone 7
                (versions[0] == 9 && versions[1] == 3) || // iPhone 7
                (versions[0] == 10 && versions[1] == 1) || // iPhone 8
                (versions[0] == 10 && versions[1] == 4) // iPhone 8
                )
            {
                // Those phones have small screen
                UserConfigParams::m_multitouch_scale.setDefaultValue(1.45f);
            }
        }
    }
    else if (device.find("iPad") != std::string::npos)
    {
        // Normal configuration default
        UserConfigParams::m_multitouch_scale.setDefaultValue(0.95f);
        UserConfigParams::m_multitouch_sensitivity_x.setDefaultValue(0.2f);
        UserConfigParams::m_font_size = 3.0f;
        device.erase(0, 4);
        auto versions = StringUtils::splitToUInt(device, ',');
        if (versions.size() == 2)
        {
            if (versions[0] >= 7)
            {
                UserConfigParams::m_dynamic_lights = true;
                UserConfigParams::m_high_definition_textures = 1;
            }
        }
    }
    else if (device.find("iPod") != std::string::npos)
    {
        // All iPod touch has small screen
        UserConfigParams::m_font_size = 5.0f;
        UserConfigParams::m_multitouch_scale.setDefaultValue(1.45f);
        UserConfigParams::m_multitouch_sensitivity_x.setDefaultValue(0.1f);
        device.erase(0, 4);
        auto versions = StringUtils::splitToUInt(device, ',');
        if (versions.size() == 2)
        {
            // iPod Touch 7th Generation (A10)
            if (versions[0] >= 9)
            {
                UserConfigParams::m_dynamic_lights = true;
                UserConfigParams::m_high_definition_textures = 1;
            }
        }
    }
    // TODO remove when vulkan is used as it uses less power than gles3
    UserConfigParams::m_max_fps = 30;
}

#endif

#endif
