#ifndef LOTModel_H
#define LOTModel_H

#include<vector>
#include<memory>
#include<unordered_map>
#include"vpoint.h"
#include"vrect.h"
#include"vinterpolator.h"
#include"vmatrix.h"
#include"vbezier.h"
#include"vbrush.h"
#include"vpath.h"

V_USE_NAMESPACE

class LOTCompositionData;
class LOTLayerData;
class LOTTransformData;
class LOTShapeGroupData;
class LOTShapeData;
class LOTRectData;
class LOTEllipseData;
class LOTTrimData;
class LOTRepeaterData;
class LOTFillData;
class LOTStrokeData;
class LOTGroupData;
class LOTGFillData;
class LOTGStrokeData;
class LottieShapeData;
class LOTPolystarData;
class LOTMaskData;

enum class MatteType
{
    None = 0,
    Alpha = 1,
    AlphaInv,
    Luma,
    LumaInv
};

enum class LayerType {
    Precomp = 0,
    Solid = 1,
    Image = 2,
    Null = 3,
    Shape = 4,
    Text = 5
};

class LottieColor
{
public:
    LottieColor() = default;
    LottieColor(float red, float green , float blue):r(red), g(green),b(blue){}
    VColor toColor(float a=1){ return VColor((255 * r), (255 * g), (255 * b), (255 * a));}
    friend inline LottieColor operator+(const LottieColor &c1, const LottieColor &c2);
    friend inline LottieColor operator-(const LottieColor &c1, const LottieColor &c2);
public:
    float r{1};
    float g{1};
    float b{1};
};

inline LottieColor operator-(const LottieColor &c1, const LottieColor &c2)
{
    return LottieColor(c1.r - c2.r, c1.g - c2.g, c1.b - c2.b);
}
inline LottieColor operator+(const LottieColor &c1, const LottieColor &c2)
{
    return LottieColor(c1.r + c2.r, c1.g + c2.g, c1.b + c2.b);
}

inline const LottieColor operator*(const LottieColor &c, float m)
{ return LottieColor(c.r*m, c.g*m, c.b*m); }

inline const LottieColor operator*(float m, const LottieColor &c)
{ return LottieColor(c.r*m, c.g*m, c.b*m); }

class LottieShapeData
{
public:
    void reserve(int size) {
        mPoints.reserve(mPoints.size() + size);
    }
    void toPath(VPath& path) {
        path.reset();

        if (mPoints.empty()) return;

        int size = mPoints.size();
        const VPointF *points = mPoints.data();
        /* reserve exact memory requirement at once
         * ptSize = size + 1(size + close)
         * elmSize = size/3 cubic + 1 move + 1 close
         */
        path.reserve(size + 1 , size/3 + 2);
        path.moveTo(points[0]);
        for (int i = 1 ; i < size; i+=3) {
           path.cubicTo(points[i], points[i+1], points[i+2]);
        }
        if (mClosed)
          path.close();
    }
public:
    std::vector<VPointF>    mPoints;
    bool                     mClosed = false;   /* "c" */
};



template<typename T>
inline T lerp(const T& start, const T& end, float t)
{
    return start + t * (end - start);
}

inline LottieShapeData lerp(const LottieShapeData& start, const LottieShapeData& end, float t)
{
    if (start.mPoints.size() != start.mPoints.size())
       return LottieShapeData();

    LottieShapeData result;
    result.reserve(start.mPoints.size());
    for (unsigned int i = 0 ; i < start.mPoints.size(); i++) {
       result.mPoints.push_back(start.mPoints[i] + t * (end.mPoints[i] - start.mPoints[i]));
    }
   return result;
}

template <typename T>
struct LOTKeyFrameValue
{
    T mStartValue;
    T mEndValue;
    T value(float t) const {
        return lerp(mStartValue, mEndValue, t);
    }
};

template <>
struct LOTKeyFrameValue<VPointF>
{
    VPointF mStartValue;
    VPointF mEndValue;
    VPointF mInTangent;
    VPointF mOutTangent;
    bool    mPathKeyFrame = false;

    VPointF value(float t) const {
        if (mPathKeyFrame) {
            return VBezier::fromPoints(mStartValue, mStartValue + mOutTangent,
                                       mEndValue + mInTangent, mEndValue).pointAt(t);
        } else {
            return lerp(mStartValue, mEndValue, t);
        }
    }
};


template<typename T>
class LOTKeyFrame
{
public:
    T value(int frameNo) const {
        float progress = mInterpolator->value(float(frameNo - mStartFrame) / float(mEndFrame - mStartFrame));
        return mValue.value(progress);
    }

public:
    int                 mStartFrame{0};
    int                 mEndFrame{0};
    std::shared_ptr<VInterpolator> mInterpolator;
    LOTKeyFrameValue<T>  mValue;
};

template<typename T>
class LOTAnimInfo
{
public:
    T value(int frameNo) const {
        if (mKeyFrames.front().mStartFrame >= frameNo)
            return mKeyFrames.front().mValue.mStartValue;
        if(mKeyFrames.back().mEndFrame <= frameNo)
            return mKeyFrames.back().mValue.mEndValue;

        for(auto keyFrame : mKeyFrames) {
            if (frameNo >= keyFrame.mStartFrame && frameNo < keyFrame.mEndFrame)
                return keyFrame.value(frameNo);
        }
        return T();
    }
public:
    std::vector<LOTKeyFrame<T>>    mKeyFrames;
};

template<typename T>
class LOTAnimatable
{
public:
    LOTAnimatable():mValue(),mAnimInfo(nullptr){}
    LOTAnimatable(const T &value): mValue(value){}
    bool isStatic() const {if (mAnimInfo) return false; else return true;}
    T value(int frameNo) const{
        if (isStatic())
            return mValue;
        else
            return mAnimInfo->value(frameNo);
    }
public:
    T                                    mValue;
    int                                  mPropertyIndex; /* "ix" */
    std::unique_ptr<LOTAnimInfo<T>>   mAnimInfo;
};

enum class LottieBlendMode
{
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    OverLay = 3
};

class LOTDataVisitor;
class LOTData
{
public:
    enum class Type {
        Composition = 1,
        Layer,
        ShapeGroup,
        Transform,
        Fill,
        Stroke,
        GFill,
        GStroke,
        Rect,
        Ellipse,
        Shape,
        Polystar,
        Trim,
        Repeater
    };
    LOTData(LOTData::Type  type): mType(type){}
    inline LOTData::Type type() const {return mType;}
    bool isStatic() const{return mStatic;}
    void setStatic(bool value) {mStatic = value;}
public:
    bool                mStatic{true};
    LOTData::Type       mType;
};

class LOTGroupData: public LOTData
{
public:
    LOTGroupData(LOTData::Type  type):LOTData(type){}
public:
    std::vector<std::shared_ptr<LOTData>>  mChildren;
    std::shared_ptr<LOTTransformData>      mTransform;
};

class LOTShapeGroupData : public LOTGroupData
{
public:
    LOTShapeGroupData():LOTGroupData(LOTData::Type::ShapeGroup){}
};

class LOTLayerData;
struct LOTAsset
{
    int                                          mAssetType; //lottie asset type  (precomp/char/image)
    std::string                                  mRefId; // ref id
    std::vector<std::shared_ptr<LOTData>>   mLayers;
};

class LOTLayerData : public LOTGroupData
{
public:
    LOTLayerData():LOTGroupData(LOTData::Type::Layer){}
    bool hasPathOperator() const noexcept {return mHasPathOperator;}
    bool hasGradient() const noexcept {return mHasGradient;}
    bool hasMask() const noexcept {return mHasMask;}
    bool hasRepeater() const noexcept {return mHasRepeater;}
    bool root() const noexcept {return mRoot;}
    int id() const noexcept{ return mId;}
    int parentId() const noexcept{ return mParentId;}
    int inFrame() const noexcept{return mInFrame;}
    int outFrame() const noexcept{return mOutFrame;}
    int startFrame() const noexcept{return mStartFrame;}
    int solidWidth() const noexcept{return mSolidLayer.mWidth;}
    int solidHeight() const noexcept{return mSolidLayer.mHeight;}
    LottieColor solidColor() const noexcept{return mSolidLayer.mColor;}
public:
    struct SolidLayer {
        int            mWidth{0};
        int            mHeight{0};
        LottieColor    mColor;
    };

    MatteType            mMatteType{MatteType::None};
    VRect                mBound;
    LayerType            mLayerType; //lottie layer type  (solid/shape/precomp)
    int                  mParentId{-1}; // Lottie the id of the parent in the composition
    int                  mId{-1};  // Lottie the group id  used for parenting.
    long                 mInFrame{0};
    long                 mOutFrame{0};
    long                 mStartFrame{0};
    LottieBlendMode      mBlendMode;
    float                mTimeStreatch{1.0f};
    std::string          mPreCompRefId;
    LOTAnimatable<float> mTimeRemap;  /* "tm" */
    SolidLayer           mSolidLayer;
    bool                 mHasPathOperator{false};
    bool                 mHasMask{false};
    bool                 mHasRepeater{false};
    bool                 mHasGradient{false};
    bool                 mRoot{false};
    std::vector<std::shared_ptr<LOTMaskData>>  mMasks;
};

class LOTCompositionData : public LOTData
{
public:
    LOTCompositionData():LOTData(LOTData::Type::Composition){}
    double duration() const {
        return isStatic() ? startFrame() :
                            frameDuration() / frameRate(); // in second
    }
    size_t frameAtPos(double pos) const {
        if (pos < 0) pos = 0;
        if (pos > 1) pos = 1;
        return isStatic() ? startFrame() :
                            startFrame() + pos * frameDuration();
    }
    long frameDuration() const {return mEndFrame - mStartFrame -1;}
    float frameRate() const {return mFrameRate;}
    long startFrame() const {return mStartFrame;}
    long endFrame() const {return mEndFrame;}
    VSize size() const {return mSize;}
    void processRepeaterObjects();
public:
    std::string          mVersion;
    VSize                mSize;
    long                 mStartFrame{0};
    long                 mEndFrame{0};
    float                mFrameRate;
    LottieBlendMode      mBlendMode;
    std::shared_ptr<LOTLayerData> mRootLayer;
    std::unordered_map<std::string,
                       std::shared_ptr<VInterpolator>> mInterpolatorCache;
    std::unordered_map<std::string,
                       std::shared_ptr<LOTAsset>>    mAssets;

};

class LOTTransformData : public LOTData
{
public:
    LOTTransformData():LOTData(LOTData::Type::Transform),mScale({100, 100}){}
    VMatrix matrix(int frameNo) const;
    float    opacity(int frameNo) const;
    void cacheMatrix();
    bool staticMatrix() const {return mStaticMatrix;}
private:
    VMatrix computeMatrix(int frameNo) const;
public:
    LOTAnimatable<float>     mRotation{0};  /* "r" */
    LOTAnimatable<VPointF>   mScale;     /* "s" */
    LOTAnimatable<VPointF>   mPosition;  /* "p" */
    LOTAnimatable<float>     mX{0};
    LOTAnimatable<float>     mY{0};
    LOTAnimatable<VPointF>   mAnchor;    /* "a" */
    LOTAnimatable<float>     mOpacity{100};   /* "o" */
    LOTAnimatable<float>     mSkew{0};      /* "sk" */
    LOTAnimatable<float>     mSkewAxis{0};  /* "sa" */
    bool                     mStaticMatrix{true};
    bool                     mSeparate{false};
    VMatrix                  mCachedMatrix;
};

class LOTFillData : public LOTData
{
public:
    LOTFillData():LOTData(LOTData::Type::Fill){}
    float opacity(int frameNo) const {return mOpacity.value(frameNo)/100.0;}
    FillRule fillRule() const {return mFillRule;}
public:
    FillRule                       mFillRule{FillRule::Winding}; /* "r" */
    LOTAnimatable<LottieColor>     mColor;   /* "c" */
    LOTAnimatable<int>             mOpacity{100};  /* "o" */
    bool                           mEnabled{true}; /* "fillEnabled" */
};

struct LOTDashProperty
{
    LOTAnimatable<float>     mDashArray[5]; /* "d" "g" "o"*/
    int                      mDashCount{0};
    bool                     mStatic{true};
};

class LOTStrokeData : public LOTData
{
public:
    LOTStrokeData():LOTData(LOTData::Type::Stroke){}
    float opacity(int frameNo) const {return mOpacity.value(frameNo)/100.0;}
    float width(int frameNo) const {return mWidth.value(frameNo);}
    CapStyle capStyle() const {return mCapStyle;}
    JoinStyle joinStyle() const {return mJoinStyle;}
    float meterLimit() const{return mMeterLimit;}
    bool hasDashInfo() const { return !(mDash.mDashCount == 0);}
    int getDashInfo(int frameNo, float *array) const;
public:
    LOTAnimatable<LottieColor>        mColor;      /* "c" */
    LOTAnimatable<int>                mOpacity{100};    /* "o" */
    LOTAnimatable<float>              mWidth{0};      /* "w" */
    CapStyle                          mCapStyle;   /* "lc" */
    JoinStyle                         mJoinStyle;  /* "lj" */
    float                             mMeterLimit{0}; /* "ml" */
    LOTDashProperty                   mDash;
    bool                              mEnabled{true}; /* "fillEnabled" */
};

class LottieGradient
{
public:
    friend inline LottieGradient operator+(const LottieGradient &g1, const LottieGradient &g2);
    friend inline LottieGradient operator-(const LottieGradient &g1, const LottieGradient &g2);
    friend inline LottieGradient operator*(float m, const LottieGradient &g);
public:
    std::vector<float>    mGradient;
};

inline LottieGradient operator+(const LottieGradient &g1, const LottieGradient &g2)
{
    if (g1.mGradient.size() != g2.mGradient.size())
        return g1;

    LottieGradient newG;
    newG.mGradient = g1.mGradient;

    auto g2It = g2.mGradient.begin();
    for(auto &i : newG.mGradient) {
        i = i + *g2It;
        g2It++;
    }

    return newG;
}

inline LottieGradient operator-(const LottieGradient &g1, const LottieGradient &g2)
{
    if (g1.mGradient.size() != g2.mGradient.size())
        return g1;
    LottieGradient newG;
    newG.mGradient = g1.mGradient;

    auto g2It = g2.mGradient.begin();
    for(auto &i : newG.mGradient) {
        i = i - *g2It;
        g2It++;
    }

    return newG;
}

inline LottieGradient operator*(float m, const LottieGradient &g)
{
    LottieGradient newG;
    newG.mGradient = g.mGradient;

    for(auto &i : newG.mGradient) {
        i = i * m;
    }
    return newG;
}



class LOTGradient : public LOTData
{
public:
    LOTGradient(LOTData::Type  type):LOTData(type){}
    inline float opacity(int frameNo) const {return mOpacity.value(frameNo)/100.0;}
    void update(std::unique_ptr<VGradient> &grad, int frameNo);

private:
    void populate(VGradientStops &stops, int frameNo);
public:
    int                                 mGradientType;        /* "t" Linear=1 , Radial = 2*/
    LOTAnimatable<VPointF>              mStartPoint;          /* "s" */
    LOTAnimatable<VPointF>              mEndPoint;            /* "e" */
    LOTAnimatable<float>                mHighlightLength{0};     /* "h" */
    LOTAnimatable<float>                mHighlightAngle{0};      /* "a" */
    LOTAnimatable<int>                  mOpacity{0};             /* "o" */
    LOTAnimatable<LottieGradient>       mGradient;            /* "g" */
    int                                 mColorPoints{-1};
    bool                                mEnabled{true};      /* "fillEnabled" */
};

class LOTGFillData : public LOTGradient
{
public:
    LOTGFillData():LOTGradient(LOTData::Type::GFill){}
    FillRule fillRule() const {return mFillRule;}
public:
    FillRule                       mFillRule{FillRule::Winding}; /* "r" */
};

class LOTGStrokeData : public LOTGradient
{
public:
    LOTGStrokeData():LOTGradient(LOTData::Type::GStroke){}
    float width(int frameNo) const {return mWidth.value(frameNo);}
    CapStyle capStyle() const {return mCapStyle;}
    JoinStyle joinStyle() const {return mJoinStyle;}
    float meterLimit() const{return mMeterLimit;}
    bool hasDashInfo() const { return !(mDash.mDashCount == 0);}
    int getDashInfo(int frameNo, float *array) const;
public:
    LOTAnimatable<float>           mWidth;       /* "w" */
    CapStyle                       mCapStyle;    /* "lc" */
    JoinStyle                      mJoinStyle;   /* "lj" */
    float                          mMeterLimit{0};  /* "ml" */
    LOTDashProperty                mDash;
};

class LOTPath : public LOTData
{
public:
    LOTPath(LOTData::Type  type):LOTData(type){}
    VPath::Direction direction() { if (mDirection == 3) return VPath::Direction::CCW;
                                   else return VPath::Direction::CW;}
public:
    int                                    mDirection{1};
};

class LOTShapeData : public LOTPath
{
public:
    LOTShapeData():LOTPath(LOTData::Type::Shape){}
    void process();
public:
    LOTAnimatable<LottieShapeData>    mShape;
};

class LOTMaskData
{
public:
    enum class Mode {
      None,
      Add,
      Substarct,
      Intersect,
      Difference
    };
    float opacity(int frameNo) const {return mOpacity.value(frameNo)/100.0;}
    bool isStatic() const {return mIsStatic;}
public:
    LOTAnimatable<LottieShapeData>    mShape;
    LOTAnimatable<float>              mOpacity;
    bool                              mInv{false};
    bool                              mIsStatic{true};
    LOTMaskData::Mode                 mMode;
};

class LOTRectData : public LOTPath
{
public:
    LOTRectData():LOTPath(LOTData::Type::Rect){}
public:
    LOTAnimatable<VPointF>    mPos;
    LOTAnimatable<VPointF>    mSize;
    LOTAnimatable<float>      mRound{0};
};

class LOTEllipseData : public LOTPath
{
public:
    LOTEllipseData():LOTPath(LOTData::Type::Ellipse){}
public:
    LOTAnimatable<VPointF>   mPos;
    LOTAnimatable<VPointF>   mSize;
};

class LOTPolystarData : public LOTPath
{
public:
    enum class PolyType {
        Star = 1,
        Polygon = 2
    };
    LOTPolystarData():LOTPath(LOTData::Type::Polystar){}
public:
    LOTPolystarData::PolyType     mType{PolyType::Polygon};
    LOTAnimatable<VPointF>        mPos;
    LOTAnimatable<float>          mPointCount{0};
    LOTAnimatable<float>          mInnerRadius{0};
    LOTAnimatable<float>          mOuterRadius{0};
    LOTAnimatable<float>          mInnerRoundness{0};
    LOTAnimatable<float>          mOuterRoundness{0};
    LOTAnimatable<float>          mRotation{0};
};

class LOTTrimData : public LOTData
{
public:
    enum class TrimType {
        Simultaneously,
        Individually
    };
    LOTTrimData():LOTData(LOTData::Type::Trim){}
    float start(int frameNo) const {return mStart.value(frameNo)/100.0f;}
    float end(int frameNo) const {return mEnd.value(frameNo)/100.0f;}
    float offset(int frameNo) const {return fmod(mOffset.value(frameNo), 360.0f)/ 360.0f;}
    LOTTrimData::TrimType type() const {return mTrimType;}
public:
    LOTAnimatable<float>             mStart{0};
    LOTAnimatable<float>             mEnd{0};
    LOTAnimatable<float>             mOffset{0};
    LOTTrimData::TrimType            mTrimType{TrimType::Simultaneously};
};

class LOTRepeaterData : public LOTGroupData
{
public:
    LOTRepeaterData():LOTGroupData(LOTData::Type::Repeater){}
public:
    LOTAnimatable<float>             mCopies{0};
    LOTAnimatable<float>             mOffset{0};
};

class LOTModel
{
public:
   bool  isStatic() const {return mRoot->isStatic();}
   double duration() const {return mRoot->duration();}
   size_t frameDuration() const {return mRoot->frameDuration();}
   size_t frameRate() const {return mRoot->frameRate();}
   size_t startFrame() const {return mRoot->startFrame();}
   size_t frameAtPos(double pos) const {return mRoot->frameAtPos(pos);}
public:
    std::shared_ptr<LOTCompositionData> mRoot;
};

#endif // LOTModel_H
