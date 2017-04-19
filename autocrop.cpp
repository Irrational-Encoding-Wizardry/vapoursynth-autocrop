#include "VapourSynth.h"
#include "VSHelper.h"
#include <iostream>
#define SET_COLOR_RESAMPLE 0x0000001
#define setBlack(color, format) {uint32_t b1[3] = {0, 123, 123}; setColor(color, format, b1, SET_COLOR_RESAMPLE);}
#define setBlack2(color, format) {uint32_t b2[3] = {21, 133, 133}; setColor(color, format, b2, SET_COLOR_RESAMPLE);}
#define setColorSimple(color, format, base) setColor(color, format, base, SET_COLOR_RESAMPLE);
#define OUT
#define IN

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi_finish;
    const VSVideoInfo *vi;
    int range;
    int top;
    int bottom;
    int left;
    int right;
    uint32_t color[3];
    uint32_t color_second[3];
    int x;
    int y;
    int x2;
    int y2;
    int width;
    int height;
} InvertData;

struct CropValues{
    int top_crop[3];
    int bottom_crop[3];
    int left_crop[3];
    int right_crop[3];
};

static inline void setColor(OUT uint32_t color[3], const IN VSFormat *format, IN uint32_t base[3], int flags=0) {
    if (flags & SET_COLOR_RESAMPLE && format->sampleType == stInteger) {
        for (int i = 0; i<3; i++)
            base[i] <<= (format->bitsPerSample - 8);
    }

    for (int i = 0; i < 3; i++)
        color[i] = base[i];
}

static int ColorCheck(int crop_plane0, int crop_plane1, int crop_plane2, void **instanceData, int side = 1){
    InvertData *d = (InvertData *) * instanceData;
    int crop_plane;

    if(side){
        crop_plane = std::min(std::min(crop_plane0, crop_plane1 << d->vi->format->subSamplingW), crop_plane2 << d->vi->format->subSamplingW);
        crop_plane = crop_plane - (crop_plane % (1 << d->vi->format->subSamplingW));
    }
    else{
        crop_plane = std::min(std::min(crop_plane0, crop_plane1 << d->vi->format->subSamplingH), crop_plane2 << d->vi->format->subSamplingH);
        crop_plane = crop_plane - (crop_plane % (1 << d->vi->format->subSamplingH));
    }
    return crop_plane;
}

template <typename Bit>
void getCropValues(struct CropValues *c, const Bit *srcp, int src_stride, int w, int h, int color_second, int color2,
                   int top_range, int bottom_range, int left_range, int right_range, int ArrayNumber){
    int top = 0;
    int top_crop = 0;
    int bottom_crop = 0;
    int left_crop = left_range;
    int right_crop = right_range;
    bool left_check = false;
    bool right_check = false;
    for (int y = 0; y < h; y++) {
        //top
        if (top < top_range) {
            for (int x = 0; x < w; x += 10) {
                if (!(color_second <= srcp[x] && srcp[x] <= color2)) {
                    top = top_range;
                    break;
                }
            }
            if (top < top_range) {
                top++;
                top_crop++;
            }
        }
        //left
        for (int x = 0; x < left_range; x++) {
            if (!(color_second <= srcp[x] && srcp[x] <= color2)) {
                left_check = true;
                if (left_crop >= x) {
                    left_crop = x;
                }
            }
        }
        //right
        for (int x = w - right_range; x < w; x++) {
            if (!(color_second <= srcp[x] && srcp[x] <= color2)) {
                right_check = true;
                if (right_crop >= w - x) {
                    right_crop = w - x - 1;
                }
            }
        }
        //bottom
        bool r = false;
        if (y >= h - bottom_range) {
            for (int x = 0; x < w; x += 10) {
                if (!r) {
                    if (!(color_second <= srcp[x] && srcp[x] <= color2)) {
                        r = true;
                    }
                }
            }
            if (!r) {
                bottom_crop++;
            }
        }
        srcp += src_stride;
    }
    if (right_check && right_crop == right_range){
        right_crop = 0;
    }
    if (left_check && left_crop == left_range){
        left_crop = 0;
    }

    c->top_crop[ArrayNumber] = top_crop;
    c->bottom_crop[ArrayNumber] = bottom_crop;
    c->left_crop[ArrayNumber] = left_crop;
    c->right_crop[ArrayNumber] = right_crop;
}

template <typename Bit>
void getFramePlane(const VSFrameRef *src, void **instanceData, const VSAPI *vsapi){
    InvertData *d = (InvertData *) * instanceData;
    struct CropValues c;
    const VSFormat *fi = d->vi->format;

    for (int plane = 0; plane < fi->numPlanes; plane++) {
        const Bit *srcp = (const Bit *) vsapi->getReadPtr(src, plane);
        int src_stride = vsapi->getStride(src, plane)  / sizeof(Bit);
        int h = vsapi->getFrameHeight(src, plane);
        int w = vsapi->getFrameWidth(src, plane);
        if (plane == 0){
            getCropValues<Bit>(&c, srcp, src_stride, w, h, d->color[0], d->color_second[0], d->top, d->bottom, d->left, d->right, 0);
        }
        else if(plane == 1){
            if (d->vi->format->subSamplingH == 0 && d->vi->format->subSamplingW == 0) {
                getCropValues<Bit>(&c, srcp, src_stride, w, h, d->color[1], d->color_second[1], d->top, d->bottom, d->left, d->right, 1);
            }
            else if (d->vi->format->subSamplingH == 1 && d->vi->format->subSamplingW == 1){
                getCropValues<Bit>(&c, srcp, src_stride, w, h, d->color[1], d->color_second[1], d->top/2, d->bottom/2, d->left/2, d->right/2, 1);
            }
        }
        else{
            if (d->vi->format->subSamplingH == 0 && d->vi->format->subSamplingW == 0) {
                getCropValues<Bit>(&c, srcp, src_stride, w, h, d->color[2], d->color_second[2], d->top, d->bottom, d->left, d->right, 2);
            }
            else if (d->vi->format->subSamplingH == 1 && d->vi->format->subSamplingW == 1){
                getCropValues<Bit>(&c, srcp, src_stride, w, h, d->color[2], d->color_second[2], d->top/2, d->bottom/2, d->left/2, d->right/2, 2);
            }
        }
    }
    int side = 0;

    d->x = int64ToIntS(ColorCheck(c.left_crop[0], c.left_crop[1], c.left_crop[2], instanceData));
    d->x2 = int64ToIntS(ColorCheck(c.right_crop[0], c.right_crop[1], c.right_crop[2], instanceData));
    d->y = int64ToIntS(ColorCheck(c.top_crop[0], c.top_crop[1], c.top_crop[2], instanceData, side));
    d->y2 = int64ToIntS(ColorCheck(c.bottom_crop[0], c.bottom_crop[1], c.bottom_crop[2], instanceData, side));
    d->height = d->vi->height - d->y - d->y2;
    d->width = d->vi->width - d->x - d->x2;
}

/////////////////
// CropProp

static void VS_CC croppropInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *) * instanceData;
    VSVideoInfo vi_finish = *d->vi_finish;
    vi_finish.height = 0;
    vi_finish.width = 0;
    vsapi->setVideoInfo(&vi_finish, 1, node);
}

static const VSFrameRef *VS_CC croppropGetFrame(int n, int activationReason, void **instanceData, void **frameData,
                                                VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *) * instanceData;
    const char *top = "CropTopValue";
    const char *bottom = "CropBottomValue";
    const char *left = "CropLeftValue";
    const char *right = "CropRightValue";
    int err;


    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = d->vi->format;
        const VSMap *prop = vsapi->getFramePropsRO(src);

        d->y = static_cast<int>(vsapi->propGetInt(prop, top, 0, &err));
        if(err){
            vsapi->freeFrame(src);
            vsapi->setFilterError("CropProp: Cant Read Frame Prop ... Fatal Error", frameCtx);
            return 0;
        }

        d->x = static_cast<int>(vsapi->propGetInt(prop, left, 0, &err));
        if(err){
            vsapi->freeFrame(src);
            vsapi->setFilterError("CropProp: Cant Read Frame Prop ... Fatal Error", frameCtx);
            return 0;
        }

        d->y2 = static_cast<int>(vsapi->propGetInt(prop, bottom, 0, &err));
        if(err){
            vsapi->freeFrame(src);
            vsapi->setFilterError("CropProp: Cant Read Frame Prop ... Fatal Error", frameCtx);
            return 0;
        }

        d->x2 = static_cast<int>(vsapi->propGetInt(prop, right, 0, &err));
        if(err){
            vsapi->freeFrame(src);
            vsapi->setFilterError("CropProp: Cant Read Frame Prop ... Fatal Error", frameCtx);
            return 0;
        }

        d->height = d->vi->height - d->y - d->y2;
        d->width = d->vi->width - d->x - d->x2;

        //from https://github.com/vapoursynth/vapoursynth/blob/738f2be63b8d2b19c73b5e20116058f12f9b278d/src/core/simplefilters.c#L132
        VSFrameRef *dst_finish = vsapi->newVideoFrame(fi, d->width, d->height, src, core);
        for (int plane_new = 0; plane_new < fi->numPlanes; plane_new++) {
            int srcstride = vsapi->getStride(src, plane_new);
            int dststride = vsapi->getStride(dst_finish, plane_new);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane_new);
            uint8_t *dstdata = vsapi->getWritePtr(dst_finish, plane_new);
            srcdata += srcstride * (d->y >> (plane_new ? fi->subSamplingH : 0));
            srcdata += (d->x >> (plane_new ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            vs_bitblt(dstdata, dststride, srcdata, srcstride, (d->width >> (plane_new ? fi->subSamplingW : 0)) * fi->bytesPerSample, vsapi->getFrameHeight(dst_finish, plane_new));
        }
        vsapi->freeFrame(src);

        return dst_finish;
    }

    return 0;
}

static void VS_CC croppropFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC croppropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    InvertData d;
    InvertData *data;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi_finish = vsapi->getVideoInfo(d.node);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample < 8 || d.vi->format->bitsPerSample > 16) {
        vsapi->setError(out, "AutoCrop: only constant format 8...16Bit integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (!(d.vi->format->colorFamily == cmYCoCg || d.vi->format->colorFamily == cmYUV)){
        vsapi->setError(out, "AutoCrop: only YUV or YCoCg  input supported");
        vsapi->freeNode(d.node);
        return;
    }

    data = static_cast<InvertData*>(malloc(sizeof(d)));
    *data = d;

    vsapi->createFilter(in, out, "CropProp", croppropInit, croppropGetFrame, croppropFree, fmParallel, 0, data, core);

}

/////////////////
// CropValues

static void VS_CC cropvaluesInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *) * instanceData;
    VSVideoInfo vi_finish = *d->vi_finish;
    vsapi->setVideoInfo(&vi_finish, 1, node);
}

static const VSFrameRef *VS_CC cropvaluesGetFrame(int n, int activationReason, void **instanceData, void **frameData,
                                                VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *) * instanceData;
    const char *top = "CropTopValue";
    const char *bottom = "CropBottomValue";
    const char *left = "CropLeftValue";
    const char *right = "CropRightValue";

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFormat *fi = d->vi->format;

        if (d->vi->format->sampleType == stInteger && d->vi->format->bitsPerSample == 8) {
            getFramePlane<uint8_t>(src, instanceData, vsapi);
        } else if (d->vi->format->sampleType == stInteger && d->vi->format->bitsPerSample <= 16){
            getFramePlane<uint16_t>(src, instanceData, vsapi);
        }

        VSFrameRef *dst = vsapi->copyFrame(src, core);
        int plane;
        for (plane = 0; plane < fi->numPlanes; plane++) {
            vsapi->getWritePtr(dst, plane);
        }

        VSMap *dstProps = vsapi->getFramePropsRW(dst);

        if (fi->sampleType == stInteger) {
            vsapi->propSetInt(dstProps, top, d->y, paAppend);
            vsapi->propSetInt(dstProps, bottom, d->y2, paAppend);
            vsapi->propSetInt(dstProps, left, d->x, paAppend);
            vsapi->propSetInt(dstProps, right, d->x2, paAppend);
        }
        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}

static void VS_CC cropvaluesFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC cropvaluesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    InvertData d;
    InvertData *data;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi_finish = vsapi->getVideoInfo(d.node);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample < 8 || d.vi->format->bitsPerSample > 16) {
        vsapi->setError(out, "CropValues: only constant format 8...16Bit integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (!(d.vi->format->colorFamily == cmYCoCg || d.vi->format->colorFamily == cmYUV)){
        vsapi->setError(out, "CropValues: only YUV or YCoCg  input supported");
        vsapi->freeNode(d.node);
        return;
    }

    d.range = int64ToIntS(vsapi->propGetInt(in, "range", 0, &err));
    if (err)
        d.range = 4;

    d.top = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));
    if (err)
        d.top = d.range;

    d.bottom = int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
    if (err)
        d.bottom = d.range;

    d.right = int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));
    if (err)
        d.right = d.range;

    d.left = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    if (err)
        d.left = d.range;

    if (d.vi->format->subSamplingH == 1 && d.vi->format->subSamplingW == 1){
        if (d.range&1 || d.left&1 || d.right&1 || d.top&1 || d.bottom&1) {
            vsapi->setError(out, "CropValues: Odd numbers for Crop not allowed");
            vsapi->freeNode(d.node);
            return;
        }
    }

    if (d.range < 0 || d.left < 0 || d.right < 0 || d.top < 0 || d.bottom < 0) {
        vsapi->setError(out, "AutoCrop: Negative numbers for crop not allowed");
        vsapi->freeNode(d.node);
        return;
    }

    uint32_t color_base[3];
    uint32_t color_base1[3];
    int numcomponents = (d.vi->format->colorFamily == cmCompat) ? 3 : d.vi->format->numPlanes;
    int ncolors = vsapi->propNumElements(in, "color");
    int ncolors1 = vsapi->propNumElements(in, "color_second");

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            color_base[i] = vsapi->propGetInt(in, "color", i, 0);
        }
        setColorSimple(d.color, d.vi->format, color_base);
    }
    else{
        setBlack(d.color, d.vi->format);
    }

    if (ncolors1 == numcomponents) {
        for (int i = 0; i < ncolors1; i++) {
            color_base1[i] = vsapi->propGetInt(in, "color_second", i, 0);
        }
        setColorSimple(d.color_second, d.vi->format, color_base1);
    }
    else{
        setBlack2(d.color_second, d.vi->format);
    }

    data = static_cast<InvertData*>(malloc(sizeof(d)));
    *data = d;

    vsapi->createFilter(in, out, "CropValues", cropvaluesInit, cropvaluesGetFrame, cropvaluesFree, fmParallel, 0, data, core);
}

/////////////////
// AutoCrop

static void VS_CC autocropInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *) * instanceData;
    VSVideoInfo vi_finish = *d->vi_finish;
    vi_finish.height = 0;
    vi_finish.width = 0;
    vsapi->setVideoInfo(&vi_finish, 1, node);
}

static const VSFrameRef *VS_CC autocropGetFrame(int n, int activationReason, void **instanceData, void **frameData,
                                                VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);

        const VSFormat *fi = d->vi->format;

        if (d->vi->format->sampleType == stInteger && d->vi->format->bitsPerSample == 8) {
            getFramePlane<uint8_t>(src, instanceData, vsapi);
        } else if (d->vi->format->sampleType == stInteger && d->vi->format->bitsPerSample <= 16){
            getFramePlane<uint16_t>(src, instanceData, vsapi);
        }

        //from https://github.com/vapoursynth/vapoursynth/blob/738f2be63b8d2b19c73b5e20116058f12f9b278d/src/core/simplefilters.c#L132
        VSFrameRef *dst_finish = vsapi->newVideoFrame(fi, d->width, d->height, src, core);
        for (int plane_new = 0; plane_new < fi->numPlanes; plane_new++) {
            int srcstride = vsapi->getStride(src, plane_new);
            int dststride = vsapi->getStride(dst_finish, plane_new);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane_new);
            uint8_t *dstdata = vsapi->getWritePtr(dst_finish, plane_new);
            srcdata += srcstride * (d->y >> (plane_new ? fi->subSamplingH : 0));
            srcdata += (d->x >> (plane_new ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            vs_bitblt(dstdata, dststride, srcdata, srcstride, (d->width >> (plane_new ? fi->subSamplingW : 0)) * fi->bytesPerSample, vsapi->getFrameHeight(dst_finish, plane_new));
        }
        vsapi->freeFrame(src);

        return dst_finish;
        }

    return 0;
}

static void VS_CC autocropFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    InvertData *d = (InvertData *)instanceData;
    vsapi->freeNode(d->node);
    free(d);
}

static void VS_CC autocropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    InvertData d;
    InvertData *data;
    uint32_t color_base[3];
    uint32_t color_base1[3];
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi_finish = vsapi->getVideoInfo(d.node);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample < 8 || d.vi->format->bitsPerSample > 16) {
        vsapi->setError(out, "AutoCrop: only constant format 8...16Bit integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (!(d.vi->format->colorFamily == cmYCoCg || d.vi->format->colorFamily == cmYUV)){
        vsapi->setError(out, "AutoCrop: only YUV or YCoCg  input supported");
        vsapi->freeNode(d.node);
        return;
    }

    d.range = int64ToIntS(vsapi->propGetInt(in, "range", 0, &err));
    if (err)
        d.range = 4;

    d.top = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));
    if (err)
        d.top = d.range;

    d.bottom = int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
    if (err)
        d.bottom = d.range;

    d.right = int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));
    if (err)
        d.right = d.range;

    d.left = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    if (err)
        d.left = d.range;

    if (d.vi->format->subSamplingH == 1 && d.vi->format->subSamplingW == 1){
        if (d.range&1 || d.left&1 || d.right&1 || d.top&1 || d.bottom&1) {
            vsapi->setError(out, "AutoCrop: Odd numbers for crop not allowed");
            vsapi->freeNode(d.node);
            return;
        }
    }

    if (d.range < 0 || d.left < 0 || d.right < 0 || d.top < 0 || d.bottom < 0) {
        vsapi->setError(out, "AutoCrop: Negative numbers for crop not allowed");
        vsapi->freeNode(d.node);
        return;
    }

    int numcomponents = (d.vi->format->colorFamily == cmCompat) ? 3 : d.vi->format->numPlanes;
    int ncolors = vsapi->propNumElements(in, "color");
    int ncolors1 = vsapi->propNumElements(in, "color_second");

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            color_base[i] = vsapi->propGetInt(in, "color", i, 0);
        }
        setColorSimple(d.color, d.vi->format, color_base);
    }
    else{
        setBlack(d.color, d.vi->format);
    }

    if (ncolors1 == numcomponents) {
        for (int i = 0; i < ncolors1; i++) {
            color_base1[i] = vsapi->propGetInt(in, "color_second", i, 0);
        }
        setColorSimple(d.color_second, d.vi->format, color_base1);
    }
    else{
        setBlack2(d.color_second, d.vi->format);
    }
    data = static_cast<InvertData*>(malloc(sizeof(d)));
    *data = d;

    vsapi->createFilter(in, out, "AutoCrop", autocropInit, autocropGetFrame, autocropFree, fmParallel, 0, data, core);

}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("info.infiistgott.autocrop", "acrop", "VapourSynth AutoCrop", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("AutoCrop", "clip:clip;range:int:opt;top:int:opt;bottom:int:opt;left:int:opt;right:int:opt;color:int[]:opt;color_second:int[]:opt", autocropCreate, 0, plugin);
    registerFunc("CropValues", "clip:clip;range:int:opt;top:int:opt;bottom:int:opt;left:int:opt;right:int:opt;color:int[]:opt;color_second:int[]:opt", cropvaluesCreate, 0, plugin);
    registerFunc("CropProp", "clip:clip", croppropCreate, 0, plugin);
}
