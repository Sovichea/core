#include "LogicalMetafileAdapter.h"
#include "LogicalFontSharding.h"
#include "LogicalTextSerializer.h"
#include "LogicalType0Font.h"
#include "LogicalUnitMetafile.h"
#include "MetafileToRendererReader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace PdfWriter
{
	namespace
	{
		std::vector<std::uint8_t> ReadSourceFont()
		{
			const std::string path = std::string(EO_CORE_ROOT_DIR) +
				"/../core-fonts/dejavu/DejaVuSans.ttf";
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				return {};
			return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
			                                 std::istreambuf_iterator<char>());
		}

		CRendererLogicalUnit MakeTransport(std::uint32_t scalar,
		                                   std::uint32_t gid,
		                                   std::uint16_t advance,
		                                   double x)
		{
			CRendererLogicalUnit unit;
			unit.Unicode = {scalar};
			unit.LogicalAdvance = static_cast<double>(advance) / 2048.0;
			unit.VisualX = x;
			unit.VisualY = 0.0;
			unit.Components = {{gid, 0.0, 0.0}};
			return unit;
		}
	}

	TEST(LogicalMetafileAdapter, ConvertsRendererOffsetsToTrueTypeCoordinates)
	{
		CRendererLogicalUnit unit;
		unit.Unicode = {0x1780u, 0x17B7u};
		unit.LogicalAdvance = 1.0;
		unit.Components = {{1, 0.25, -0.5}, {2, -0.75, 1.25}};

		CLogicalUnitPlan plan;
		CLogicalMetafileAdapterError error;
		ASSERT_TRUE(TryPlanLogicalMetafileUnit(unit, 1000, plan, error)) << error.Message;
		ASSERT_EQ(2u, plan.Visual.Components.size());
		EXPECT_EQ(250, plan.Visual.Components[0].X);
		EXPECT_EQ(500, plan.Visual.Components[0].Y);
		EXPECT_EQ(-750, plan.Visual.Components[1].X);
		EXPECT_EQ(-1250, plan.Visual.Components[1].Y);
	}

	TEST(LogicalMetafileAdapter, DrivesCompleteNativeRecordToLogicalPdfPipeline)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		std::uint16_t latinAdvance = 0;
		std::uint16_t arabicAdvance = 0;
		CLogicalTrueTypeSubsetError advanceError;
		ASSERT_TRUE(TryGetTrueTypeGlyphAdvance(source, 36, latinAdvance, advanceError))
			<< advanceError.Message;
		ASSERT_TRUE(TryGetTrueTypeGlyphAdvance(source, 1366, arabicAdvance, advanceError))
			<< advanceError.Message;

		const std::vector<CRendererLogicalUnit> input = {
			MakeTransport(0x41u, 36, latinAdvance, 0.0),
			MakeTransport(0x0628u, 1366, arabicAdvance, 1.25)};
		std::vector<CLogicalUnitPlan> plans;
		for (const CRendererLogicalUnit& unit : input)
		{
			std::vector<unsigned char> record;
			NSOnlineOfficeBinToPdf::CLogicalUnitRecordError recordError;
			ASSERT_TRUE(NSOnlineOfficeBinToPdf::SerializeLogicalUnitRecord(unit, record, &recordError))
				<< recordError.Message;
			NSOnlineOfficeBinToPdf::CBufferReader reader(
				record.data(), static_cast<int>(record.size()));
			const unsigned char* payload = nullptr;
			std::size_t payloadSize = 0;
			ASSERT_TRUE(reader.TryReadBoundedRecord(
				NSOnlineOfficeBinToPdf::MaximumLogicalUnitRecordBytes, payload, payloadSize));
			CRendererLogicalUnit parsed;
			ASSERT_EQ(NSOnlineOfficeBinToPdf::ELogicalUnitRecordResult::Parsed,
			          NSOnlineOfficeBinToPdf::ParseLogicalUnitRecord(
				          payload, payloadSize, parsed, &recordError))
				<< recordError.Message;
			CLogicalUnitPlan plan;
			CLogicalMetafileAdapterError adapterError;
			ASSERT_TRUE(TryPlanLogicalMetafileUnit(parsed, 2048, plan, adapterError))
				<< adapterError.Message;
			plans.push_back(std::move(plan));
		}

		ASSERT_EQ(U"A", plans[0].Text);
		ASSERT_EQ(std::u32string{static_cast<char32_t>(0x0628)}, plans[1].Text);
		EXPECT_EQ(36u, plans[0].Visual.Components[0].GlyphId);
		EXPECT_EQ(1366u, plans[1].Visual.Components[0].GlyphId);

		CShardedLogicalFontMapper mapper(source, 1);
		std::vector<CLogicalTextCommand> commands;
		for (const CLogicalUnitPlan& plan : plans)
		{
			CShardedLogicalFontMapping mapping;
			CLogicalFontShardingError error;
			ASSERT_TRUE(mapper.TryMap(plan, mapping, error)) << error.Message;
			commands.push_back({plan, mapping, 0});
		}
		ASSERT_EQ(2u, mapper.GetShardCount());

		std::vector<CLogicalType0FontResult> fonts(2);
		for (std::size_t index = 0; index < fonts.size(); ++index)
		{
			CLogicalType0FontError error;
			ASSERT_TRUE(TryBuildLogicalType0Font(source, *mapper.GetShard(index), fonts[index], error))
				<< error.Message;
		}
		EXPECT_NE(std::string::npos, fonts[0].ToUnicode.find("<0001> <0041>"));
		EXPECT_NE(std::string::npos, fonts[1].ToUnicode.find("<0001> <0628>"));

		const std::vector<CLogicalTextFontResource> resources = {
			{"LF0", &fonts[0]}, {"LF1", &fonts[1]}};
		std::string content;
		CLogicalTextSerializationError error;
		ASSERT_TRUE(TrySerializeLogicalTextCommands(commands, resources, {}, content, error))
			<< error.Message;
		EXPECT_LT(content.find("/LF0 1 Tf"), content.find("/LF1 1 Tf"));
		EXPECT_NE(std::string::npos, content.find("<0001> Tj"));
	}
}
