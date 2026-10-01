#ifndef stderr
int stderr = 0; // Hack: fix linker error
#endif

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_mouse.h>

/*******************************************************************************
 Functions called by JNI
 *******************************************************************************/
#include <jni.h>

/* Called before  to initialize JNI bindings  */

extern int argcData;
extern const char** argvData;
void releaseArgv();

extern "C" int Java_org_libsdl_app_SDLActivity_getMouseX(JNIEnv* env, jclass cls, jobject obj)
{
    float ret = 0;
    SDL_GetMouseState(&ret, nullptr);
    return static_cast<int>(ret);
}

extern "C" int Java_org_libsdl_app_SDLActivity_getMouseY(JNIEnv* env, jclass cls, jobject obj)
{
    float ret = 0;
    SDL_GetMouseState(nullptr, &ret);
    return static_cast<int>(ret);
}

extern "C" int Java_org_libsdl_app_SDLActivity_isMouseShown(JNIEnv* env, jclass cls, jobject obj)
{
    return SDL_CursorVisible();
}

// SDL's own internals, which the Java side reaches through these: their SDL3 signatures.
extern SDL_Window* Android_Window;
extern "C" void SDL_SendMouseMotion(
    Uint64 timestamp, SDL_Window* window, SDL_MouseID mouseID, bool relative, float x, float y);
extern "C" void Java_org_libsdl_app_SDLActivity_sendRelativeMouseMotion(JNIEnv* env, jclass cls, int x, int y)
{
    SDL_SendMouseMotion(0, Android_Window, 0, true, static_cast<float>(x), static_cast<float>(y));
}

extern "C" void SDL_SendMouseButton(Uint64 timestamp, SDL_Window* window, SDL_MouseID mouseID, Uint8 button, bool down);
extern "C" void Java_org_libsdl_app_SDLActivity_sendMouseButton(JNIEnv* env, jclass cls, int state, int button)
{
    SDL_SendMouseButton(0, Android_Window, 0, static_cast<Uint8>(button), state != 0);
}

extern "C" int Java_org_libsdl_app_SDLActivity_nativeInit(JNIEnv* env, jclass cls, jobject obj)
{
    setenv("OPENMW_DECOMPRESS_TEXTURES", "1", 1);

    // On Android, we use a virtual controller with guid="Virtual"
    SDL_AddGamepadMapping(
        "5669727475616c000000000000000000,Virtual,a:b0,b:b1,back:b15,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,"
        "guide:b16,leftshoulder:b6,leftstick:b13,lefttrigger:a5,leftx:a0,lefty:a1,rightshoulder:b7,rightstick:b14,"
        "righttrigger:a4,rightx:a2,righty:a3,start:b11,x:b3,y:b4");

    return 0;
}
