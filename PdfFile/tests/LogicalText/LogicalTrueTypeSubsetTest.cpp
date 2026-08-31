#include "LogicalTrueTypeSubset.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace PdfWriter
{
	namespace
	{
		constexpr std::uint32_t MakeTag(char a, char b, char c, char d)
		{
			return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24) |
			       (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16) |
			       (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8) |
			       static_cast<std::uint32_t>(static_cast<unsigned char>(d));
		}

		std::uint16_t ReadU16(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) |
			                                  static_cast<std::uint16_t>(data[offset + 1]));
		}

		std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return (static_cast<std::uint32_t>(data[offset]) << 24) |
			       (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
			       (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
			       static_cast<std::uint32_t>(data[offset + 3]);
		}

		void WriteU16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value)
		{
			data[offset] = static_cast<std::uint8_t>(value >> 8);
			data[offset + 1] = static_cast<std::uint8_t>(value);
		}

		void WriteU32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value)
		{
			data[offset] = static_cast<std::uint8_t>(value >> 24);
			data[offset + 1] = static_cast<std::uint8_t>(value >> 16);
			data[offset + 2] = static_cast<std::uint8_t>(value >> 8);
			data[offset + 3] = static_cast<std::uint8_t>(value);
		}

		void AppendU16(std::vector<std::uint8_t>& data, std::uint16_t value)
		{
			data.push_back(static_cast<std::uint8_t>(value >> 8));
			data.push_back(static_cast<std::uint8_t>(value));
		}

		void AppendU32(std::vector<std::uint8_t>& data, std::uint32_t value)
		{
			data.push_back(static_cast<std::uint8_t>(value >> 24));
			data.push_back(static_cast<std::uint8_t>(value >> 16));
			data.push_back(static_cast<std::uint8_t>(value >> 8));
			data.push_back(static_cast<std::uint8_t>(value));
		}

		std::vector<std::uint8_t> MakeSimpleGlyph()
		{
			std::vector<std::uint8_t> glyph(12, 0);
			return glyph;
		}

		std::vector<std::uint8_t> MakeCompositeGlyph(const std::vector<std::uint16_t>& components)
		{
			std::vector<std::uint8_t> glyph(10, 0);
			WriteU16(glyph, 0, 0xFFFFu);
			for (std::size_t index = 0; index < components.size(); ++index)
			{
				std::uint16_t flags = 0x0003u;
				if (index + 1 < components.size())
					flags |= 0x0020u;
				AppendU16(glyph, flags);
				AppendU16(glyph, components[index]);
				AppendU16(glyph, 0);
				AppendU16(glyph, 0);
			}
			return glyph;
		}

		std::vector<std::uint8_t> BuildTestFont(std::uint16_t numGlyphs,
		                                        bool includeNestedComposite,
		                                        bool includeCompositeChain = false,
		                                        bool includeVariableTable = false,
		                                        bool includeCompositeCycle = false)
		{
			std::vector<std::uint8_t> glyf;
			std::vector<std::uint8_t> loca;
			std::vector<std::uint8_t> hmtx;
			for (std::uint32_t gid = 0; gid < numGlyphs; ++gid)
			{
				AppendU32(loca, static_cast<std::uint32_t>(glyf.size()));
				std::vector<std::uint8_t> glyph;
				if (includeCompositeCycle && gid + 1 == numGlyphs)
					glyph = MakeCompositeGlyph({static_cast<std::uint16_t>(gid)});
				else if (includeCompositeChain && gid == 0)
					glyph = MakeSimpleGlyph();
				else if (includeCompositeChain)
					glyph = MakeCompositeGlyph({static_cast<std::uint16_t>(gid - 1)});
				else if (includeNestedComposite && (gid == 1 || gid == 2))
					glyph = MakeSimpleGlyph();
				else if (includeNestedComposite && gid == 3)
					glyph = MakeCompositeGlyph({1, 2});
				else if (includeNestedComposite && gid == 4)
					glyph = MakeCompositeGlyph({3});
				glyf.insert(glyf.end(), glyph.begin(), glyph.end());
				AppendU16(hmtx, static_cast<std::uint16_t>(500 + gid % 100));
				AppendU16(hmtx, 0);
			}
			AppendU32(loca, static_cast<std::uint32_t>(glyf.size()));

			std::vector<std::uint8_t> head(54, 0);
			WriteU32(head, 0, 0x00010000u);
			WriteU32(head, 12, 0x5F0F3CF5u);
			WriteU16(head, 18, 1000);
			WriteU16(head, 50, 1);
			std::vector<std::uint8_t> hhea(36, 0);
			WriteU32(hhea, 0, 0x00010000u);
			WriteU16(hhea, 34, numGlyphs);
			std::vector<std::uint8_t> maxp(32, 0);
			WriteU32(maxp, 0, 0x00010000u);
			WriteU16(maxp, 4, numGlyphs);

			std::map<std::uint32_t, std::vector<std::uint8_t>> tables;
			tables.emplace(MakeTag('g', 'l', 'y', 'f'), std::move(glyf));
			tables.emplace(MakeTag('h', 'e', 'a', 'd'), std::move(head));
			tables.emplace(MakeTag('h', 'h', 'e', 'a'), std::move(hhea));
			tables.emplace(MakeTag('h', 'm', 't', 'x'), std::move(hmtx));
			tables.emplace(MakeTag('l', 'o', 'c', 'a'), std::move(loca));
			tables.emplace(MakeTag('m', 'a', 'x', 'p'), std::move(maxp));
			if (includeVariableTable)
				tables.emplace(MakeTag('f', 'v', 'a', 'r'), std::vector<std::uint8_t>(16, 0));

			const std::size_t directorySize = 12 + tables.size() * 16;
			std::size_t size = directorySize;
			for (const auto& table : tables)
				size += (table.second.size() + 3) & ~static_cast<std::size_t>(3);
			std::vector<std::uint8_t> font(size, 0);
			WriteU32(font, 0, 0x00010000u);
			WriteU16(font, 4, static_cast<std::uint16_t>(tables.size()));
			std::size_t record = 12;
			std::size_t offset = directorySize;
			for (const auto& table : tables)
			{
				WriteU32(font, record, table.first);
				WriteU32(font, record + 8, static_cast<std::uint32_t>(offset));
				WriteU32(font, record + 12, static_cast<std::uint32_t>(table.second.size()));
				std::copy(table.second.begin(), table.second.end(),
				          font.begin() + static_cast<std::ptrdiff_t>(offset));
				record += 16;
				offset += (table.second.size() + 3) & ~static_cast<std::size_t>(3);
			}
			return font;
		}

		std::vector<std::uint8_t> ReadFile(const std::string& path)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				return {};
			return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
			                                 std::istreambuf_iterator<char>());
		}

		CLogicalFontMapping MapSourceGlyph(CLogicalFontMapper& mapper,
		                                     const std::u32string& text,
		                                     std::uint32_t glyphId,
		                                     int advanceWidth,
		                                     int x = 0,
		                                     int y = 0)
		{
			CLogicalUnitPlan plan;
			plan.Text = text;
			plan.Visual.AdvanceWidth = advanceWidth;
			plan.Visual.Components.push_back({glyphId, x, y});
			return mapper.Map(plan);
		}

		std::map<std::uint32_t, std::pair<std::size_t, std::size_t>> ReadTables(
			const std::vector<std::uint8_t>& font)
		{
			std::map<std::uint32_t, std::pair<std::size_t, std::size_t>> tables;
			const std::uint16_t count = ReadU16(font, 4);
			for (std::uint16_t index = 0; index < count; ++index)
			{
				const std::size_t record = 12 + static_cast<std::size_t>(index) * 16;
				tables.emplace(ReadU32(font, record),
				               std::make_pair(ReadU32(font, record + 8), ReadU32(font, record + 12)));
			}
			return tables;
		}

		std::uint32_t FontChecksum(const std::vector<std::uint8_t>& font)
		{
			std::uint32_t sum = 0;
			for (std::size_t offset = 0; offset < font.size(); offset += 4)
			{
				std::uint32_t word = 0;
				for (std::size_t byte = 0; byte < 4 && offset + byte < font.size(); ++byte)
					word |= static_cast<std::uint32_t>(font[offset + byte]) << (24 - byte * 8);
				sum += word;
			}
			return sum;
		}

		void WriteSubsetFixture(const std::string& name, const std::vector<std::uint8_t>& font)
		{
			std::ofstream stream(name, std::ios::binary);
			ASSERT_TRUE(stream.good());
			stream.write(reinterpret_cast<const char*>(font.data()),
			             static_cast<std::streamsize>(font.size()));
			ASSERT_TRUE(stream.good());
		}

		CLogicalTrueTypeSubsetResult BuildSubset(const std::vector<std::uint8_t>& source,
		                                         const CLogicalFontShard& shard)
		{
			CLogicalTrueTypeSubsetResult result;
			CLogicalTrueTypeSubsetError error;
			EXPECT_TRUE(TryBuildSourceBackedLogicalTrueType(source, shard, result, error))
				<< error.Message;
			return result;
		}
	}

	TEST(LogicalTrueTypeSubset, CompactsNestedCompositeClosureAndSharesVisualGid)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(5, true);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 4, 504);
		MapSourceGlyph(mapper, U"B", 4, 504);

		const CLogicalTrueTypeSubsetResult result = BuildSubset(source, mapper.GetShard());
		EXPECT_EQ(1000u, result.UnitsPerEm);
		EXPECT_EQ(0u, result.SourceGidToSubsetGid[0]);
		EXPECT_EQ(1u, result.SourceGidToSubsetGid[4]);
		EXPECT_NE(CLogicalTrueTypeSubsetResult::UnmappedGlyph, result.SourceGidToSubsetGid[3]);
		EXPECT_NE(CLogicalTrueTypeSubsetResult::UnmappedGlyph, result.SourceGidToSubsetGid[1]);
		EXPECT_NE(CLogicalTrueTypeSubsetResult::UnmappedGlyph, result.SourceGidToSubsetGid[2]);
		EXPECT_EQ(result.CidToSubsetGid[1], result.CidToSubsetGid[2]);
		EXPECT_EQ(1u, result.CidToSubsetGid[1]);

		const auto tables = ReadTables(result.FontData);
		const auto maxp = tables.at(MakeTag('m', 'a', 'x', 'p'));
		EXPECT_EQ(5u, ReadU16(result.FontData, maxp.first + 4));
		EXPECT_EQ(0xB1B0AFBAu, FontChecksum(result.FontData));
		WriteSubsetFixture("phase3-nested-composite.ttf", result.FontData);
	}

	TEST(LogicalTrueTypeSubset, CompactsHighSourceGid)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(65001, false);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 65000, 500);

		const CLogicalTrueTypeSubsetResult result = BuildSubset(source, mapper.GetShard());
		EXPECT_EQ(1u, result.SourceGidToSubsetGid[65000]);
		const auto tables = ReadTables(result.FontData);
		EXPECT_EQ(2u, ReadU16(result.FontData, tables.at(MakeTag('m', 'a', 'x', 'p')).first + 4));
		EXPECT_LT(result.FontData.size(), source.size());
	}

	TEST(LogicalTrueTypeSubset, SupportsCommittedLongLocaFont)
	{
		const std::string path = std::string(EO_CORE_ROOT_DIR) +
			"/DesktopEditor/freetype-2.10.4/docs/reference/assets/fonts/specimen/FontAwesome.ttf";
		const std::vector<std::uint8_t> source = ReadFile(path);
		ASSERT_FALSE(source.empty());
		std::uint16_t advance = 0;
		CLogicalTrueTypeSubsetError error;
		ASSERT_TRUE(TryGetTrueTypeGlyphAdvance(source, 1, advance, error)) << error.Message;
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 1, advance);

		const CLogicalTrueTypeSubsetResult result = BuildSubset(source, mapper.GetShard());
		EXPECT_EQ(1u, result.SourceGidToSubsetGid[1]);
		EXPECT_EQ(0xB1B0AFBAu, FontChecksum(result.FontData));
		WriteSubsetFixture("phase3-fontawesome.ttf", result.FontData);
	}

	TEST(LogicalTrueTypeSubset, SupportsCommittedShortLocaFontAndDropsLayoutTables)
	{
		const std::string path = std::string(EO_CORE_ROOT_DIR) +
			"/DesktopEditor/freetype-2.10.4/docs/reference/assets/fonts/specimen/MaterialIcons-Regular.ttf";
		const std::vector<std::uint8_t> source = ReadFile(path);
		ASSERT_FALSE(source.empty());
		std::uint16_t advance = 0;
		CLogicalTrueTypeSubsetError error;
		ASSERT_TRUE(TryGetTrueTypeGlyphAdvance(source, 1, advance, error)) << error.Message;
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 1, advance);

		const CLogicalTrueTypeSubsetResult result = BuildSubset(source, mapper.GetShard());
		const auto tables = ReadTables(result.FontData);
		EXPECT_EQ(1u, ReadU16(result.FontData, tables.at(MakeTag('h', 'e', 'a', 'd')).first + 50));
		EXPECT_EQ(0u, tables.count(MakeTag('G', 'S', 'U', 'B')));
		EXPECT_EQ(0u, tables.count(MakeTag('G', 'P', 'O', 'S')));
		EXPECT_EQ(0u, tables.count(MakeTag('G', 'D', 'E', 'F')));
		EXPECT_EQ(0xB1B0AFBAu, FontChecksum(result.FontData));
		WriteSubsetFixture("phase3-material-icons.ttf", result.FontData);
	}

	TEST(LogicalTrueTypeSubset, RejectsNonSourceBackedVisualsAndChangedAdvance)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(5, true);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 1, 501, 1, 0);
		CLogicalTrueTypeSubsetResult result;
		CLogicalTrueTypeSubsetError error;
		EXPECT_FALSE(TryBuildSourceBackedLogicalTrueType(source, mapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::UnsupportedVisual, error.Code);

		CLogicalFontMapper changedAdvanceMapper;
		MapSourceGlyph(changedAdvanceMapper, U"A", 1, 999);
		EXPECT_FALSE(TryBuildSourceBackedLogicalTrueType(
			source, changedAdvanceMapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::AdvanceWidthMismatch, error.Code);
	}

	TEST(LogicalTrueTypeSubset, RejectsSourceGidZeroAndUnsupportedFontFlavor)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(5, true);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 0, 500);
		CLogicalTrueTypeSubsetResult result;
		CLogicalTrueTypeSubsetError error;
		EXPECT_FALSE(TryBuildSourceBackedLogicalTrueType(source, mapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::InvalidSourceGlyph, error.Code);

		std::vector<std::uint8_t> cff = source;
		WriteU32(cff, 0, MakeTag('O', 'T', 'T', 'O'));
		CLogicalFontMapper validMapper;
		MapSourceGlyph(validMapper, U"A", 1, 501);
		EXPECT_FALSE(TryBuildSourceBackedLogicalTrueType(cff, validMapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::UnsupportedFontFlavor, error.Code);
	}

	TEST(LogicalTrueTypeSubset, RejectsMalformedDirectoryAndOddLocaOffset)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(5, true);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 1, 501);
		CLogicalTrueTypeSubsetResult result;
		CLogicalTrueTypeSubsetError error;

		std::vector<std::uint8_t> malformedDirectory = source;
		WriteU32(malformedDirectory, 20, 12);
		EXPECT_FALSE(TryBuildSourceBackedLogicalTrueType(
			malformedDirectory, mapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::MalformedSfnt, error.Code);

		std::vector<std::uint8_t> oddLoca = source;
		const auto tables = ReadTables(oddLoca);
		WriteU32(oddLoca, tables.at(MakeTag('l', 'o', 'c', 'a')).first + 8, 11);
		EXPECT_FALSE(TryBuildSourceBackedLogicalTrueType(oddLoca, mapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::MalformedLoca, error.Code);
	}

	TEST(LogicalTrueTypeSubset, RejectsVariableFonts)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(5, false, false, true);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 1, 501);
		CLogicalTrueTypeSubsetResult result;
		CLogicalTrueTypeSubsetError error;

		EXPECT_FALSE(TryBuildSourceBackedLogicalTrueType(source, mapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::UnsupportedVariableFont, error.Code);
	}

	TEST(LogicalTrueTypeSubset, HandlesDeepCompositeClosureWithoutRecursion)
	{
		const std::uint16_t glyphCount = 4096;
		const std::vector<std::uint8_t> source = BuildTestFont(glyphCount, false, true);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", glyphCount - 1, 595);

		const CLogicalTrueTypeSubsetResult result = BuildSubset(source, mapper.GetShard());
		EXPECT_EQ(glyphCount, result.SourceGidToSubsetGid.size());
		EXPECT_NE(CLogicalTrueTypeSubsetResult::UnmappedGlyph, result.SourceGidToSubsetGid[0]);
	}

	TEST(LogicalTrueTypeSubset, RejectsCompositeDependencyCycle)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(5, false, false, false, true);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 4, 504);
		CLogicalTrueTypeSubsetResult result;
		CLogicalTrueTypeSubsetError error;

		EXPECT_FALSE(TryBuildSourceBackedLogicalTrueType(source, mapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalTrueTypeSubsetErrorCode::CompositeCycle, error.Code);
	}

	TEST(LogicalTrueTypeSubset, ProducesDeterministicFontBytesAndMappings)
	{
		const std::vector<std::uint8_t> source = BuildTestFont(5, true);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 4, 504);
		const CLogicalTrueTypeSubsetResult first = BuildSubset(source, mapper.GetShard());
		const CLogicalTrueTypeSubsetResult second = BuildSubset(source, mapper.GetShard());

		EXPECT_EQ(first.FontData, second.FontData);
		EXPECT_EQ(first.SourceGidToSubsetGid, second.SourceGidToSubsetGid);
		EXPECT_EQ(first.VisualRecordToSubsetGid, second.VisualRecordToSubsetGid);
		EXPECT_EQ(first.CidToSubsetGid, second.CidToSubsetGid);
	}
}
