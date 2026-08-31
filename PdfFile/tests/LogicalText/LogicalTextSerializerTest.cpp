#include "LogicalTextSerializer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace PdfWriter
{
	namespace
	{
		CLogicalType0FontResult MakeFont(std::initializer_list<int> widths)
		{
			CLogicalType0FontResult font;
			font.Widths.assign(widths);
			return font;
		}

		CLogicalTextCommand MakeCommand(TLogicalCid cid,
		                                std::size_t shard,
		                                double x,
		                                double y,
		                                std::uint64_t boundary = 0,
		                                char32_t text = U'A')
		{
			CLogicalTextCommand command;
			command.Plan.Text = std::u32string{text};
			command.Plan.Visual.AdvanceWidth = 500;
			command.Plan.Visual.Components.push_back({1, 0, 0});
			command.Plan.VisualX = x;
			command.Plan.VisualY = y;
			command.Mapping.ShardIndex = shard;
			command.Mapping.FontMapping.Cid = cid;
			command.Mapping.FontMapping.VisualRecordId = 1;
			command.BoundaryId = boundary;
			return command;
		}

		std::string Serialize(const std::vector<CLogicalTextCommand>& commands,
		                      const std::vector<CLogicalTextFontResource>& resources,
		                      CLogicalTextSerializationOptions options = {})
		{
			std::string content;
			CLogicalTextSerializationError error;
			EXPECT_TRUE(TrySerializeLogicalTextCommands(commands, resources, options, content, error))
				<< error.Message;
			return content;
		}
	}

	TEST(LogicalTextSerializer, UsesOneTjForContiguousLtrSourceOrder)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 0.5, 0.0),
			 MakeCommand(3, 0, 1.0, 0.0)}, resources);

		EXPECT_EQ("/LF0 1 Tf\n1 0 0 1 0 0 Tm\n<000100020003> Tj\n", content);
	}

	TEST(LogicalTextSerializer, UsesNegativeTjAdjustmentForForwardLtrGap)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 0.6, 0.0)}, resources);

		EXPECT_NE(std::string::npos, content.find("[<0001> -100 <0002>] TJ"));
		EXPECT_LT(content.find("0001"), content.find("0002"));
	}

	TEST(LogicalTextSerializer, UsesPositiveTjAdjustmentForBackwardRtlMovement)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 1.0, 0.0, 0, static_cast<char32_t>(0x0627)),
			 MakeCommand(2, 0, 0.4, 0.0, 0, static_cast<char32_t>(0x0628)),
			 MakeCommand(3, 0, -0.2, 0.0, 0, static_cast<char32_t>(0x062C))}, resources);

		EXPECT_NE(std::string::npos, content.find("[<0001> 1100 <0002> 1100 <0003>] TJ"));
		EXPECT_LT(content.find("0001"), content.find("0002"));
		EXPECT_LT(content.find("0002"), content.find("0003"));
	}

	TEST(LogicalTextSerializer, PreservesMixedDirectionInputWithoutSortingByVisualX)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 1.2, 0.0),
			 MakeCommand(3, 0, 0.6, 0.0), MakeCommand(4, 0, 1.8, 0.0)}, resources);

		EXPECT_NE(std::string::npos,
		          content.find("[<0001> -700 <0002> 1100 <0003> -700 <0004>] TJ"));
	}

	TEST(LogicalTextSerializer, StartsNewMatrixForBaselineWithoutRepeatingFont)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 0.5, 0.25)}, resources);

		EXPECT_EQ("/LF0 1 Tf\n1 0 0 1 0 0 Tm\n<0001> Tj\n"
		          "1 0 0 1 0.5 0.25 Tm\n<0002> Tj\n", content);
	}

	TEST(LogicalTextSerializer, SwitchesShardResourcesAtSourceOrderPositions)
	{
		const CLogicalType0FontResult first = MakeFont({0, 500, 500});
		const CLogicalType0FontResult second = MakeFont({0, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &first}, {"LF1", &second}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(1, 1, 0.5, 0.0),
			 MakeCommand(2, 0, 1.0, 0.0)}, resources);

		const std::size_t firstFont = content.find("/LF0 1 Tf");
		const std::size_t secondFont = content.find("/LF1 1 Tf");
		const std::size_t returnFont = content.find("/LF0 1 Tf", firstFont + 1);
		ASSERT_NE(std::string::npos, firstFont);
		ASSERT_NE(std::string::npos, secondFont);
		ASSERT_NE(std::string::npos, returnFont);
		EXPECT_LT(firstFont, secondFont);
		EXPECT_LT(secondFont, returnFont);
		EXPECT_LT(content.find("<0001>", firstFont), content.find("<0001>", secondFont));
		EXPECT_LT(content.find("<0001>", secondFont), content.find("<0002>", returnFont));
	}

	TEST(LogicalTextSerializer, DoesNotCrossMarkedContentBoundary)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0, 10), MakeCommand(2, 0, 0.5, 0.0, 11)}, resources);

		EXPECT_EQ("/LF0 1 Tf\n1 0 0 1 0 0 Tm\n<0001> Tj\n"
		          "1 0 0 1 0.5 0 Tm\n<0002> Tj\n", content);
	}

	TEST(LogicalTextSerializer, RoundsAdjustmentToTwoDecimalsWithinTolerance)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 0.623456, 0.0)}, resources);

		EXPECT_NE(std::string::npos, content.find("[<0001> -123.46 <0002>] TJ"));
		const double reconstructedX = 0.5 - (-123.46 / 1000.0);
		EXPECT_LE(std::fabs(reconstructedX - 0.623456), 0.000005);
	}

	TEST(LogicalTextSerializer, UsesTjOnlyForExactContiguity)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 0.500001, 0.0)}, resources);

		EXPECT_NE(std::string::npos, content.find("[<0001> 0 <0002>] TJ"));
		EXPECT_EQ(std::string::npos, content.find("<00010002> Tj"));
	}

	TEST(LogicalTextSerializer, CompensatesForPriorRoundedPositionError)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 0.5000049, 0.0),
			 MakeCommand(3, 0, 1.0000098, 0.0)}, resources);

		EXPECT_NE(std::string::npos,
		          content.find("[<0001> 0 <0002> -0.01 <0003>] TJ"));
		const double reconstructedThirdX = 1.0 - (-0.01 / 1000.0);
		EXPECT_LE(std::fabs(reconstructedThirdX - 1.0000098), 0.000005);
	}

	TEST(LogicalTextSerializer, ComparesEveryBaselineWithTheEmittedGroupMatrix)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 0.5, 0.000004),
			 MakeCommand(3, 0, 1.0, 0.000008)}, resources);

		EXPECT_NE(std::string::npos, content.find("<00010002> Tj"));
		EXPECT_NE(std::string::npos, content.find("1 0 0 1 1 0.000008 Tm\n<0003> Tj"));
	}

	TEST(LogicalTextSerializer, UsesSerializedPdfWidthForCursorReconstruction)
	{
		const CLogicalType0FontResult font = MakeFont({0, 333, 333});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 1.0 / 3.0, 0.0)}, resources);

		EXPECT_NE(std::string::npos, content.find("[<0001> -0.33 <0002>] TJ"));
	}

	TEST(LogicalTextSerializer, FallsBackWhenRoundedDisplacementMissesRequiredPrecision)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		CLogicalTextSerializationOptions options;
		options.PositionTolerance = 0.0;
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 0.623456, 0.0)}, resources, options);

		EXPECT_EQ("/LF0 1 Tf\n1 0 0 1 0 0 Tm\n<0001> Tj\n"
		          "1 0 0 1 0.623456 0 Tm\n<0002> Tj\n", content);
	}

	TEST(LogicalTextSerializer, FallsBackToNewMatrixForUnsafeDisplacement)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		CLogicalTextSerializationOptions options;
		options.MaximumAbsoluteTjAdjustment = 50.0;
		const std::string content = Serialize(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(2, 0, 0.6, 0.0)}, resources, options);

		EXPECT_EQ("/LF0 1 Tf\n1 0 0 1 0 0 Tm\n<0001> Tj\n"
		          "1 0 0 1 0.6 0 Tm\n<0002> Tj\n", content);
	}

	TEST(LogicalTextSerializer, ScalesNormalizedPlacementByFontSizeAndRunOrigin)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		CLogicalTextSerializationOptions options;
		options.OriginX = 72.0;
		options.OriginY = 700.0;
		options.FontSize = 24.0;
		const std::string content = Serialize({MakeCommand(1, 0, 1.5, -0.25)}, resources, options);

		EXPECT_EQ("/LF0 24 Tf\n1 0 0 1 108 694 Tm\n<0001> Tj\n", content);
	}

	TEST(LogicalTextSerializer, RejectsFontSizeThatSerializesAsZero)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		CLogicalTextSerializationOptions options;
		options.FontSize = 0.0000004;
		std::string content = "unchanged";
		CLogicalTextSerializationError error;

		EXPECT_FALSE(TrySerializeLogicalTextCommands(
			{MakeCommand(1, 0, 0.0, 0.0)}, resources, options, content, error));
		EXPECT_EQ(CLogicalTextSerializationErrorCode::InvalidOptions, error.Code);
		EXPECT_EQ("unchanged", content);
	}

	TEST(LogicalTextSerializer, RejectsMatrixOriginOutsideRequiredPrecision)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		CLogicalTextSerializationOptions options;
		options.PositionTolerance = 0.0;
		std::string content = "unchanged";
		CLogicalTextSerializationError error;

		EXPECT_FALSE(TrySerializeLogicalTextCommands(
			{MakeCommand(1, 0, 0.1234567, 0.0)}, resources, options, content, error));
		EXPECT_EQ(CLogicalTextSerializationErrorCode::InsufficientPrecision, error.Code);
		EXPECT_EQ(0u, error.CommandIndex);
		EXPECT_EQ("unchanged", content);
	}

	TEST(LogicalTextSerializer, RejectsDuplicateShardResourceNames)
	{
		const CLogicalType0FontResult first = MakeFont({0, 500});
		const CLogicalType0FontResult second = MakeFont({0, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &first}, {"LF0", &second}};
		std::string content = "unchanged";
		CLogicalTextSerializationError error;

		EXPECT_FALSE(TrySerializeLogicalTextCommands(
			{MakeCommand(1, 0, 0.0, 0.0), MakeCommand(1, 1, 0.5, 0.0)},
			resources, {}, content, error));
		EXPECT_EQ(CLogicalTextSerializationErrorCode::InvalidResource, error.Code);
		EXPECT_EQ("unchanged", content);
	}

	TEST(LogicalTextSerializer, RejectsInvalidInputWithoutChangingOutput)
	{
		const CLogicalType0FontResult font = MakeFont({0, 500});
		const std::vector<CLogicalTextFontResource> resources = {{"LF0", &font}};
		CLogicalTextCommand command = MakeCommand(1, 0, 0.0, 0.0);
		command.Plan.VisualX = std::numeric_limits<double>::infinity();
		std::string content = "unchanged";
		CLogicalTextSerializationError error;

		EXPECT_FALSE(TrySerializeLogicalTextCommands({command}, resources, {}, content, error));
		EXPECT_EQ(CLogicalTextSerializationErrorCode::InvalidCommand, error.Code);
		EXPECT_EQ(0u, error.CommandIndex);
		EXPECT_EQ("unchanged", content);
	}
}
