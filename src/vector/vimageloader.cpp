#include "vimageloader.h"
#include "config.h"
#include "vdebug.h"
#include <cstring>

#ifdef _WIN32
# include <windows.h>
#else
# include <dlfcn.h>
#endif  // _WIN32

#ifdef LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
#include "stb/stb_image.h"
#endif // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED

using lottie_image_load_f = unsigned char *(*)(const char *filename, int *x,
                                               int *y, int *comp, int req_comp);
using lottie_image_load_data_f = unsigned char *(*)(const char *data, int len,
                                                    int *x, int *y, int *comp,
                                                    int req_comp);
using lottie_image_free_f = void (*)(unsigned char *);

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __APPLE__

unsigned char *lottie_image_load(char const *filename, int *x, int *y,
                                 int *comp, int req_comp)
{
    return nullptr;
}
unsigned char *lottie_image_load_from_data(const char *imageData, int len,
                                           int *x, int *y, int *comp,
                                           int req_comp)
{
    return nullptr;
}

void lottie_image_free(unsigned char *data) {}

#else // __APPLE__

extern unsigned char *lottie_image_load(char const *filename, int *x, int *y,
                                        int *comp, int req_comp);
extern unsigned char *lottie_image_load_from_data(const char *imageData,
                                                  int len, int *x, int *y,
                                                  int *comp, int req_comp);
extern void           lottie_image_free(unsigned char *data);

#endif // __APPLE__

#ifdef __cplusplus
}
#endif

struct VImageLoader::Impl {
#ifdef LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
    void  init() {}
    void moduleFree() {}
    bool moduleLoad() { return false; }
    unsigned char * imageLoad(char const *filename, int *x,
                              int *y, int *comp, int req_comp)
    {
        return stbi_load(filename, x, y, comp, req_comp);
    }
    
    unsigned char * imageFromData(const char *imageData,
                                  int len, int *x, int *y,
                                  int *comp, int req_comp)
    {
        unsigned char *data = (unsigned char *)imageData;
        return stbi_load_from_memory(data, len, x, y, comp, req_comp);
    }
    
    void imageFree(unsigned char *data)
    {
        stbi_image_free(data);
    }
#else // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
    lottie_image_load_f      imageLoad{nullptr};
    lottie_image_free_f      imageFree{nullptr};
    lottie_image_load_data_f imageFromData{nullptr};
#ifdef LOTTIE_IMAGE_MODULE_SUPPORT
# ifdef _WIN32
    HMODULE dl_handle{nullptr};
    bool    moduleLoad()
    {
        dl_handle = LoadLibraryA(LOTTIE_IMAGE_MODULE_PLUGIN);
        return (dl_handle == nullptr);
    }
    void moduleFree()
    {
        if (dl_handle) FreeLibrary(dl_handle);
    }
    void init()
    {
        imageLoad = reinterpret_cast<lottie_image_load_f>(
                    GetProcAddress(dl_handle, "lottie_image_load"));
        imageFree = reinterpret_cast<lottie_image_free_f>(
                    GetProcAddress(dl_handle, "lottie_image_free"));
        imageFromData = reinterpret_cast<lottie_image_load_data_f>(
                        GetProcAddress(dl_handle, "lottie_image_load_from_data"));
    }
# else  // _WIN32
    void *dl_handle{nullptr};
    void  init()
    {
        imageLoad = reinterpret_cast<lottie_image_load_f>(
                    dlsym(dl_handle, "lottie_image_load"));
        imageFree = reinterpret_cast<lottie_image_free_f>(
                    dlsym(dl_handle, "lottie_image_free"));
        imageFromData = reinterpret_cast<lottie_image_load_data_f>(
                    dlsym(dl_handle, "lottie_image_load_from_data"));
    }

    void moduleFree()
    {
        if (dl_handle) dlclose(dl_handle);
    }
    bool moduleLoad()
    {
        dl_handle = dlopen(LOTTIE_IMAGE_MODULE_PLUGIN, RTLD_LAZY);
        return (dl_handle == nullptr);
    }
# endif  // _WIN32
#else  // LOTTIE_IMAGE_MODULE_SUPPORT
    void  init()
    {
        imageLoad = lottie_image_load;
        imageFree = lottie_image_free;
        imageFromData = lottie_image_load_from_data;
    }
    void moduleFree() {}
    bool moduleLoad() { return false; }
#endif  // LOTTIE_IMAGE_MODULE_SUPPORT
#endif // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
    
    Impl()
    {
#ifdef LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
#else // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
        if (moduleLoad()) {
            vWarning << "Failed to dlopen librlottie-image-loader library";
            return;
        }

        init();

        if (!imageLoad)
            vWarning << "Failed to find symbol lottie_image_load in "
                        "librlottie-image-loader library";

        if (!imageFree)
            vWarning << "Failed to find symbol lottie_image_free in "
                        "librlottie-image-loader library";

        if (!imageFromData)
            vWarning << "Failed to find symbol lottie_image_load_data in "
                        "librlottie-image-loader library";
#endif // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
    }

#ifdef LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
#else // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
    ~Impl() { moduleFree(); }
#endif // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED

    VBitmap createBitmap(unsigned char *data, int width, int height,
                         int channel)
    {
        // premultiply alpha
        if (channel == 4)
            premultiplyAlpha(data, width, height);

        // create a bitmap of same size.
        VBitmap result =
            VBitmap(width, height, VBitmap::Format::ARGB32_Premultiplied);

        // copy the data to bitmap buffer
        memcpy(result.data(), data, width * height * 4);

        // free the image data
        imageFree(data);

        return result;
    }

    VBitmap load(const char *fileName)
    {
#ifdef LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
#else // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
        if (!imageLoad) return VBitmap();
#endif // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED

        int            width, height, n;
        unsigned char *data = imageLoad(fileName, &width, &height, &n, 4);

        if (!data) {
            return VBitmap();
        }

        return createBitmap(data, width, height, n);
    }

    VBitmap load(const char *imageData, size_t len)
    {
#ifdef LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
#else // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED
        if (!imageFromData) return VBitmap();
#endif // LOTTIE_DIRECT_IMAGE_LOADER_ENABLED

        int            width, height, n;
        unsigned char *data =
            imageFromData(imageData, static_cast<int>(len), &width, &height, &n, 4);

        if (!data) {
            return VBitmap();
        }

        return createBitmap(data, width, height, n);
    }
    
    void premultiplyAlpha(unsigned char *bits, int width, int height)
    {
        int            pixelCount = width * height;
        unsigned char *pix = bits;
        for (int i = 0; i < pixelCount; i++) {
            unsigned char r = pix[0];
            unsigned char g = pix[1];
            unsigned char b = pix[2];
            unsigned char a = pix[3];

            pix[0] = (r * a) / 255;
            pix[1] = (g * a) / 255;
            pix[2] = (b * a) / 255;

            pix += 4;
        }
    }
};

VImageLoader::VImageLoader() : mImpl(std::make_unique<VImageLoader::Impl>()) {}

VImageLoader::~VImageLoader() {}

VBitmap VImageLoader::load(const char *fileName)
{
    return mImpl->load(fileName);
}

VBitmap VImageLoader::load(const char *data, size_t len)
{
    return mImpl->load(data, int(len));
}
