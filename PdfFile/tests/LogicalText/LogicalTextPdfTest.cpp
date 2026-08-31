#include "LogicalTextSerializer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
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

		std::string BytesToString(const std::vector<std::uint8_t>& bytes)
		{
			return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		}

		std::string MakeStream(const std::string& attributes, const std::string& data)
		{
			return "<< /Length " + std::to_string(data.size()) + attributes +
			       " >>\nstream\n" + data + "\nendstream";
		}

		std::string MakeWidths(const CLogicalType0FontResult& font)
		{
			std::ostringstream widths;
			widths << "/W [1 [";
			for (std::size_t cid = 1; cid < font.Widths.size(); ++cid)
				widths << font.Widths[cid] << ' ';
			widths << "]]";
			return widths.str();
		}

		void AddFontObjects(std::vector<std::string>& objects,
		                    std::size_t firstObject,
		                    const std::string& subsetName,
		                    const CLogicalType0FontResult& font)
		{
			objects[firstObject] =
				"<< /Type /Font /Subtype /Type0 /BaseFont /" + subsetName +
				" /Encoding /Identity-H /DescendantFonts [" + std::to_string(firstObject + 1) +
				" 0 R] /ToUnicode " + std::to_string(firstObject + 5) + " 0 R >>";
			objects[firstObject + 1] =
				"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" + subsetName +
				" /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
				"/FontDescriptor " + std::to_string(firstObject + 2) +
				" 0 R /CIDToGIDMap " + std::to_string(firstObject + 4) +
				" 0 R /DW 1000 " + MakeWidths(font) + " >>";
			objects[firstObject + 2] =
				"<< /Type /FontDescriptor /FontName /" + subsetName +
				" /Flags 4 /FontBBox [-1000 -1000 2000 2000] /ItalicAngle 0 "
				"/Ascent 1000 /Descent -200 /CapHeight 700 /StemV 80 /FontFile2 " +
				std::to_string(firstObject + 3) + " 0 R >>";
			objects[firstObject + 3] =
				MakeStream(" /Length1 " + std::to_string(font.FontFile2.size()),
				           BytesToString(font.FontFile2));
			objects[firstObject + 4] = MakeStream("", BytesToString(font.CIDToGIDMap));
			objects[firstObject + 5] = MakeStream("", font.ToUnicode);
		}

		void WritePdf(const std::string& content,
		              const std::vector<CLogicalType0FontResult>& fonts)
		{
			ASSERT_EQ(2u, fonts.size());
			std::vector<std::string> objects(17);
			objects[1] = "<< /Type /Catalog /Pages 2 0 R >>";
			objects[2] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
			objects[3] =
				"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
				"/Resources << /Font << /LF0 5 0 R /LF1 11 0 R >> >> /Contents 4 0 R >>";
			objects[4] = MakeStream("", "BT\n" + content + "ET\n");
			AddFontObjects(objects, 5, "AAAAAA+LogicalShard0", fonts[0]);
			AddFontObjects(objects, 11, "BBBBBB+LogicalShard1", fonts[1]);

			std::string pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
			std::vector<std::size_t> offsets(objects.size(), 0);
			for (std::size_t index = 1; index < objects.size(); ++index)
			{
				offsets[index] = pdf.size();
				pdf += std::to_string(index) + " 0 obj\n" + objects[index] + "\nendobj\n";
			}
			const std::size_t xrefOffset = pdf.size();
			std::ostringstream xref;
			xref << "xref\n0 " << objects.size() << "\n0000000000 65535 f \n";
			for (std::size_t index = 1; index < objects.size(); ++index)
				xref << std::setw(10) << std::setfill('0') << offsets[index] << " 00000 n \n";
			xref << "trailer\n<< /Size " << objects.size() << " /Root 1 0 R >>\n"
			     << "startxref\n" << xrefOffset << "\n%%EOF\n";
			pdf += xref.str();

			std::ofstream stream("phase6-logical-source-order.pdf", std::ios::binary);
			ASSERT_TRUE(stream.good());
			stream.write(pdf.data(), static_cast<std::streamsize>(pdf.size()));
			ASSERT_TRUE(stream.good());
		}

		CLogicalUnitPlan MakePlan(const std::u32string& text,
		                          std::uint32_t glyphId,
		                          std::uint16_t advance,
		                          double x,
		                          double y)
		{
			CLogicalUnitPlan plan;
			plan.Text = text;
			plan.Visual.AdvanceWidth = advance;
			plan.Visual.Components.push_back({glyphId, 0, 0});
			plan.VisualX = x;
			plan.VisualY = y;
			return plan;
		}

		CLogicalUnitPlan MakeWordPlan(
			const std::u32string& text,
			const std::vector<std::pair<std::uint32_t, std::uint16_t>>& glyphs,
			double x,
			double y)
		{
			CLogicalUnitPlan plan;
			plan.Text = text;
			plan.VisualX = x;
			plan.VisualY = y;
			for (const auto& glyph : glyphs)
			{
				plan.Visual.Components.push_back({glyph.first, plan.Visual.AdvanceWidth, 0});
				plan.Visual.AdvanceWidth += glyph.second;
			}
			return plan;
		}
	}

	TEST(LogicalTextPdf, WritesTwoShardArabicHebrewAndMixedDirectionFixture)
	{
		const std::vector<std::uint8_t> source = ReadSourceFont();
		ASSERT_FALSE(source.empty());
		auto getAdvance = [&source](std::uint32_t glyphId) {
			std::uint16_t advance = 0;
			CLogicalTrueTypeSubsetError error;
			EXPECT_TRUE(TryGetTrueTypeGlyphAdvance(source, glyphId, advance, error)) << error.Message;
			return advance;
		};

		CShardedLogicalFontMapper mapper(source, 3);
		std::vector<CLogicalTextCommand> commands;
		const std::vector<std::pair<std::uint32_t, std::uint16_t>> arabicGlyphs = {
			{1365, getAdvance(1365)}, {1389, getAdvance(1389)}, {1383, getAdvance(1383)},
			{1375, getAdvance(1375)}, {1366, getAdvance(1366)}, {1395, getAdvance(1395)},
			{1367, getAdvance(1367)}};
		const std::vector<std::pair<std::uint32_t, std::uint16_t>> hebrewGlyphs = {
			{1337, getAdvance(1337)}, {1320, getAdvance(1320)}, {1343, getAdvance(1343)},
			{1328, getAdvance(1328)}, {1345, getAdvance(1345)}};
		auto pdfAdvance = [&getAdvance](std::uint32_t glyphId) {
			return static_cast<double>((static_cast<unsigned int>(getAdvance(glyphId)) * 1000u + 1024u) /
			                           2048u) /
			       1000.0;
		};
		const double mixedArabicX = pdfAdvance(36);
		const double mixedHebrewX = mixedArabicX + pdfAdvance(1366);
		const double mixedLatinX = mixedHebrewX + pdfAdvance(1320);
		const std::vector<CLogicalUnitPlan> plans = {
			MakeWordPlan(U"\u0627\u0644\u0639\u0631\u0628\u064A\u0629", arabicGlyphs, 0.0, 2.0),
			MakeWordPlan(U"\u05E2\u05D1\u05E8\u05D9\u05EA", hebrewGlyphs, 0.0, 1.0),
			MakePlan(U"A", 36, getAdvance(36), 0.0, 0.0),
			MakePlan(U"\u0628", 1366, getAdvance(1366), mixedArabicX, 0.0),
			MakePlan(U" \u05D1", 1320, getAdvance(1320), mixedHebrewX, 0.0),
			MakePlan(U"Z", 61, getAdvance(61), mixedLatinX, 0.0)};

		for (const CLogicalUnitPlan& plan : plans)
		{
			CShardedLogicalFontMapping mapping;
			CLogicalFontShardingError error;
			ASSERT_TRUE(mapper.TryMap(plan, mapping, error)) << error.Message;
			commands.push_back({plan, mapping, 0});
		}
		ASSERT_EQ(2u, mapper.GetShardCount());

		std::vector<CLogicalType0FontResult> fonts(mapper.GetShardCount());
		for (std::size_t index = 0; index < mapper.GetShardCount(); ++index)
		{
			CLogicalType0FontError error;
			ASSERT_TRUE(TryBuildLogicalType0Font(source, *mapper.GetShard(index), fonts[index], error))
				<< error.Message;
		}
		EXPECT_NE(std::string::npos,
		          fonts[0].ToUnicode.find("<0001> <06270644063906310628064A0629>"));
		EXPECT_NE(std::string::npos,
		          fonts[0].ToUnicode.find("<0002> <05E205D105E805D905EA>"));
		EXPECT_NE(std::string::npos, fonts[0].ToUnicode.find("<0003> <0041>"));
		EXPECT_NE(std::string::npos, fonts[1].ToUnicode.find("<0001> <0628>"));
		EXPECT_NE(std::string::npos, fonts[1].ToUnicode.find("<0002> <002005D1>"));
		EXPECT_NE(std::string::npos, fonts[1].ToUnicode.find("<0003> <005A>"));
		const std::vector<CLogicalTextFontResource> resources = {
			{"LF0", &fonts[0]}, {"LF1", &fonts[1]}};
		CLogicalTextSerializationOptions options;
		options.OriginX = 72.0;
		options.OriginY = 650.0;
		options.FontSize = 24.0;
		std::string content;
		CLogicalTextSerializationError error;
		ASSERT_TRUE(TrySerializeLogicalTextCommands(commands, resources, options, content, error))
			<< error.Message;

		EXPECT_LT(content.find("/LF0 24 Tf"), content.find("/LF1 24 Tf"));
		EXPECT_NE(std::string::npos, content.find(" TJ\n"));
		EXPECT_NE(std::string::npos, content.find("<0001>"));
		WritePdf(content, fonts);

		std::ofstream expected("phase6-expected-source-order.txt", std::ios::binary);
		ASSERT_TRUE(expected.good());
		const std::string expectedUtf8 =
			"\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9\n"
			"\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA\n"
			"A\xD8\xA8 \xD7\x91Z\n";
		expected.write(expectedUtf8.data(), static_cast<std::streamsize>(expectedUtf8.size()));
		ASSERT_TRUE(expected.good());
	}
}
