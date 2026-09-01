#include "LogicalType0Font.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace PdfWriter
{
	namespace
	{
		std::vector<std::uint8_t> ReadSourceFont()
		{
			const std::string path = std::string(EO_CORE_ROOT_DIR) +
				"/DesktopEditor/freetype-2.10.4/docs/reference/assets/fonts/specimen/FontAwesome.ttf";
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				return {};
			return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
			                                 std::istreambuf_iterator<char>());
		}

		std::uint16_t GetAdvance(const std::vector<std::uint8_t>& source, std::uint32_t glyphId = 1)
		{
			std::uint16_t advance = 0;
			CLogicalTrueTypeSubsetError error;
			EXPECT_TRUE(TryGetTrueTypeGlyphAdvance(source, glyphId, advance, error)) << error.Message;
			return advance;
		}

		void MapSourceGlyph(CLogicalFontMapper& mapper,
		                    const std::u32string& text,
		                    std::uint32_t glyphId,
		                    int advance)
		{
			CLogicalUnitPlan plan;
			plan.Text = text;
			plan.Visual.AdvanceWidth = advance;
			plan.Visual.Components.push_back({glyphId, 0, 0});
			mapper.Map(plan);
		}

		void MapSyntheticGlyph(CLogicalFontMapper& mapper,
		                       const std::u32string& text,
		                       int advance,
		                       std::vector<CLogicalComponent> components)
		{
			CLogicalUnitPlan plan;
			plan.Text = text;
			plan.Visual.AdvanceWidth = advance;
			plan.Visual.Components = std::move(components);
			mapper.Map(plan);
		}

		CLogicalType0FontResult BuildType0(const std::vector<std::uint8_t>& source,
		                                   const CLogicalFontShard& shard)
		{
			CLogicalType0FontResult result;
			CLogicalType0FontError error;
			EXPECT_TRUE(TryBuildLogicalType0Font(source, shard, result, error)) << error.Message;
			return result;
		}

		std::string MakeStream(const std::string& attributes, const std::string& data)
		{
			return "<< /Length " + std::to_string(data.size()) + attributes +
			       " >>\nstream\n" + data + "\nendstream";
		}

		std::string BytesToString(const std::vector<std::uint8_t>& bytes)
		{
			return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		}

		void WritePdfFixture(const CLogicalType0FontResult& font,
		                     const std::string& fileName,
		                     const std::string& subsetName,
		                     const std::string& codes)
		{
			std::ostringstream widths;
			widths << "/W [1 [";
			for (std::size_t cid = 1; cid < font.Widths.size(); ++cid)
				widths << font.Widths[cid] << ' ';
			widths << "]]";

			const std::string content = "BT\n/F1 24 Tf\n72 700 Td\n<" + codes + "> Tj\nET\n";
			std::vector<std::string> objects(11);
			objects[1] = "<< /Type /Catalog /Pages 2 0 R >>";
			objects[2] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
			objects[3] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
			             "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>";
			objects[4] = MakeStream("", content);
			objects[5] = "<< /Type /Font /Subtype /Type0 /BaseFont /" + subsetName +
			             " /Encoding /Identity-H /DescendantFonts [6 0 R] /ToUnicode 10 0 R >>";
			objects[6] = "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" + subsetName +
			             " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
			             "/FontDescriptor 7 0 R /CIDToGIDMap 9 0 R /DW 1000 " + widths.str() + " >>";
			objects[7] = "<< /Type /FontDescriptor /FontName /" + subsetName + " /Flags 4 "
			             "/FontBBox [-1000 -1000 2000 2000] /ItalicAngle 0 /Ascent 1000 "
			             "/Descent -200 /CapHeight 700 /StemV 80 /FontFile2 8 0 R >>";
			objects[8] = MakeStream(" /Length1 " + std::to_string(font.FontFile2.size()),
			                        BytesToString(font.FontFile2));
			objects[9] = MakeStream("", BytesToString(font.CIDToGIDMap));
			objects[10] = MakeStream("", font.ToUnicode);

			std::string pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
			std::vector<std::size_t> offsets(objects.size(), 0);
			for (std::size_t index = 1; index < objects.size(); ++index)
			{
				offsets[index] = pdf.size();
				pdf += std::to_string(index) + " 0 obj\n" + objects[index] + "\nendobj\n";
			}
			const std::size_t xrefOffset = pdf.size();
			std::ostringstream xref;
			xref << "xref\n0 " << objects.size() << "\n"
			     << "0000000000 65535 f \n";
			for (std::size_t index = 1; index < objects.size(); ++index)
				xref << std::setw(10) << std::setfill('0') << offsets[index] << " 00000 n \n";
			xref << "trailer\n<< /Size " << objects.size() << " /Root 1 0 R >>\n"
			     << "startxref\n" << xrefOffset << "\n%%EOF\n";
			pdf += xref.str();

			EXPECT_NE(std::string::npos, pdf.find("/BaseFont /" + subsetName));
			EXPECT_NE(std::string::npos, pdf.find("/FontName /" + subsetName));
			EXPECT_NE(std::string::npos, pdf.find("<" + codes + "> Tj"));

			std::ofstream stream(fileName, std::ios::binary);
			ASSERT_TRUE(stream.good());
			stream.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
			ASSERT_TRUE(stream.good());
		}
	}

	TEST(LogicalType0Font, BuildsIndependentSemanticAndVisualMappings)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		const int advance = GetAdvance(source);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 1, advance);
		MapSourceGlyph(mapper, U"B", 1, advance);
		MapSourceGlyph(mapper, std::u32string{static_cast<char32_t>(0x1F600)}, 1, advance);

		const CLogicalType0FontResult result = BuildType0(source, mapper.GetShard());
		EXPECT_STREQ("Type0", result.FontSubtype);
		EXPECT_STREQ("Identity-H", result.GetEncoding());
		EXPECT_STREQ("CIDFontType2", result.DescendantFontSubtype);
		ASSERT_EQ(8u, result.CIDToGIDMap.size());
		EXPECT_EQ(0u, result.CIDToGIDMap[0]);
		EXPECT_EQ(0u, result.CIDToGIDMap[1]);
		EXPECT_EQ(result.CIDToGIDMap[3], result.CIDToGIDMap[5]);
		EXPECT_EQ(result.CIDToGIDMap[3], result.CIDToGIDMap[7]);
		EXPECT_NE(std::string::npos, result.ToUnicode.find("<0001> <0041>"));
		EXPECT_NE(std::string::npos, result.ToUnicode.find("<0002> <0042>"));
		EXPECT_NE(std::string::npos, result.ToUnicode.find("<0003> <D83DDE00>"));
		const std::size_t mappings = result.ToUnicode.find("beginbfchar\n");
		ASSERT_NE(std::string::npos, mappings);
		EXPECT_EQ(std::string::npos, result.ToUnicode.find("<0000> <", mappings));
		ASSERT_EQ(4u, result.Widths.size());
		EXPECT_EQ(0, result.Widths[0]);
		const int pdfWidth = (advance * 1000 + 896) / 1792;
		EXPECT_EQ(pdfWidth, result.Widths[1]);
		EXPECT_EQ(pdfWidth, result.Widths[2]);
		EXPECT_EQ(pdfWidth, result.Widths[3]);
	}

	TEST(LogicalType0Font, BuildsIdentityVWithExplicitVerticalMetrics)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		const int advance = GetAdvance(source);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", 1, advance);

		CLogicalType0FontResult result;
		CLogicalType0FontError error;
		ASSERT_TRUE(TryBuildLogicalType0Font(source, mapper.GetShard(), result, error,
		                                         std::string(), ELogicalPdfWritingMode::Vertical))
			<< error.Message;
		EXPECT_STREQ("Identity-V", result.GetEncoding());
		ASSERT_EQ(2u, result.VerticalMetrics.size());
		EXPECT_EQ(-result.Widths[1], result.VerticalMetrics[1].W1Y);
		EXPECT_EQ(0, result.VerticalMetrics[1].V1X);
		EXPECT_EQ(0, result.VerticalMetrics[1].V1Y);
	}

	TEST(LogicalType0Font, SplitsToUnicodeIntoBoundedBlocks)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		const int advance = GetAdvance(source);
		CLogicalFontMapper mapper;
		for (std::uint32_t index = 0; index < 101; ++index)
			MapSourceGlyph(mapper, std::u32string{static_cast<char32_t>(0x0100 + index)}, 1, advance);

		const CLogicalType0FontResult result = BuildType0(source, mapper.GetShard());
		EXPECT_NE(std::string::npos, result.ToUnicode.find("100 beginbfchar"));
		EXPECT_NE(std::string::npos, result.ToUnicode.find("1 beginbfchar"));
		EXPECT_EQ(204u, result.CIDToGIDMap.size());
	}

	TEST(LogicalType0Font, RejectsInvalidOrEmptySemanticTextWithoutChangingResult)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		const int advance = GetAdvance(source);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, std::u32string{static_cast<char32_t>(0xD800)}, 1, advance);
		CLogicalType0FontResult result;
		result.ToUnicode = "unchanged";
		CLogicalType0FontError error;
		EXPECT_FALSE(TryBuildLogicalType0Font(source, mapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalType0FontErrorCode::InvalidUnicodeScalar, error.Code);
		EXPECT_EQ("unchanged", result.ToUnicode);

		CLogicalFontMapper emptyMapper;
		MapSourceGlyph(emptyMapper, U"", 1, advance);
		EXPECT_FALSE(TryBuildLogicalType0Font(source, emptyMapper.GetShard(), result, error));
		EXPECT_EQ(CLogicalType0FontErrorCode::InvalidCidRecord, error.Code);
	}

	TEST(LogicalType0Font, WritesSourceBackedPdfFixture)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		const std::uint32_t glyphId = 13;
		const int advance = GetAdvance(source, glyphId);
		CLogicalFontMapper mapper;
		MapSourceGlyph(mapper, U"A", glyphId, advance);
		MapSourceGlyph(mapper, U"B", glyphId, advance);
		const CLogicalType0FontResult result = BuildType0(source, mapper.GetShard());
		WritePdfFixture(result, "phase3-source-backed.pdf", "ABCDEF+Phase3Subset", "00010002");
	}

	TEST(LogicalType0Font, WritesSyntheticMultiComponentPdfFixture)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		CLogicalFontMapper mapper;
		MapSyntheticGlyph(mapper, U"AB", 2800, {{13, 0, 0}, {14, 1000, 0}});
		const CLogicalType0FontResult result = BuildType0(source, mapper.GetShard());
		ASSERT_EQ(4u, result.CIDToGIDMap.size());
		EXPECT_EQ(3u, result.CIDToGIDMap[3]);
		EXPECT_NE(std::string::npos, result.ToUnicode.find("<0001> <00410042>"));
		WritePdfFixture(result, "phase4-synthetic.pdf", "ABCDEF+Phase4Synthetic", "0001");
	}
}
