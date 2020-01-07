// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the OpenColorIO Project.

#include <sstream>
#include <string.h>

#include <OpenColorIO/OpenColorIO.h>

#include "BitDepthUtils.h"
#include "HashUtils.h"
#include "MathUtils.h"
#include "md5/md5.h"
#include "ops/lut1d/Lut1DOp.h"
#include "ops/lut1d/Lut1DOpData.h"
#include "ops/matrix/MatrixOp.h"
#include "ops/OpTools.h"
#include "ops/range/RangeOpData.h"

namespace OCIO_NAMESPACE
{
// Number of possible values for the Half domain.
static const unsigned long HALF_DOMAIN_REQUIRED_ENTRIES = 65536;

Lut1DOpData::Lut3by1DArray::Lut3by1DArray(HalfFlags halfFlags,
                                          unsigned long length)
{
    if (length < 2)
    {
        throw Exception("LUT 1D length needs to be at least 2.");
    }
    resize(length, getMaxColorComponents());
    fill(halfFlags);
}

Lut1DOpData::Lut3by1DArray::~Lut3by1DArray()
{
}

void Lut1DOpData::Lut3by1DArray::fill(HalfFlags halfFlags)
{
    const unsigned long dim = getLength();
    const unsigned long maxChannels = getMaxColorComponents();

    Array::Values& values = getValues();
    if (Lut1DOpData::IsInputHalfDomain(halfFlags))
    {
        for (unsigned long idx = 0; idx<dim; ++idx)
        {
            half htemp; htemp.setBits((unsigned short)idx);
            const float ftemp = static_cast<float>(htemp);

            const unsigned long row = maxChannels * idx;
            for (unsigned long channel = 0; channel<maxChannels; ++channel)
            {
                values[channel + row] = ftemp;
            }
        }

    }
    else
    {
        const float stepValue = 1.0f / ((float)dim - 1.0f);

        for (unsigned long idx = 0; idx<dim; ++idx)
        {
            const float ftemp = (float)idx * stepValue;

            const unsigned long row = maxChannels * idx;
            for (unsigned long channel = 0; channel<maxChannels; ++channel)
            {
                values[channel + row] = ftemp;
            }
        }
    }
}

void Lut1DOpData::Lut3by1DArray::resize(unsigned long length, unsigned long numColorComponents)
{
    if (length < 2)
    {
        throw Exception("LUT 1D length needs to be at least 2.");
    }
    else if (length > 1024 * 1024)
    {
        std::ostringstream oss;
        oss << "LUT 1D: Length '" << length
            << "' must not be greater than 1024x1024 (1048576).";
        throw Exception(oss.str().c_str());
    }
    Array::resize(length, numColorComponents);
}

unsigned long Lut1DOpData::Lut3by1DArray::getNumValues() const
{
    return getLength() * getMaxColorComponents();
}

bool Lut1DOpData::Lut3by1DArray::isIdentity(HalfFlags halfFlags) const
{
    // An identity LUT does nothing except possibly bit-depth conversion.
    //
    // Note: OCIO v1 had a member variable so that this check would only need
    // to be calculated once.  However, there were cases where m_isNoOp could
    // get out of sync with the LUT contents.  It also required an unfinalize
    // method.  It's not clear that this is a performance bottleneck since for
    // most LUTs, we'll return false after only a few iterations of the loop.
    // So for now we've remove the member to store the result of this computation.
    //
    const unsigned long dim = getLength();
    const Array::Values& values = getValues();
    const unsigned long maxChannels = getMaxColorComponents();

    if (Lut1DOpData::IsInputHalfDomain(halfFlags))
    {
        for (unsigned long idx = 0; idx<dim; ++idx)
        {
            half aimHalf;
            aimHalf.setBits((unsigned short)idx);

            const unsigned long row = maxChannels * idx;
            for (unsigned long channel = 0; channel<maxChannels; ++channel)
            {
                const half valHalf = values[channel + row];
                // Must be different by at least two ULPs to not be an identity.
                if (HalfsDiffer(aimHalf, valHalf, 1))
                {
                    return false;
                }
            }
        }
    }
    else
    {
        //
        // Note: OCIO v1 allowed the ability to control the tolerance and
        // set whether it is absolute or relative. There was a TODO that
        // suggested making it automatic based on the bit-depth, and that
        // is essentially what is done now since all the file readers normalize
        // the array based on the file bit-depth.
        //
        // Note: LUTs that are approximately identity transforms and
        // contain a wide range of float values should use the
        // half-domain representation.  The contents of most LUTs using 
        // this branch will hence either not be close to an identity anyway
        // or will be in units that are roughly perceptually uniform and hence
        // it is more appropriate to use an absolute error based on the
        // bit-depth rather than a fully relative error that will be both
        // too sensitive near zero and too loose at the high end.
        //
        constexpr float abs_tol = 1e-5f;
        const float stepValue = 1.0f / ((float)dim - 1.0f);

        for (unsigned long idx = 0; idx<dim; ++idx)
        {
            const float aim = (float)idx * stepValue;

            const unsigned long row = maxChannels * idx;
            for (unsigned long channel = 0; channel<maxChannels; ++channel)
            {
                const float err = values[channel + row] - aim;

                if (fabs(err) > abs_tol)
                {
                    return false;
                }
            }
        }
    }

    return true;
}

Lut1DOpData::Lut1DOpData(unsigned long dimension)
    : OpData()
    , m_interpolation(INTERP_DEFAULT)
    , m_array(LUT_STANDARD, dimension)
    , m_halfFlags(LUT_STANDARD)
    , m_hueAdjust(HUE_NONE)
    , m_direction(TRANSFORM_DIR_FORWARD)
    , m_invQuality(LUT_INVERSION_FAST)
{
}

Lut1DOpData::Lut1DOpData(unsigned long dimension, TransformDirection dir)
    : OpData()
    , m_interpolation(INTERP_DEFAULT)
    , m_array(LUT_STANDARD, dimension)
    , m_halfFlags(LUT_STANDARD)
    , m_hueAdjust(HUE_NONE)
    , m_direction(dir)
    , m_invQuality(LUT_INVERSION_FAST)
{
}

Lut1DOpData::Lut1DOpData(HalfFlags halfFlags,
                         unsigned long dimension)
    : OpData()
    , m_interpolation(INTERP_DEFAULT)
    , m_array(halfFlags, dimension)
    , m_halfFlags(halfFlags)
    , m_hueAdjust(HUE_NONE)
    , m_direction(TRANSFORM_DIR_FORWARD)
    , m_invQuality(LUT_INVERSION_FAST)
{
}

Lut1DOpData::~Lut1DOpData()
{
}

Interpolation Lut1DOpData::getConcreteInterpolation() const
{
    // TODO: currently INTERP_NEAREST is not implemented in Lut1DOpCPU.
    // This is a regression from OCIO v1.
    // NB: To have the same interpolation support (i.e. same color processing)
    // between the CPU & GPU paths, the 'Nearest' interpolation is implemented
    // using the 'Linear' even if GPU path already support the 'Nearest'
    // interpolation.
    // NB: invalid interpolation will make validate() throw.
    return INTERP_LINEAR;
}

void Lut1DOpData::setInterpolation(Interpolation algo)
{
    m_interpolation = algo;
}

void Lut1DOpData::setInversionQuality(LutInversionQuality style)
{
    m_invQuality = style;
}

bool Lut1DOpData::isIdentity() const
{
    return m_array.isIdentity(m_halfFlags);
}

bool Lut1DOpData::hasChannelCrosstalk() const
{
    if (getHueAdjust() != HUE_NONE)
    {
        // Returning !isIdentity() would be time consuming.
        return true;
    }
    else
    {
        return false;
    }
}

bool Lut1DOpData::isNoOp() const
{
    if (isInputHalfDomain())
    {
        return isIdentity();
    }
    else
    {
        return false;
    }
}

OpDataRcPtr Lut1DOpData::getIdentityReplacement() const
{
    OpDataRcPtr res;
    if (isInputHalfDomain())
    {
        res = std::make_shared<MatrixOpData>();
    }
    else
    {
        res = std::make_shared<RangeOpData>(0., 1., 0., 1.);
    }
    return res;
}

void Lut1DOpData::setInputHalfDomain(bool isHalfDomain) noexcept
{
    m_halfFlags = (isHalfDomain) ?
        ((HalfFlags)(m_halfFlags | LUT_INPUT_HALF_CODE)) :
        ((HalfFlags)(m_halfFlags & ~LUT_INPUT_HALF_CODE));
}

void Lut1DOpData::setOutputRawHalfs(bool isRawHalfs) noexcept
{
    m_halfFlags = (isRawHalfs) ?
        ((HalfFlags)(m_halfFlags | LUT_OUTPUT_HALF_CODE)) :
        ((HalfFlags)(m_halfFlags & ~LUT_OUTPUT_HALF_CODE));
}

namespace
{
bool IsValid(const Interpolation & interpolation)
{
    switch (interpolation)
    {
    case INTERP_BEST:
    case INTERP_DEFAULT:
    case INTERP_LINEAR:
    case INTERP_NEAREST:
        return true;
    case INTERP_CUBIC:
    case INTERP_TETRAHEDRAL:
    case INTERP_UNKNOWN:
    default:
        return false;
    }
}
}

void Lut1DOpData::validate() const
{
    OpData::validate();

    if (!IsValid(m_interpolation))
    {
        std::ostringstream oss;
        oss << "1D LUT does not support interpolation algorithm: ";
        oss << InterpolationToString(getInterpolation());
        oss << ".";
        throw Exception(oss.str().c_str());
    }

    try
    {
        getArray().validate();
    }
    catch (Exception& e)
    {
        std::ostringstream oss;
        oss << "1D LUT content array issue: ";
        oss << e.what();

        throw Exception(oss.str().c_str());
    }

    // If isHalfDomain is set, we need to make sure we have 65536 entries.
    if (isInputHalfDomain() && getArray().getLength()
        != HALF_DOMAIN_REQUIRED_ENTRIES)
    {
        std::ostringstream oss;
        oss << "1D LUT: ";
        oss << getArray().getLength();
        oss << " entries found, ";
        oss << HALF_DOMAIN_REQUIRED_ENTRIES;
        oss << " required for halfDomain 1D LUT.";

        throw Exception(oss.str().c_str());
    }
}

unsigned long Lut1DOpData::GetLutIdealSize(BitDepth incomingBitDepth)
{
    // Return the number of entries needed in order to do a lookup 
    // for the specified bit-depth.

    // For 32f, a look-up is impractical so in that case return 64k.

    switch (incomingBitDepth)
    {
    case BIT_DEPTH_UINT8:
    case BIT_DEPTH_UINT10:
    case BIT_DEPTH_UINT12:
    case BIT_DEPTH_UINT14:
    case BIT_DEPTH_UINT16:
        return (unsigned long)(GetBitDepthMaxValue(incomingBitDepth) + 1);

    case BIT_DEPTH_F16:
    case BIT_DEPTH_F32:
        break;

    case BIT_DEPTH_UNKNOWN:
    case BIT_DEPTH_UINT32:
    default:
    {
        std::string err("Bit-depth is not supported: ");
        err += BitDepthToString(incomingBitDepth);
        throw Exception(err.c_str());
    }

    }

    return 65536;
}

unsigned long Lut1DOpData::GetLutIdealSize(BitDepth inputBitDepth,
                                           HalfFlags halfFlags)
{
    // Returns the number of entries that fill() expects in order to make
    // an identity LUT.

    // For half domain always return 65536, since that is what fill() expects.
    // However note that if the inputBitDepth is, e.g. 10i, this might not be
    // the number of entries required for a look-up.

    const unsigned long size = HALF_DOMAIN_REQUIRED_ENTRIES;

    if (Lut1DOpData::IsInputHalfDomain(halfFlags))
    {
        return size;
    }

    return GetLutIdealSize(inputBitDepth);
}

bool Lut1DOpData::mayLookup(BitDepth incomingDepth) const
{
    if (isInputHalfDomain())
    {
        return incomingDepth == BIT_DEPTH_F16;
    }
    else  // not a half-domain LUT
    {
        if (!IsFloatBitDepth(incomingDepth))
        {
            return m_array.getLength()
                == (GetBitDepthMaxValue(incomingDepth) + 1);
        }
    }
    return false;
}

Lut1DOpDataRcPtr Lut1DOpData::MakeLookupDomain(BitDepth incomingDepth)
{
    // For integer in-depths, we need a standard domain.
    Lut1DOpData::HalfFlags domainType = Lut1DOpData::LUT_STANDARD;

    // For 16f in-depth, we need a half domain.
    // (Return same for 32f, even though a pure lookup wouldn't be appropriate.)
    if (IsFloatBitDepth(incomingDepth))
    {
        domainType = Lut1DOpData::LUT_INPUT_HALF_CODE;
    }

    const auto idealSize = GetLutIdealSize(incomingDepth, domainType);
    // Note that in this case the domainType is always appropriate for
    // the incomingDepth, so it should be safe to rely on the constructor
    // and fill() to always return the correct length.
    // (E.g., we don't need to worry about 10i with a half domain.)
    return std::make_shared<Lut1DOpData>(domainType, idealSize);
}

bool Lut1DOpData::haveEqualBasics(const Lut1DOpData & B) const
{
    // Question:  Should interpolation style be considered?
    return m_halfFlags == B.m_halfFlags &&
           m_hueAdjust == B.m_hueAdjust &&
           m_array == B.m_array;
}

bool Lut1DOpData::operator==(const OpData & other) const
{
    if (!OpData::operator==(other)) return false;

    const Lut1DOpData* lop = static_cast<const Lut1DOpData*>(&other);

    // NB: The m_invQuality is not currently included.
    if (m_direction != lop->m_direction ||
        getConcreteInterpolation() != lop->getConcreteInterpolation())
    {
        return false;
    }

    return haveEqualBasics(*lop);
}

void Lut1DOpData::setHueAdjust(Lut1DHueAdjust algo) noexcept
{
    m_hueAdjust = algo;
}

Lut1DOpDataRcPtr Lut1DOpData::clone() const
{
    // TODO: As 1D LUT could be cloned by the CPUProcessor,
    //       think about the 'bypass' behavior.

    return std::make_shared<Lut1DOpData>(*this);
}

bool Lut1DOpData::isInverse(ConstLut1DOpDataRcPtr & B) const
{
    if ((m_direction == TRANSFORM_DIR_FORWARD &&
         B->m_direction == TRANSFORM_DIR_INVERSE) ||
        (m_direction == TRANSFORM_DIR_INVERSE &&
         B->m_direction == TRANSFORM_DIR_FORWARD))
    {
        // Note: The inverse LUT 1D finalize modifies the array to make it
        // monotonic, hence, this could return false in unexpected cases.
        // However, one could argue that those LUTs should not be optimized
        // out as an identity anyway.
        return haveEqualBasics(*B);
    }
    return false;
}

bool Lut1DOpData::mayCompose(ConstLut1DOpDataRcPtr & B) const
{
    return getDirection()    == TRANSFORM_DIR_FORWARD &&
           B->getDirection() == TRANSFORM_DIR_FORWARD &&
           getHueAdjust()    == HUE_NONE              &&
           B->getHueAdjust() == HUE_NONE;
}

Lut1DOpDataRcPtr Lut1DOpData::inverse() const
{
    Lut1DOpDataRcPtr invLut = clone();

    invLut->m_direction = (m_direction == TRANSFORM_DIR_FORWARD) ?
                          TRANSFORM_DIR_INVERSE : TRANSFORM_DIR_FORWARD;

    // Note that any existing metadata could become stale at this point but
    // trying to update it is also challenging since inverse() is sometimes
    // called even during the creation of new ops.
    return invLut;
}

namespace
{
const char* GetHueAdjustName(Lut1DHueAdjust algo)
{
    switch (algo)
    {
    case HUE_DW3:
    {
        return "dw3";
    }
    case HUE_NONE:
    {
        return "none";
    }
    }
    throw Exception("1D LUT has an invalid hue adjust style.");
}

}

void Lut1DOpData::finalize()
{
    if (m_direction == TRANSFORM_DIR_INVERSE)
    {
        initializeFromForward();
    }

    m_array.adjustColorComponentNumber();

    AutoMutex lock(m_mutex);

    validate();

    md5_state_t state;
    md5_byte_t digest[16];

    md5_init(&state);
    md5_append(&state,
        (const md5_byte_t *)&(getArray().getValues()[0]),
        (int)(getArray().getValues().size() * sizeof(float)));
    md5_finish(&state, digest);

    std::ostringstream cacheIDStream;
    cacheIDStream << GetPrintableHash(digest) << " ";
    cacheIDStream << TransformDirectionToString(m_direction) << " ";
    cacheIDStream << InterpolationToString(m_interpolation) << " ";
    cacheIDStream << (isInputHalfDomain()?"half domain ":"standard domain ");
    cacheIDStream << GetHueAdjustName(m_hueAdjust);
    // NB: The m_invQuality is not currently included.

    m_cacheID = cacheIDStream.str();
}

//-----------------------------------------------------------------------------
//
// Functional composition is a concept from mathematics where two functions
// are combined into a single function.  This idea may be applied to ops
// where we generate a single op that has the same (or similar) effect as
// applying the two ops separately.  The motivation is faster processing.
//
// When composing LUTs, the algorithm produces a result which takes the
// domain of the first op into the range of the last op.  So the algorithm
// needs to render values through the ops.  In some cases the domain of
// the first op is sufficient, in other cases we need to create a new more
// finely sampled domain to try and make the result less lossy.


//-----------------------------------------------------------------------------
// Calculate a new LUT by evaluating a new domain (A) through a set of ops (B).
//
// Note1: The caller must ensure that B is separable (i.e., it has no channel
//        crosstalk).
//
// Note2: Unlike Compose(Lut1DOpDataRcPtr,Lut1DOpDataRcPtr, ), this function
//        does not try to resize the first LUT (A), so the caller needs to
//        create a suitable domain.
//
// Note3:  We do not attempt to propagate hueAdjust or bypass states.
//         These must be taken care of by the caller.
// 
// A is used as in/out parameter. As input is it the first LUT in the composition,
// as output it is the result of the composition.
void Lut1DOpData::ComposeVec(Lut1DOpDataRcPtr & A, OpRcPtrVec & B)
{
    if (B.size() == 0)
    {
        throw Exception("There is nothing to compose the 1D LUT with");
    }

    // Set up so that the eval directly fills in the array of the result LUT.

    const long numPixels = (long)A->getArray().getLength();

    // TODO: Could keep it one channel in some cases.
    A->getArray().resize(numPixels, 3);
    Array::Values & inValues = A->getArray().getValues();

    // Evaluate the transforms at 32f.
    // Note: If any ops are bypassed, that will be respected here.

    EvalTransform((const float*)(&inValues[0]),
                  (float*)(&inValues[0]),
                  numPixels,
                  B);
}

// Compose two Lut1DOpData.
//
// Note1: If either LUT uses hue adjust, composition will not give the same
// result as if they were applied sequentially.  However, we need to allow
// composition because the LUT 1D CPU renderer needs it to build the lookup
// table for the hue adjust renderer.  We could potentially do a lock object in
// that renderer to over-ride the hue adjust temporarily like in invLut1d.
// But for now, put the burdon on the caller to use Lut1DOpData::mayCompose first.
//
// Note2: Likewise ideally we would prohibit composition if hasMatchingBypass
// is false.  However, since the renderers may need to resample the LUTs,
// do not want to raise an exception or require the new domain to be dynamic.
// So again, it is up to the caller verify dynamic and bypass compatibility
// when calling this function in a more general context.
void Lut1DOpData::Compose(Lut1DOpDataRcPtr & A,
                          ConstLut1DOpDataRcPtr & B,
                          ComposeMethod compFlag)
{
    // We assume the caller has validated that A and B are forward LUTs 1D.

    // TODO: Add support for inverse LUTs.  Note that makeFastLut is currently called after
    // optimization so there is no opportunity to combine a forward LUT it produces.
    // However supporting inverse LUTs in compose would mean that situation would not arise
    // since the inverse would have been combined with another forward.

    OpRcPtrVec ops;

    unsigned long min_size = 0;
    bool needHalfDomain = false;

    switch (compFlag)
    {
    case COMPOSE_RESAMPLE_NO:
    {
        min_size = 0;
        break;
    }
    case COMPOSE_RESAMPLE_BIG:
    {
        min_size = 65536;
        break;
    }
    case COMPOSE_RESAMPLE_HD:
    {
        min_size = 65536;
        needHalfDomain = true;
        break;
    }

    // TODO: May want to add another style which is the maximum of
    //       B size (careful of half domain), and in-depth ideal size.
    }

    const unsigned long Asz = A->getArray().getLength();
    const bool goodDomain = A->isInputHalfDomain() || ((Asz >= min_size) && !needHalfDomain);
    const bool useOrigDomain = compFlag == COMPOSE_RESAMPLE_NO;

    if (!goodDomain && !useOrigDomain)
    {
        // Interpolate through both LUTs in this case (resample).
        CreateLut1DOp(ops, A, TRANSFORM_DIR_FORWARD);

        // Create identity with finer domain.

        auto metadata = A->getFormatMetadata();
        A = std::make_shared<Lut1DOpData>(needHalfDomain ? Lut1DOpData::LUT_INPUT_HALF_CODE :
                                                           Lut1DOpData::LUT_STANDARD,
                                          min_size);
        A->setInterpolation(A->getInterpolation());
        A->getFormatMetadata() = metadata;
    }

    Lut1DOpDataRcPtr bCloned = B->clone();
    CreateLut1DOp(ops, bCloned, TRANSFORM_DIR_FORWARD);

    // Create the result LUT by composing the domain through the desired ops.
    ComposeVec(A, ops);

    // Configure the metadata of the result LUT.
    // TODO:  May want to revisit metadata propagation.
    A->getFormatMetadata().combine(B->getFormatMetadata());

    // See note above: Taking these from B since the common use case is for B to be 
    // the original LUT and A to be a new domain (e.g. used in LUT1D renderers).
    // TODO: Adjust domain in Lut1D renderer to be one channel.
    A->setHueAdjust(B->getHueAdjust());
}

// The domain to use for the FastLut is a challenging problem since we don't
// know the input and output color space of the LUT.  In particular, we don't
// know if a half or normal domain would be better.  For now, we use a
// heuristic which is based on the original input bit-depth of the inverse LUT
// (the output bit-depth of the forward LUT).  (We preserve the original depth
// as a member since typically by the time this routine is called, the depth
// has been reset to 32f.)  However, there are situations where the origDepth
// is not reliable (e.g. a user creates a transform in Custom mode and exports it).
// Ultimately, the goal is to replace this with an automated algorithm that
// computes the best domain based on analysis of the curvature of the LUT.
Lut1DOpDataRcPtr Lut1DOpData::MakeFastLut1DFromInverse(ConstLut1DOpDataRcPtr & lut, bool forGPU)
{
    if (lut->getDirection() != TRANSFORM_DIR_INVERSE)
    {
        throw Exception("MakeFastLut1DFromInverse expects an inverse 1D LUT");
    }

    auto depth = lut->getFileOutputBitDepth();
    if (depth == BIT_DEPTH_UNKNOWN || depth == BIT_DEPTH_UINT14 || depth == BIT_DEPTH_UINT32)
    {
        depth = BIT_DEPTH_UINT12;
    }

    // For typical LUTs (e.g. gamma tables from ICC monitor profiles)
    // we can use a smaller FastLUT on the GPU.
    // Currently allowing 16f to be subsampled for GPU but using 16i as a way
    // to indicate not to subsample certain LUTs (e.g. float-conversion LUTs).
    if (forGPU && depth != BIT_DEPTH_UINT16)
    {
        // GPU will always interpolate rather than look-up.
        // Use a smaller table for better efficiency.

        // TODO: Investigate performance/quality trade-off.

        depth = BIT_DEPTH_UINT12;
    }

    // But if the LUT has values outside [0,1], use a half-domain fastLUT.
    // NB: This requires the lut to have been finalized.
    if (lut->hasExtendedRange())
    {
        depth = BIT_DEPTH_F16;
    }

    // Make a domain for the composed 1D LUT.
    Lut1DOpDataRcPtr newDomainLut = MakeLookupDomain(depth);

    // Change inv style to INV_EXACT to avoid recursion.
    LutStyleGuard<Lut1DOpData> guard(*lut);

    Compose(newDomainLut, lut, COMPOSE_RESAMPLE_NO);

    return newDomainLut;
}

void Lut1DOpData::scale(float scale)
{
    getArray().scale(scale);
}

void Lut1DOpData::initializeFromForward()
{
    // This routine is to be called (e.g. in XML reader) once the base forward
    // Lut1D has been created and then sets up what is needed for the invLut1D.

    // Note that if the original LUT had a half domain, the invLut needs to as
    // well so that the appropriate evaluation algorithm is called.

    // NB: The file reader must call setFileOutputBitDepth since some methods
    // need to know the original scaling of the LUT.
    prepareArray();
}

bool Lut1DOpData::hasExtendedRange() const
{
    // The forward LUT is allowed to have entries outside the outDepth (e.g. a
    // 10i LUT is allowed to have values on [-20,1050] if it wants).  This is
    // called an extended range LUT and helps maximize accuracy by allowing
    // clamping to happen (if necessary) after the interpolation.
    // The implication is that the inverse LUT needs to evaluate over an
    // extended domain.  Since this potentially requires a slower rendering
    // method for the Fast style, this method allows the renderers to determine
    // if this is necessary.

    // Note that it is the range (output) of the forward LUT that determines
    // the need for an extended domain on the inverse LUT.  Whether the forward
    // LUT has a half domain does not matter.  E.g., a Lustre float-conversion
    // LUT has a half domain but outputs integers within [0,65535] so the
    // inverse actually wants a normal 16i domain.

    const Array::Values& values = getArray().getValues();

    constexpr float normalMin = 0.0f - 1e-5f;
    constexpr float normalMax = 1.0f + 1e-5f;

    for (auto & val : values)
    {
        if (IsNan(val)) continue;
        if (val < normalMin)
        {
            return true;
        }
        if (val > normalMax)
        {
            return true;
        }
    }

    return false;
}

// NB: The half domain includes pos/neg infinity and NaNs.  
// The prepareArray makes the LUT monotonic to ensure a unique inverse and
// determines an effective domain to handle flat spots at the ends nicely.
// It's not clear how the NaN part of the domain should be included in the
// monotonicity constraints, furthermore there are 2048 NaNs that could each
// potentially have different values.  For now, the inversion algorithm and
// the pre-processing ignore the NaN part of the domain.

void Lut1DOpData::prepareArray()
{
    // Note: Data allocated for the array is length*getMaxColorComponents().
    const unsigned long length = getArray().getLength();
    const unsigned long maxChannels = getArray().getMaxColorComponents();
    const unsigned long activeChannels = getArray().getNumColorComponents();
    Array::Values& values = getArray().getValues();

    for (unsigned long c = 0; c < activeChannels; ++c)
    {
        // Determine if the LUT is overall increasing or decreasing.
        // The heuristic used is to compare first and last entries.
        // (Note flat LUTs (arbitrarily) have isIncreasing == false.)
        unsigned long lowInd = c;
        unsigned long highInd = (length - 1) * maxChannels + c;
        if (isInputHalfDomain())
        {
            // For half-domain LUTs, I am concerned that customer LUTs may not
            // correctly populate the whole domain, so using -HALF_MAX and
            // +HALF_MAX could potentially give unreliable results.
            // Just using 0 and 1 for now.
            lowInd = 0u * maxChannels + c;       // 0.0
            highInd = 15360u * maxChannels + c;  // 15360 == 1.0
        }

        {
            m_componentProperties[c].isIncreasing
                = (values[lowInd] < values[highInd]);
        }

        // Flatten reversals.
        // (If the LUT has a reversal, there is not a unique inverse.
        // Furthermore we require sorted values for the exact eval algorithm.)
        {
            bool isIncreasing = m_componentProperties[c].isIncreasing;

            if (!isInputHalfDomain())
            {
                float prevValue = values[c];
                for (unsigned long idx = c + maxChannels;
                     idx < length * maxChannels;
                     idx += maxChannels)
                {
                    if (isIncreasing != (values[idx] > prevValue))
                    {
                        values[idx] = prevValue;
                    }
                    else
                    {
                        prevValue = values[idx];
                    }
                }
            }
            else
            {
                // Do positive numbers.
                unsigned long startInd = 0u * maxChannels + c; // 0 == +zero
                unsigned long endInd = 31744u * maxChannels;   // 31744 == +infinity
                float prevValue = values[startInd];
                for (unsigned long idx = startInd + maxChannels;
                     idx <= endInd;
                     idx += maxChannels)
                {
                    if (isIncreasing != (values[idx] > prevValue))
                    {
                        values[idx] = prevValue;
                    }
                    else
                    {
                        prevValue = values[idx];
                    }
                }

                // Do negative numbers.
                isIncreasing = !isIncreasing;
                startInd = 32768u * maxChannels + c;      // 32768 == -zero
                endInd = 64512u * maxChannels;            // 64512 == -infinity
                prevValue = values[c];  // prev value for -0 is +0 (disallow overlaps)
                for (unsigned long idx = startInd; idx <= endInd; idx += maxChannels)
                {
                    if (isIncreasing != (values[idx] > prevValue))
                    {
                        values[idx] = prevValue;
                    }
                    else
                    {
                        prevValue = values[idx];
                    }
                }
            }
        }

        // Determine effective domain from the starting/ending flat spots.
        // (If the LUT begins or ends with a flat spot, the inverse should be
        // the value nearest the center of the LUT.)
        // For constant LUTs, the end domain == start domain == 0.
        {
            if (!isInputHalfDomain())
            {
                unsigned long endDomain = length - 1;
                const float endValue = values[endDomain * maxChannels + c];
                while (endDomain > 0
                    && values[(endDomain - 1) * maxChannels + c] == endValue)
                {
                    --endDomain;
                }

                unsigned long startDomain = 0;
                const float startValue = values[startDomain * maxChannels + c];
                // Note that this works for both increasing and decreasing LUTs
                // since there is no reqmt that startValue < endValue.
                while (startDomain < endDomain
                    &&  values[(startDomain + 1) * maxChannels + c] == startValue)
                {
                    ++startDomain;
                }

                m_componentProperties[c].startDomain = startDomain;
                m_componentProperties[c].endDomain = endDomain;
            }
            else
            {
                // Question: Should the value for infinity be considered for
                // interpolation? The advantage is that in theory, if infinity
                // is mapped to some value by the forward LUT, you could
                // restore that value to infinity in the inverse.
                // This does seem to work in INV_EXACT mode (e.g.
                // CPURendererInvLUT1DHalf_fclut unit test).
                // TODO: Test to be ported CPURenderer_cases.cpp_inc
                // The problem is that in INV_FAST mode, there are Infs in the fast
                // LUT and these seem to make the value for both inf and 65504
                // a NaN. Limiting the effective domain allows 65504 to invert
                // correctly.
                unsigned long endDomain = 31743u;    // +65504 = largest half value < inf
                const float endValue = values[endDomain * maxChannels + c];
                while (endDomain > 0
                    && values[(endDomain - 1) * maxChannels + c] == endValue)
                {
                    --endDomain;
                }

                unsigned long startDomain = 0;       // positive zero
                const float startValue = values[startDomain * maxChannels + c];
                // Note that this works for both increasing and decreasing LUTs
                // since there is no reqmt that startValue < endValue.
                while (startDomain < endDomain
                    &&  values[(startDomain + 1) * maxChannels + c] == startValue)
                {
                    ++startDomain;
                }

                m_componentProperties[c].startDomain = startDomain;
                m_componentProperties[c].endDomain = endDomain;

                // Negative half of domain has its own start/end.
                unsigned long negEndDomain = 64511u;  // -65504 = last value before neg inf
                const float negEndValue = values[negEndDomain * maxChannels + c];
                while (negEndDomain > 32768u     // negative zero
                    && values[(negEndDomain - 1) * maxChannels + c] == negEndValue)
                {
                    --negEndDomain;
                }

                unsigned long negStartDomain = 32768u; // negative zero
                const float negStartValue = values[negStartDomain * maxChannels + c];
                while (negStartDomain < negEndDomain
                    && values[(negStartDomain + 1) * maxChannels + c] == negStartValue)
                {
                    ++negStartDomain;
                }

                m_componentProperties[c].negStartDomain = negStartDomain;
                m_componentProperties[c].negEndDomain = negEndDomain;
            }
        }
    }

    if (activeChannels == 1)
    {
        m_componentProperties[2] = m_componentProperties[1] = m_componentProperties[0];
    }
}

} // namespace OCIO_NAMESPACE

