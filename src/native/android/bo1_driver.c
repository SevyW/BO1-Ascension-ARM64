// bo1_driver.c - Android: the Vulkan driver of the Vulkan renderer, the system's or a custom one (Mesa Turnip).
//
// files/driver.txt names a folder files/driver/<name>/ (files = the app's external files directory) holding a driver
// in the AdrenoTools package format: meta.json and the driver's library (e.g. vulkan.purple.so or
// libvulkan_freedreno.so; meta.json's libraryName says which). Such a driver exports only the HAL module (HMI), not
// vkGetInstanceProcAddr, so libadrenotools (libadrenotools.so and its hook libraries, next to libmain.so) opens a
// private copy of the system's Vulkan loader whose driver is the custom one. That loader's vkGetInstanceProcAddr goes
// to DXVK as BO1_VK_GET_INSTANCE_PROC_ADDR (hex; tools/native/build-dxvk.sh, BO1_LOADER).
// The library is copied to the app's internal storage first: the external files directory cannot hold executable code.
// Without driver.txt, or when the custom driver cannot be loaded or has no device, the system driver stays.
// Either way the log (bo1.log) says which driver is in use, with its name and version.
#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include <SDL2/SDL_system.h>

#define ADRENOTOOLS_DRIVER_CUSTOM (1 << 0)   // libadrenotools: include/adrenotools/driver.h
typedef void *(*OpenLibvulkan)(int dlopenFlags, int featureFlags, const char *tmpLibDir, const char *hookLibDir,
    const char *customDriverDir, const char *customDriverName, const char *fileRedirectDir, void **userMappingHandle);

static void Say(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, "bo1", "%s", line);
    fprintf(stderr, "vulkan driver: %s\n", line);
    fflush(stderr);
}

// a whole small file as a string (NULL when missing)
static char *ReadText(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    char *text = calloc(1, 65536);
    if (text)
        fread(text, 1, 65535, file);
    fclose(file);
    return text;
}

// the string value of "key" in a flat JSON object (meta.json); "" when absent
static void JsonString(const char *json, const char *key, char *out, size_t size)
{
    char quoted[64];
    snprintf(quoted, sizeof(quoted), "\"%s\"", key);
    out[0] = 0;
    const char *at = json ? strstr(json, quoted) : NULL;
    if (!at || !(at = strchr(at + strlen(quoted), ':')) || !(at = strchr(at, '"')))
        return;
    size_t n = 0;
    for (++at; *at && *at != '"' && n + 1 < size; ++at)
        out[n++] = *at;
    out[n] = 0;
}

static int CopyFile(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
    int ok = in && out;
    static char buffer[1 << 16];
    for (size_t n; ok && (n = fread(buffer, 1, sizeof(buffer), in)) > 0;)
        ok = fwrite(buffer, 1, n, out) == n;
    if (in)
        fclose(in);
    if (out && fclose(out) != 0)
        ok = 0;
    return ok;
}

// the first device of the driver behind getProc, as "<device>, <driver name> <driver info>, Vulkan x.y.z"; 0 without one
static int Describe(PFN_vkGetInstanceProcAddr getProc, char *out, size_t size)
{
    PFN_vkCreateInstance createInstance = (PFN_vkCreateInstance)getProc(NULL, "vkCreateInstance");
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (!createInstance || createInstance(&info, NULL, &instance) != VK_SUCCESS)
        return 0;
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)getProc(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties2 properties2 = (PFN_vkGetPhysicalDeviceProperties2)getProc(instance, "vkGetPhysicalDeviceProperties2");
    PFN_vkDestroyInstance destroyInstance = (PFN_vkDestroyInstance)getProc(instance, "vkDestroyInstance");
    uint32_t count = 1;
    VkPhysicalDevice device = VK_NULL_HANDLE;
    int found = enumerate && properties2 && enumerate(instance, &count, &device) >= 0 && count > 0 && device;
    if (found)
    {
        VkPhysicalDeviceDriverProperties driver = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver };
        properties2(device, &properties);
        const VkPhysicalDeviceProperties *p = &properties.properties;
        snprintf(out, size, "%s, %s %s (driver version %u.%u.%u), Vulkan %u.%u.%u", p->deviceName, driver.driverName,
            driver.driverInfo, VK_API_VERSION_MAJOR(p->driverVersion), VK_API_VERSION_MINOR(p->driverVersion),
            VK_API_VERSION_PATCH(p->driverVersion), VK_API_VERSION_MAJOR(p->apiVersion),
            VK_API_VERSION_MINOR(p->apiVersion), VK_API_VERSION_PATCH(p->apiVersion));
    }
    if (destroyInstance)
        destroyInstance(instance, NULL);
    return found;
}

static void UseSystemDriver(const char *why)
{
    unsetenv("BO1_VK_GET_INSTANCE_PROC_ADDR");
    char description[512] = "no device";
    void *vulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    PFN_vkGetInstanceProcAddr getProc = vulkan ? (PFN_vkGetInstanceProcAddr)dlsym(vulkan, "vkGetInstanceProcAddr") : NULL;
    if (getProc)
        Describe(getProc, description, sizeof(description));
    Say("system (%s): %s", why, description);
}

// the custom driver named by driver.txt; NULL when it is in use, else why not
static const char *TryCustomDriver(const char *files, const char *nativeDir)
{
    static char path[1024], name[256], library[256], from[1400], dir[1400], to[1700], hookDir[1100];
    snprintf(path, sizeof(path), "%s/driver.txt", files);
    char *text = ReadText(path);
    if (!text)
        return "no driver.txt";
    // the first line, without spaces around it
    char *start = text + strspn(text, " \t\r\n");
    start[strcspn(start, "\r\n")] = 0;
    for (char *end = start + strlen(start); end > start && (end[-1] == ' ' || end[-1] == '\t');)
        *--end = 0;
    snprintf(name, sizeof(name), "%s", start);
    free(text);
    if (!name[0] || !strcmp(name, "system"))
        return "driver.txt names none";
    if (strchr(name, '/') || !strcmp(name, ".."))
        return "driver.txt: not a folder name";

    snprintf(path, sizeof(path), "%s/driver/%s/meta.json", files, name);
    char *meta = ReadText(path);
    char title[128], version[128];
    JsonString(meta, "name", title, sizeof(title));
    JsonString(meta, "driverVersion", version, sizeof(version));
    JsonString(meta, "libraryName", library, sizeof(library));
    if (!meta)
        Say("%s has no meta.json", name);
    free(meta);
    snprintf(path, sizeof(path), "%s/driver/%s", files, name);
    if (!library[0])
    {
        // no libraryName: the folder's only library
        DIR *folder = opendir(path);
        for (struct dirent *entry; folder && (entry = readdir(folder));)
        {
            size_t n = strlen(entry->d_name);
            if (n > 3 && !strcmp(entry->d_name + n - 3, ".so"))
                snprintf(library, sizeof(library), "%s", entry->d_name);
        }
        if (folder)
            closedir(folder);
    }
    snprintf(from, sizeof(from), "%s/%s", path, library);
    if (!library[0] || access(from, R_OK) != 0)
        return "the driver folder named by driver.txt has no driver library";

    // into internal storage, where code may be loaded from
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (!internal)
        return "no internal storage path";
    snprintf(dir, sizeof(dir), "%s/driver", internal);
    mkdir(dir, 0700);
    snprintf(dir, sizeof(dir), "%s/driver/%s/", internal, name);
    mkdir(dir, 0700);
    snprintf(to, sizeof(to), "%s%s", dir, library);
    if (!CopyFile(from, to))
    {
        Say("cannot copy %s to %s: %s", from, to, strerror(errno));
        return "the custom driver could not be copied";
    }

    snprintf(hookDir, sizeof(hookDir), "%s/", nativeDir);
    snprintf(path, sizeof(path), "%s/libadrenotools.so", nativeDir);
    void *tools = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    OpenLibvulkan openLibvulkan = tools ? (OpenLibvulkan)dlsym(tools, "adrenotools_open_libvulkan") : NULL;
    if (!openLibvulkan)
    {
        Say("libadrenotools: %s", dlerror());
        return "libadrenotools could not be loaded";
    }
    void *vulkan = openLibvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, NULL, hookDir, dir, library, NULL, NULL);
    PFN_vkGetInstanceProcAddr getProc = vulkan ? (PFN_vkGetInstanceProcAddr)dlsym(vulkan, "vkGetInstanceProcAddr") : NULL;
    char description[512];
    if (!getProc || !Describe(getProc, description, sizeof(description)))
    {
        Say("%s (%s): adrenotools_open_libvulkan %s", name, library, !vulkan ? "failed (see logcat, tag AdrenoTools)"
            : !getProc ? "gave a loader without vkGetInstanceProcAddr" : "loaded it, but it has no device");
        return "the custom driver could not be used";
    }
    char number[32];
    snprintf(number, sizeof(number), "%p", (void *)getProc);
    setenv("BO1_VK_GET_INSTANCE_PROC_ADDR", number, 1);
    Say("custom \"%s\" from driver/%s/%s (meta.json: \"%s\", %s): %s", name, name, library, title, version[0] ? version : "no driverVersion", description);
    return NULL;
}

// files: the external files directory; nativeDir: the native library directory (libmain.so's, no trailing slash)
void Bo1SelectVulkanDriver(const char *files, const char *nativeDir)
{
    const char *why = TryCustomDriver(files, nativeDir);
    if (why)
        UseSystemDriver(why);
}
