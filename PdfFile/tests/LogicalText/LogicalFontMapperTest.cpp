#include "LogicalFontMapper.h"

#include <gtest/gtest.h>

namespace PdfWriter
{
	namespace
	{
		CLogicalUnitPlan MakePlan(const std::u32string& text = U"A",
		                          unsigned int glyphId = 12,
		                          double visualX = 10.0)
		{
			CLogicalTextUnit unit;
			unit.Text = text;
			unit.Glyphs.push_back({glyphId, 0.6, 0.0, 0.0, 0.0});
			unit.VisualX = visualX;
			unit.VisualY = 20.0;

			CLogicalUnitPlan plan;
			CLogicalTextError error;
			EXPECT_TRUE(TryPlanLogicalTextUnit(unit, 1000, plan, error)) << error.Message;
			return plan;
		}
	}

	TEST(LogicalFontMapper, ReusesIdenticalSemanticUnit)
	{
		CLogicalFontMapper mapper;
		const CLogicalFontMapping first = mapper.Map(MakePlan());
		const CLogicalFontMapping second = mapper.Map(MakePlan());

		EXPECT_EQ(1u, first.Cid);
		EXPECT_EQ(first.Cid, second.Cid);
		EXPECT_EQ(first.VisualRecordId, second.VisualRecordId);
		EXPECT_TRUE(first.SemanticCreated);
		EXPECT_TRUE(first.VisualCreated);
		EXPECT_FALSE(second.SemanticCreated);
		EXPECT_FALSE(second.VisualCreated);
		EXPECT_EQ(1u, mapper.GetShard().GetSemanticCount());
		EXPECT_EQ(1u, mapper.GetShard().GetVisualCount());
	}

	TEST(LogicalFontMapper, AllocatesDifferentCidsForDifferentUnicodeSharingOneVisual)
	{
		CLogicalFontMapper mapper;
		const CLogicalFontMapping first = mapper.Map(MakePlan(U"A"));
		const CLogicalFontMapping second = mapper.Map(MakePlan(U"B"));

		EXPECT_NE(first.Cid, second.Cid);
		EXPECT_EQ(first.VisualRecordId, second.VisualRecordId);
		EXPECT_TRUE(second.SemanticCreated);
		EXPECT_FALSE(second.VisualCreated);
		EXPECT_EQ(2u, mapper.GetShard().GetSemanticCount());
		EXPECT_EQ(1u, mapper.GetShard().GetVisualCount());

		const CLogicalCidRecord* firstRecord = mapper.GetShard().GetCidRecord(first.Cid);
		const CLogicalCidRecord* secondRecord = mapper.GetShard().GetCidRecord(second.Cid);
		ASSERT_NE(nullptr, firstRecord);
		ASSERT_NE(nullptr, secondRecord);
		EXPECT_EQ(U"A", firstRecord->Text);
		EXPECT_EQ(U"B", secondRecord->Text);
		EXPECT_EQ(first.VisualRecordId, firstRecord->VisualRecordId);
		EXPECT_EQ(first.VisualRecordId, secondRecord->VisualRecordId);
	}

	TEST(LogicalFontMapper, AllocatesDifferentVisualsForSameUnicode)
	{
		CLogicalFontMapper mapper;
		const CLogicalFontMapping first = mapper.Map(MakePlan(U"A", 12));
		const CLogicalFontMapping second = mapper.Map(MakePlan(U"A", 13));

		EXPECT_NE(first.Cid, second.Cid);
		EXPECT_NE(first.VisualRecordId, second.VisualRecordId);
		EXPECT_TRUE(second.SemanticCreated);
		EXPECT_TRUE(second.VisualCreated);
		EXPECT_EQ(2u, mapper.GetShard().GetSemanticCount());
		EXPECT_EQ(2u, mapper.GetShard().GetVisualCount());
	}

	TEST(LogicalFontMapper, UsesEveryVisualKeyDimension)
	{
		CLogicalFontMapper mapper;
		const CLogicalUnitPlan base = MakePlan();
		CLogicalUnitPlan changedAdvance = base;
		CLogicalUnitPlan changedPosition = base;
		CLogicalUnitPlan changedComponents = base;
		CLogicalUnitPlan changedOrder = base;
		changedAdvance.Visual.AdvanceWidth += 1;
		changedPosition.Visual.Components[0].X += 1;
		changedComponents.Visual.Components.push_back({13, 10, 20});
		changedOrder.Visual.Components = {{13, 10, 20}, base.Visual.Components[0]};

		const CLogicalFontMapping mappings[] = {
			mapper.Map(base),
			mapper.Map(changedAdvance),
			mapper.Map(changedPosition),
			mapper.Map(changedComponents),
			mapper.Map(changedOrder)
		};

		for (std::size_t index = 0; index < 5; ++index)
		{
			EXPECT_EQ(index + 1, mappings[index].Cid);
			EXPECT_EQ(index + 1, mappings[index].VisualRecordId);
		}
	}

	TEST(LogicalFontMapper, ExcludesPlacementAndLocationFromAllocationIdentity)
	{
		CLogicalFontMapper mapper;
		CLogicalUnitPlan firstPlan = MakePlan(U"A", 12, 10.0);
		CLogicalUnitPlan secondPlan = MakePlan(U"A", 12, -50.0);
		firstPlan.Location = CLogicalTextLocation{1, 2};
		secondPlan.VisualY = -75.0;
		secondPlan.Location = CLogicalTextLocation{100, 200};
		const CLogicalFontMapping first = mapper.Map(firstPlan);
		const CLogicalFontMapping second = mapper.Map(secondPlan);

		EXPECT_EQ(first.Cid, second.Cid);
		EXPECT_EQ(first.VisualRecordId, second.VisualRecordId);
		EXPECT_EQ(1u, mapper.GetShard().GetSemanticCount());
		EXPECT_EQ(1u, mapper.GetShard().GetVisualCount());
	}

	TEST(LogicalFontMapper, ReservesZeroIdentifiers)
	{
		CLogicalFontMapper mapper;
		EXPECT_EQ(nullptr, mapper.GetShard().GetCidRecord(0));
		EXPECT_EQ(nullptr, mapper.GetShard().GetVisualRecord(0));

		const CLogicalFontMapping mapping = mapper.Map(MakePlan());
		EXPECT_EQ(1u, mapping.Cid);
		EXPECT_EQ(1u, mapping.VisualRecordId);
		EXPECT_EQ(nullptr, mapper.GetShard().GetCidRecord(2));
		EXPECT_EQ(nullptr, mapper.GetShard().GetVisualRecord(2));
	}

	TEST(LogicalFontMapper, KeepsRecordPointersStableAcrossAllocations)
	{
		CLogicalFontMapper mapper;
		const CLogicalFontMapping first = mapper.Map(MakePlan(U"A", 12));
		const CLogicalCidRecord* cidRecord = mapper.GetShard().GetCidRecord(first.Cid);
		const CLogicalVisualRecord* visualRecord =
			mapper.GetShard().GetVisualRecord(first.VisualRecordId);
		ASSERT_NE(nullptr, cidRecord);
		ASSERT_NE(nullptr, visualRecord);

		for (unsigned int glyphId = 13; glyphId < 100; ++glyphId)
			mapper.Map(MakePlan(std::u32string(1, static_cast<char32_t>(glyphId)), glyphId));

		EXPECT_EQ(U"A", cidRecord->Text);
		EXPECT_EQ(12u, visualRecord->Visual.Components[0].GlyphId);
	}

	TEST(LogicalFontMapper, StoresUnicodeVisualReferenceAndWidthByCid)
	{
		CLogicalFontMapper mapper;
		const CLogicalFontMapping mapping = mapper.Map(MakePlan(U"AB"));
		const CLogicalCidRecord* cidRecord = mapper.GetShard().GetCidRecord(mapping.Cid);
		const CLogicalVisualRecord* visualRecord =
			mapper.GetShard().GetVisualRecord(mapping.VisualRecordId);

		ASSERT_NE(nullptr, cidRecord);
		ASSERT_NE(nullptr, visualRecord);
		EXPECT_EQ(U"AB", cidRecord->Text);
		EXPECT_EQ(mapping.VisualRecordId, cidRecord->VisualRecordId);
		EXPECT_EQ(600, cidRecord->Width);
		EXPECT_EQ(mapping.VisualRecordId, visualRecord->Id);
		EXPECT_EQ(MakePlan(U"other").Visual, visualRecord->Visual);
	}

	TEST(LogicalFontMapper, AllocatesInDeterministicEncounterOrder)
	{
		CLogicalFontMapper firstMapper;
		CLogicalFontMapper secondMapper;
		const CLogicalUnitPlan plans[] = {
			MakePlan(U"B", 12),
			MakePlan(U"A", 13),
			MakePlan(U"C", 12)
		};

		for (const CLogicalUnitPlan& plan : plans)
		{
			const CLogicalFontMapping first = firstMapper.Map(plan);
			const CLogicalFontMapping second = secondMapper.Map(plan);
			EXPECT_EQ(first.Cid, second.Cid);
			EXPECT_EQ(first.VisualRecordId, second.VisualRecordId);
		}

		EXPECT_EQ(1u, firstMapper.Map(plans[0]).Cid);
		EXPECT_EQ(2u, firstMapper.Map(plans[1]).Cid);
		EXPECT_EQ(3u, firstMapper.Map(plans[2]).Cid);
		EXPECT_EQ(1u, firstMapper.Map(plans[0]).VisualRecordId);
		EXPECT_EQ(2u, firstMapper.Map(plans[1]).VisualRecordId);
		EXPECT_EQ(1u, firstMapper.Map(plans[2]).VisualRecordId);
	}
}
