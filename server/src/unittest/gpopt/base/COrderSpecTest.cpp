//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		COrderSpecTest.cpp
//
//	@doc:
//		Tests for order specification
//---------------------------------------------------------------------------
#include "naucrates/md/IMDId.h"
#include "naucrates/md/IMDTypeInt4.h"
#include "naucrates/md/CMDProviderMemory.h"

#include "gpopt/base/COrderSpec.h"
#include "gpopt/base/CQueryContext.h"
#include "gpopt/base/CColumnFactory.h"
#include "gpopt/eval/CConstExprEvaluatorDefault.h"

#include "unittest/base.h"
#include "unittest/gpopt/base/COrderSpecTest.h"
#include "unittest/gpopt/translate/CTranslatorExprToDXLTest.h"
#include "unittest/gpopt/CTestUtils.h"

//---------------------------------------------------------------------------
//	@function:
//		COrderSpecTest::EresUnittest
//
//	@doc:
//		Unittest for order spec classes
//
//---------------------------------------------------------------------------
GPOS_RESULT
COrderSpecTest::EresUnittest()
{
	CUnittest rgut[] =
		{
		GPOS_UNITTEST_FUNC(COrderSpecTest::EresUnittest_Basics)
		};

	return CUnittest::EresExecute(rgut, GPOS_ARRAY_SIZE(rgut));
}

//---------------------------------------------------------------------------
//	@function:
//		COrderSpecTest::EresUnittest_Basics
//
//	@doc:
//		Basic order spec tests
//
//---------------------------------------------------------------------------
GPOS_RESULT
COrderSpecTest::EresUnittest_Basics()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();
		
	// Setup an MD cache with a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);
	
	// install opt context in TLS
	CAutoOptCtxt aoc
					(
					pmp,
					&mda,
					NULL, /* pceeval */
					CTestUtils::Pcm(pmp)
					);
	
	// get column factory from optimizer context object
	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();
	
	CWStringConst strName(GPOS_WSZ_LIT("Test Column"));
	CName name(&strName);
	
	const IMDTypeInt4 *pmdtypeint4 = mda.PtMDType<IMDTypeInt4>(CTestUtils::m_sysidDefault);		

	CColRef *pcr1 = pcf->PcrCreate(pmdtypeint4, IDefaultTypeModifier, name);
	CColRef *pcr2 = pcf->PcrCreate(pmdtypeint4, IDefaultTypeModifier, name);
	CColRef *pcr3 = pcf->PcrCreate(pmdtypeint4, IDefaultTypeModifier, name);
	
	
	COrderSpec *pos1 = GPOS_NEW(pmp) COrderSpec(pmp);
	
	IMDId *pmdidInt4LT = pmdtypeint4->PmdidCmp(IMDType::EcmptL);
	pmdidInt4LT->AddRef();
	pmdidInt4LT->AddRef();
	
	pos1->Append(pmdidInt4LT, pcr1, COrderSpec::EntFirst);
	pos1->Append(pmdidInt4LT, pcr2, COrderSpec::EntLast);

	GPOS_ASSERT(pos1->FMatch(pos1));
	GPOS_ASSERT(pos1->FSatisfies(pos1));
	
	COrderSpec *pos2 = GPOS_NEW(pmp) COrderSpec(pmp);
	pmdidInt4LT->AddRef();
	pmdidInt4LT->AddRef();
	pmdidInt4LT->AddRef();
		
	pos2->Append(pmdidInt4LT, pcr1, COrderSpec::EntFirst);
	pos2->Append(pmdidInt4LT, pcr2, COrderSpec::EntLast);
	pos2->Append(pmdidInt4LT, pcr3, COrderSpec::EntAuto);

	(void) pos1->UlHash();
	(void) pos2->UlHash();
	
	GPOS_ASSERT(pos2->FMatch(pos2));
	GPOS_ASSERT(pos2->FSatisfies(pos2));
	
	
	GPOS_ASSERT(!pos1->FMatch(pos2));
	GPOS_ASSERT(!pos2->FMatch(pos1));
	
	GPOS_ASSERT(pos2->FSatisfies(pos1));
	GPOS_ASSERT(!pos1->FSatisfies(pos2));
	
	// iterate over the components of the order spec
	for (ULONG ul = 0; ul < pos1->UlSortColumns(); ul++)
	{
#ifdef GPOS_DEBUG
		const CColRef *pcr =
#endif // GPOS_DEBUG
		pos1->Pcr(ul);
		
		GPOS_ASSERT(NULL != pcr);
		
#ifdef GPOS_DEBUG
		const IMDId *pmdid =
#endif // GPOS_DEBUG
		pos1->PmdidSortOp(ul);
		
		GPOS_ASSERT(pmdid->FValid());
		
		(void) pos1->Ent(ul);
	}
	
	pos1->Release();
	pos2->Release();

	return GPOS_OK;
}


// EOF
