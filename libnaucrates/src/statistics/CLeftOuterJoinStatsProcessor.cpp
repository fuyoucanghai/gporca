//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright 2013 EMC Corp.
//
//	@filename:
//		CLeftOuterJoinStatsProcessor.cpp
//
//	@doc:
//		Statistics helper routines
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "gpopt/base/CUtils.h"
#include "gpopt/base/CColRefTable.h"
#include "gpopt/base/CColRefSetIter.h"
#include "gpopt/exception.h"
#include "gpopt/operators/ops.h"
#include "gpopt/operators/CExpressionUtils.h"
#include "gpopt/operators/CPredicateUtils.h"
#include "gpopt/mdcache/CMDAccessor.h"
#include "gpopt/engine/CStatisticsConfig.h"
#include "gpopt/optimizer/COptimizerConfig.h"

#include "naucrates/statistics/CStatisticsUtils.h"
#include "naucrates/statistics/CLeftOuterJoinStatsProcessor.h"
#include "naucrates/statistics/CJoinStatsProcessor.h"
#include "naucrates/statistics/CStatistics.h"
#include "naucrates/statistics/CStatsPredUtils.h"
#include "naucrates/statistics/CStatsPredDisj.h"
#include "naucrates/statistics/CStatsPredConj.h"
#include "naucrates/statistics/CStatsPredLike.h"
#include "naucrates/statistics/CScaleFactorUtils.h"
#include "naucrates/statistics/CHistogram.h"

#include "naucrates/md/IMDScalarOp.h"
#include "naucrates/md/IMDType.h"
#include "naucrates/md/IMDTypeInt2.h"
#include "naucrates/md/IMDTypeInt8.h"
#include "naucrates/md/IMDTypeOid.h"
#include "naucrates/md/CMDIdColStats.h"


using namespace gpopt;
using namespace gpmd;



// helper for inner-joining histograms
// TODO: rename this function because it is used for all joins except left anti-semi join
void
CLeftOuterJoinStatsProcessor::JoinHistograms
		(
				IMemoryPool *pmp,
				const CHistogram *phist1,
				const CHistogram *phist2,
				CStatsPredJoin *pstatsjoin,
				CDouble dRows1,
				CDouble dRows2,
				CHistogram **pphist1, // output: histogram 1 after join
				CHistogram **pphist2, // output: histogram 2 after join
				CDouble *pdScaleFactor, // output: scale factor based on the join
				BOOL fEmptyInput
		)
{
	GPOS_ASSERT(NULL != phist1);
	GPOS_ASSERT(NULL != phist2);
	GPOS_ASSERT(NULL != pstatsjoin);
	GPOS_ASSERT(NULL != pphist1);
	GPOS_ASSERT(NULL != pphist2);
	GPOS_ASSERT(NULL != pdScaleFactor);

	*pdScaleFactor = 1.0;
	CStatsPred::EStatsCmpType escmpt = pstatsjoin->Escmpt();

	if (fEmptyInput)
	{
		// use Cartesian product as scale factor
		*pdScaleFactor = dRows1 * dRows2;
		*pphist1 =  GPOS_NEW(pmp) CHistogram(GPOS_NEW(pmp) DrgPbucket(pmp));
		*pphist2 =  GPOS_NEW(pmp) CHistogram(GPOS_NEW(pmp) DrgPbucket(pmp));

		return;
	}

	*pdScaleFactor = CScaleFactorUtils::DDefaultScaleFactorJoin;

	BOOL fEmptyHistograms = phist1->FEmpty() || phist2->FEmpty();

	if (fEmptyHistograms)
	{
		// if one more input has no histograms (due to lack of statistics
		// for table columns or computed columns), we estimate
		// the join cardinality to be the max of the two rows.
		// In other words, the scale factor is equivalent to the
		// min of the two rows.
		*pdScaleFactor = std::min(dRows1, dRows2);
	}
	else if (CHistogram::FSupportsJoin(escmpt))
	{
		CHistogram *phistJoin = phist1->PhistJoinNormalized
				(
						pmp,
						escmpt,
						dRows1,
						phist2,
						dRows2,
						pdScaleFactor
				);

		if (CStatsPred::EstatscmptEq == escmpt || CStatsPred::EstatscmptINDF == escmpt)
		{
			if (phist1->FScaledNDV())
			{
				phistJoin->SetNDVScaled();
			}
			*pphist1 = phistJoin;
			*pphist2 = (*pphist1)->PhistCopy(pmp);
			if (phist2->FScaledNDV())
			{
				(*pphist2)->SetNDVScaled();
			}
			return;
		}

		// note that IDF and Not Equality predicate we do not generate histograms but
		// just the scale factors.

		GPOS_ASSERT(phistJoin->FEmpty());
		GPOS_DELETE(phistJoin);

		// TODO:  Feb 21 2014, for all join condition except for "=" join predicate
		// we currently do not compute new histograms for the join columns
	}

	// not supported join operator or missing histograms,
	// copy input histograms and use default scale factor
	*pphist1 = phist1->PhistCopy(pmp);
	*pphist2 = phist2->PhistCopy(pmp);
}


//	return statistics object after performing LOJ operation with another statistics structure
CStatistics *
CLeftOuterJoinStatsProcessor::PstatsLOJ
		(
				IMemoryPool *pmp,
				const IStatistics *pistatsOuter,
				const IStatistics *pistatsInner,
				DrgPstatspredjoin *pdrgpstatspredjoin
		)
{
	GPOS_ASSERT(NULL != pistatsOuter);
	GPOS_ASSERT(NULL != pdrgpstatspredjoin);

	const CStatistics *pstatsOuter = dynamic_cast<const CStatistics *> (pistatsOuter);

	CStatistics *pstatsInnerJoin = CJoinStatsProcessor::PstatsJoinDriver
			(
					pmp,
					pstatsOuter->Pstatsconf(),
					pistatsOuter,
					pistatsInner,
					pdrgpstatspredjoin,
					IStatistics::EsjtLeftOuterJoin /* esjt */,
					true /* fIgnoreLasjHistComputation */
			);

	CDouble dRowsInnerJoin = pstatsInnerJoin->DRows();
	CDouble dRowsLASJ(1.0);

	const CStatistics *pstatsInner = dynamic_cast<const CStatistics *> (pistatsInner);

	// create a new hash map of histograms, for each column from the outer child
	// add the buckets that do not contribute to the inner join
	HMUlHist *phmulhistLOJ = pstatsOuter->PhmulhistLOJ
			(
					pmp,
					pstatsOuter,
					pstatsInner,
					pstatsInnerJoin,
					pdrgpstatspredjoin,
					dRowsInnerJoin,
					&dRowsLASJ
			);

	HMUlDouble *phmuldoubleWidth = GPOS_NEW(pmp) HMUlDouble(pmp);
	CStatisticsUtils::AddWidthInfo(pmp, pstatsInnerJoin->PHMUlDoubleWidth(), phmuldoubleWidth);

	pstatsInnerJoin->Release();

	// cardinality of LOJ is at least the cardinality of the outer child
	CDouble dRowsLOJ = std::max(pstatsOuter->DRows(), dRowsInnerJoin + dRowsLASJ);

	// create an output stats object
	CStatistics *pstatsLOJ = GPOS_NEW(pmp) CStatistics
			(
					pmp,
					phmulhistLOJ,
					phmuldoubleWidth,
					dRowsLOJ,
					pstatsOuter->FEmpty(),
					pstatsOuter->UlNumberOfPredicates()
			);

	// In the output statistics object, the upper bound source cardinality of the join column
	// cannot be greater than the upper bound source cardinality information maintained in the input
	// statistics object. Therefore we choose CStatistics::EcbmMin the bounding method which takes
	// the minimum of the cardinality upper bound of the source column (in the input hash map)
	// and estimated join cardinality.

	// modify source id to upper bound card information
	CJoinStatsProcessor::ComputeCardUpperBounds(pmp, pstatsInner, pstatsLOJ, dRowsLOJ, CStatistics::EcbmMin /* ecbm */);

	return pstatsLOJ;
}

// EOF
