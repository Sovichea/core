#include "LogicalText.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>

namespace PdfWriter
{
	namespace
	{
		CLogicalTextUnit MakeUnit(const std::u32string& text = U"A")
		{
			CLogicalTextUnit unit;
			unit.Text = text;
			unit.Glyphs.push_back({12, 0.6, 0.0, 0.0, 0.0});
			unit.VisualX = 10.0;
			unit.VisualY = 20.0;
			return unit;
		}

		CLogicalUnitPlan Plan(const CLogicalTextUnit& unit, unsigned int unitsPerEm = 1000)
		{
			CLogicalUnitPlan plan;
			CLogicalTextError error;
			EXPECT_TRUE(TryPlanLogicalTextUnit(unit, unitsPerEm, plan, error)) << error.Message;
			return plan;
		}

		void ExpectError(const CLogicalTextUnit& unit, unsigned int unitsPerEm,
		                 CLogicalTextErrorCode expectedCode,
		                 std::size_t expectedIndex = CLogicalTextError::NoItem)
		{
			CLogicalUnitPlan plan;
			CLogicalTextError error;
			EXPECT_FALSE(TryPlanLogicalTextUnit(unit, unitsPerEm, plan, error));
			EXPECT_EQ(expectedCode, error.Code);
			EXPECT_EQ(expectedIndex, error.ItemIndex);
			EXPECT_FALSE(error.Message.empty());
		}
	}

	TEST(LogicalTextPlan, PlansOneGlyph)
	{
		const CLogicalUnitPlan plan = Plan(MakeUnit());

		EXPECT_EQ(U"A", plan.Text);
		EXPECT_EQ(600, plan.Visual.AdvanceWidth);
		ASSERT_EQ(1u, plan.Visual.Components.size());
		EXPECT_EQ((CLogicalComponent{12, 0, 0}), plan.Visual.Components[0]);
		EXPECT_DOUBLE_EQ(10.0, plan.VisualX);
		EXPECT_DOUBLE_EQ(20.0, plan.VisualY);
	}

	TEST(LogicalTextPlan, PreservesOrderedMultiGlyphPositions)
	{
		CLogicalTextUnit unit = MakeUnit(U"abc");
		unit.Glyphs = {
			{40, 0.5, 0.1, 0.1, 0.2},
			{57, 0.25, -0.1, -0.05, 0.3},
			{62, 0.25, 0.0, 0.0, -0.1}
		};

		const CLogicalUnitPlan plan = Plan(unit);
		EXPECT_EQ(1000, plan.Visual.AdvanceWidth);
		EXPECT_EQ((std::vector<CLogicalComponent>{{40, 100, 200},
		                                           {57, 450, 400},
		                                           {62, 750, -100}}),
		          plan.Visual.Components);
	}

	TEST(LogicalTextPlan, NormalizesNegativeHorizontalPenMovement)
	{
		CLogicalTextUnit unit = MakeUnit(U"ab");
		unit.Glyphs = {{10, -0.25, 0.0, 0.0, 0.0},
		               {11, 0.75, 0.0, 0.0, 0.0}};

		const CLogicalUnitPlan plan = Plan(unit);
		EXPECT_EQ(750, plan.Visual.AdvanceWidth);
		EXPECT_EQ((std::vector<CLogicalComponent>{{10, 250, 0}, {11, 0, 0}}),
		          plan.Visual.Components);
		EXPECT_DOUBLE_EQ(9.75, plan.VisualX);
	}

	TEST(LogicalTextPlan, RoundsDeterministicallyToFontUnits)
	{
		CLogicalTextUnit first = MakeUnit();
		CLogicalTextUnit second = MakeUnit();
		first.Glyphs[0] = {12, 0.6001, 0.0, 0.1001, -0.1001};
		second.Glyphs[0] = {12, 0.6004, 0.0, 0.1004, -0.1004};

		EXPECT_EQ(Plan(first).Visual, Plan(second).Visual);
	}

	TEST(LogicalTextKeys, PlacementIsNotPartOfVisualIdentity)
	{
		CLogicalTextUnit first = MakeUnit();
		CLogicalTextUnit second = MakeUnit();
		second.VisualX = -100.0;
		second.VisualY = 42.0;

		EXPECT_EQ(Plan(first).Visual, Plan(second).Visual);
	}

	TEST(LogicalTextKeys, EqualTextAndVisualProduceEqualSemanticKeys)
	{
		const CSemanticUnitKey first = Plan(MakeUnit()).GetSemanticKey();
		const CSemanticUnitKey second = Plan(MakeUnit()).GetSemanticKey();
		EXPECT_EQ(first, second);
		std::map<CSemanticUnitKey, int> keys;
		keys[first] = 1;
		keys[second] = 2;
		EXPECT_EQ(1u, keys.size());
	}

	TEST(LogicalTextKeys, UnicodeIsIndependentFromVisualIdentity)
	{
		const CLogicalUnitPlan first = Plan(MakeUnit(U"A"));
		const CLogicalUnitPlan second = Plan(MakeUnit(U"B"));

		EXPECT_EQ(first.Visual, second.Visual);
		EXPECT_FALSE(first.GetSemanticKey() == second.GetSemanticKey());
	}

	TEST(LogicalTextKeys, VisualConstructionIsPartOfSemanticIdentity)
	{
		CLogicalTextUnit first = MakeUnit();
		CLogicalTextUnit second = MakeUnit();
		second.Glyphs[0].GlyphId = 13;

		EXPECT_FALSE(Plan(first).GetSemanticKey() == Plan(second).GetSemanticKey());
	}

	TEST(LogicalTextPlan, PreservesOptionalLocation)
	{
		CLogicalTextUnit unit = MakeUnit();
		unit.Location = CLogicalTextLocation{7, 12};

		const CLogicalUnitPlan plan = Plan(unit);
		ASSERT_TRUE(plan.Location.has_value());
		EXPECT_EQ((CLogicalTextLocation{7, 12}), *plan.Location);
	}

	TEST(LogicalTextValidation, RejectsEmptyTextAndGlyphs)
	{
		CLogicalTextUnit unit = MakeUnit();
		unit.Text.clear();
		ExpectError(unit, 1000, CLogicalTextErrorCode::EmptyText);

		unit = MakeUnit();
		unit.Glyphs.clear();
		ExpectError(unit, 1000, CLogicalTextErrorCode::EmptyGlyphs);
	}

	TEST(LogicalTextValidation, RejectsInvalidUnicodeScalars)
	{
		CLogicalTextUnit unit = MakeUnit(std::u32string(1, static_cast<char32_t>(0xD800)));
		ExpectError(unit, 1000, CLogicalTextErrorCode::InvalidUnicodeScalar, 0);

		unit.Text.assign(1, static_cast<char32_t>(0x110000));
		ExpectError(unit, 1000, CLogicalTextErrorCode::InvalidUnicodeScalar, 0);
	}

	TEST(LogicalTextValidation, RejectsInvalidGlyphIds)
	{
		CLogicalTextUnit unit = MakeUnit();
		unit.Glyphs[0].GlyphId = 0;
		ExpectError(unit, 1000, CLogicalTextErrorCode::InvalidGlyphId, 0);

		unit.Glyphs[0].GlyphId = 65536;
		ExpectError(unit, 1000, CLogicalTextErrorCode::InvalidGlyphId, 0);
	}

	TEST(LogicalTextValidation, RejectsNonFinitePositions)
	{
		CLogicalTextUnit unit = MakeUnit();
		unit.VisualX = std::numeric_limits<double>::quiet_NaN();
		ExpectError(unit, 1000, CLogicalTextErrorCode::NonFiniteVisualPosition);

		unit = MakeUnit();
		unit.VisualY = std::numeric_limits<double>::infinity();
		ExpectError(unit, 1000, CLogicalTextErrorCode::NonFiniteVisualPosition);
	}

	TEST(LogicalTextValidation, RejectsNonFiniteGlyphMetrics)
	{
		CLogicalTextUnit unit = MakeUnit();
		unit.Glyphs[0].XAdvance = std::numeric_limits<double>::infinity();
		ExpectError(unit, 1000, CLogicalTextErrorCode::NonFiniteGlyphMetric, 0);

		unit = MakeUnit();
		unit.Glyphs[0].YOffset = std::numeric_limits<double>::quiet_NaN();
		ExpectError(unit, 1000, CLogicalTextErrorCode::NonFiniteGlyphMetric, 0);
	}

	TEST(LogicalTextValidation, RejectsInvalidUnitsPerEm)
	{
		ExpectError(MakeUnit(), 0, CLogicalTextErrorCode::InvalidUnitsPerEm);
	}

	TEST(LogicalTextValidation, RejectsFontUnitOverflow)
	{
		CLogicalTextUnit unit = MakeUnit();
		unit.Glyphs[0].XAdvance = static_cast<double>(std::numeric_limits<int>::max());
		ExpectError(unit, 2, CLogicalTextErrorCode::FontUnitOverflow);

		unit = MakeUnit();
		unit.Glyphs[0].XOffset = -static_cast<double>(std::numeric_limits<int>::max());
		ExpectError(unit, 2, CLogicalTextErrorCode::FontUnitOverflow, 0);
	}
}
