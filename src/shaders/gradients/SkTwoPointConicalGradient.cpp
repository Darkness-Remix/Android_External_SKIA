/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkTwoPointConicalGradient.h"

#include "SkRasterPipeline.h"
#include "../../jumper/SkJumper.h"

sk_sp<SkShader> SkTwoPointConicalGradient::Create(const SkPoint& c0, SkScalar r0,
                                                  const SkPoint& c1, SkScalar r1,
                                                  const Descriptor& desc) {
    SkMatrix gradientMatrix;
    Type     gradientType;

    if (SkScalarNearlyZero((c0 - c1).length())) {
        // Concentric case: we can pretend we're radial (with a tiny twist).
        const SkScalar scale = 1.0f / SkTMax(r0, r1);
        gradientMatrix = SkMatrix::MakeTrans(-c1.x(), -c1.y());
        gradientMatrix.postScale(scale, scale);

        gradientType = Type::kRadial;
    } else {
        const SkPoint centers[2] = { c0    , c1     };
        const SkPoint unitvec[2] = { {0, 0}, {1, 0} };

        if (!gradientMatrix.setPolyToPoly(centers, unitvec, 2)) {
            // Degenerate case.
            return nullptr;
        }

        // General two-point case.
        gradientType = Type::kTwoPoint;
    }

    return sk_sp<SkShader>(new SkTwoPointConicalGradient(c0, r0, c1, r1, desc,
                                                         gradientType, gradientMatrix));
}

SkTwoPointConicalGradient::SkTwoPointConicalGradient(
        const SkPoint& start, SkScalar startRadius,
        const SkPoint& end, SkScalar endRadius,
        const Descriptor& desc, Type type, const SkMatrix& gradientMatrix)
    : SkGradientShaderBase(desc, gradientMatrix)
    , fCenter1(start)
    , fCenter2(end)
    , fRadius1(startRadius)
    , fRadius2(endRadius)
    , fType(type)
{
    // this is degenerate, and should be caught by our caller
    SkASSERT(fCenter1 != fCenter2 || fRadius1 != fRadius2);
}

bool SkTwoPointConicalGradient::isOpaque() const {
    // Because areas outside the cone are left untouched, we cannot treat the
    // shader as opaque even if the gradient itself is opaque.
    // TODO(junov): Compute whether the cone fills the plane crbug.com/222380
    return false;
}

// Returns the original non-sorted version of the gradient
SkShader::GradientType SkTwoPointConicalGradient::asAGradient(GradientInfo* info) const {
    if (info) {
        commonAsAGradient(info);
        info->fPoint[0] = fCenter1;
        info->fPoint[1] = fCenter2;
        info->fRadius[0] = fRadius1;
        info->fRadius[1] = fRadius2;
    }
    return kConical_GradientType;
}

sk_sp<SkFlattenable> SkTwoPointConicalGradient::CreateProc(SkReadBuffer& buffer) {
    DescriptorScope desc;
    if (!desc.unflatten(buffer)) {
        return nullptr;
    }
    SkPoint c1 = buffer.readPoint();
    SkPoint c2 = buffer.readPoint();
    SkScalar r1 = buffer.readScalar();
    SkScalar r2 = buffer.readScalar();

    if (buffer.isVersionLT(SkReadBuffer::k2PtConicalNoFlip_Version) && buffer.readBool()) {
        // legacy flipped gradient
        SkTSwap(c1, c2);
        SkTSwap(r1, r2);

        SkColor4f* colors = desc.mutableColors();
        SkScalar* pos = desc.mutablePos();
        const int last = desc.fCount - 1;
        const int half = desc.fCount >> 1;
        for (int i = 0; i < half; ++i) {
            SkTSwap(colors[i], colors[last - i]);
            if (pos) {
                SkScalar tmp = pos[i];
                pos[i] = SK_Scalar1 - pos[last - i];
                pos[last - i] = SK_Scalar1 - tmp;
            }
        }
        if (pos) {
            if (desc.fCount & 1) {
                pos[half] = SK_Scalar1 - pos[half];
            }
        }
    }

    return SkGradientShader::MakeTwoPointConical(c1, r1, c2, r2, desc.fColors,
                                                 std::move(desc.fColorSpace), desc.fPos,
                                                 desc.fCount, desc.fTileMode, desc.fGradFlags,
                                                 desc.fLocalMatrix);
}

void SkTwoPointConicalGradient::flatten(SkWriteBuffer& buffer) const {
    this->INHERITED::flatten(buffer);
    buffer.writePoint(fCenter1);
    buffer.writePoint(fCenter2);
    buffer.writeScalar(fRadius1);
    buffer.writeScalar(fRadius2);
}

#if SK_SUPPORT_GPU

#include "SkGr.h"
#include "SkTwoPointConicalGradient_gpu.h"

std::unique_ptr<GrFragmentProcessor> SkTwoPointConicalGradient::asFragmentProcessor(
        const AsFPArgs& args) const {
    SkASSERT(args.fContext);
    return Gr2PtConicalGradientEffect::Make(GrGradientEffect::CreateArgs(
            args.fContext, this, args.fLocalMatrix, fTileMode, args.fDstColorSpace));
}

#endif

sk_sp<SkShader> SkTwoPointConicalGradient::onMakeColorSpace(SkColorSpaceXformer* xformer) const {
    SkSTArray<8, SkColor> xformedColors(fColorCount);
    xformer->apply(xformedColors.begin(), fOrigColors, fColorCount);
    return SkGradientShader::MakeTwoPointConical(fCenter1, fRadius1, fCenter2, fRadius2,
                                                 xformedColors.begin(), fOrigPos, fColorCount,
                                                 fTileMode, fGradFlags, &this->getLocalMatrix());
}


#ifndef SK_IGNORE_TO_STRING
void SkTwoPointConicalGradient::toString(SkString* str) const {
    str->append("SkTwoPointConicalGradient: (");

    str->append("center1: (");
    str->appendScalar(fCenter1.fX);
    str->append(", ");
    str->appendScalar(fCenter1.fY);
    str->append(") radius1: ");
    str->appendScalar(fRadius1);
    str->append(" ");

    str->append("center2: (");
    str->appendScalar(fCenter2.fX);
    str->append(", ");
    str->appendScalar(fCenter2.fY);
    str->append(") radius2: ");
    str->appendScalar(fRadius2);
    str->append(" ");

    this->INHERITED::toString(str);

    str->append(")");
}
#endif

void SkTwoPointConicalGradient::appendGradientStages(SkArenaAlloc* alloc, SkRasterPipeline* p,
                                                     SkRasterPipeline* postPipeline) const {
    const auto dRadius = fRadius2 - fRadius1;

    if (fType == Type::kRadial) {
        p->append(SkRasterPipeline::xy_to_radius);

        // Tiny twist: radial computes a t for [0, r2], but we want a t for [r1, r2].
        auto scale =  SkTMax(fRadius1, fRadius2) / dRadius;
        auto bias  = -fRadius1 / dRadius;

        p->append_matrix(alloc, SkMatrix::Concat(SkMatrix::MakeTrans(bias, 0),
                                                 SkMatrix::MakeScale(scale, 1)));
        return;
    }

    const auto dCenter = (fCenter1 - fCenter2).length();

    // Since we've squashed the centers into a unit vector, we must also scale
    // all the coefficient variables by (1 / dCenter).
    const auto coeffA = 1 - dRadius * dRadius / (dCenter * dCenter);
    auto* ctx = alloc->make<SkJumper_2PtConicalCtx>();
    ctx->fCoeffA    = coeffA;
    ctx->fInvCoeffA = 1 / coeffA;
    ctx->fR0        = fRadius1 / dCenter;
    ctx->fDR        = dRadius  / dCenter;

    // Is the solver guaranteed to not produce degenerates?
    bool isWellBehaved = true;

    if (SkScalarNearlyZero(coeffA)) {
        // The focal point is on the edge of the end circle.
        p->append(SkRasterPipeline::xy_to_2pt_conical_linear, ctx);
        isWellBehaved = false;
    } else {
        isWellBehaved = SkScalarAbs(dRadius) >= dCenter;
        bool isFlipped = isWellBehaved && dRadius < 0;

        // We want the larger root, per spec:
        //   "For all values of ω where r(ω) > 0, starting with the value of ω nearest
        //    to positive infinity and ending with the value of ω nearest to negative
        //    infinity, draw the circumference of the circle with radius r(ω) at position
        //    (x(ω), y(ω)), with the color at ω, but only painting on the parts of the
        //    bitmap that have not yet been painted on by earlier circles in this step for
        //    this rendering of the gradient."
        // (https://html.spec.whatwg.org/multipage/canvas.html#dom-context-2d-createradialgradient)
        //
        // ... except when the gradient is flipped.
        p->append(isFlipped ? SkRasterPipeline::xy_to_2pt_conical_quadratic_min
                            : SkRasterPipeline::xy_to_2pt_conical_quadratic_max, ctx);
    }

    if (!isWellBehaved) {
        p->append(SkRasterPipeline::mask_2pt_conical_degenerates, ctx);
        postPipeline->append(SkRasterPipeline::apply_vector_mask, &ctx->fMask);
    }
}
